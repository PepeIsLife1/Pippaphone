// Lettori minimi dei formati che servono (senza librerie esterne):
//  - PNG a 8 bit RGB/RGBA non interlacciato (tasselli di elevazione Terrarium)
//  - ZIP (deflate o non compresso, senza zip64) per estrarre un file solo
//  - GeoTIFF float32 non compresso a strisce (TINITALY)
"use strict";

const zlib = require("zlib");

function decodificaPng(buf) {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error("non è un PNG");
  let p = 8, w = 0, h = 0, bit = 0, tipo = 0, inter = 0;
  const idat = [];
  while (p < buf.length) {
    const len = buf.readUInt32BE(p);
    const t = buf.toString("ascii", p + 4, p + 8);
    const d = buf.subarray(p + 8, p + 8 + len);
    if (t === "IHDR") {
      w = d.readUInt32BE(0); h = d.readUInt32BE(4); bit = d[8]; tipo = d[9]; inter = d[12];
    } else if (t === "IDAT") idat.push(d);
    else if (t === "IEND") break;
    p += 12 + len;
  }
  if (bit !== 8 || (tipo !== 2 && tipo !== 6) || inter !== 0) throw new Error(`PNG non gestito (bit ${bit}, tipo ${tipo})`);
  const bpp = tipo === 6 ? 4 : 3;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const riga = w * bpp;
  const out = Buffer.alloc(w * h * 3);
  let prec = Buffer.alloc(riga), cur = Buffer.alloc(riga);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (riga + 1)];
    const r = raw.subarray(y * (riga + 1) + 1, (y + 1) * (riga + 1));
    for (let i = 0; i < riga; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prec[i], c = i >= bpp ? prec[i - bpp] : 0;
      let v = r[i];
      if (f === 1) v += a;
      else if (f === 2) v += b;
      else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      out[(y * w + x) * 3] = cur[x * bpp];
      out[(y * w + x) * 3 + 1] = cur[x * bpp + 1];
      out[(y * w + x) * 3 + 2] = cur[x * bpp + 2];
    }
    [prec, cur] = [cur, prec];
  }
  return { w, h, rgb: out };
}

// Estrae da uno ZIP il primo file il cui nome finisce con `suffisso`.
function estraiDaZip(zip, suffisso) {
  let eocd = -1;
  for (let i = zip.length - 22; i >= Math.max(0, zip.length - 65557); i--) {
    if (zip.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error("ZIP senza directory centrale");
  const nVoci = zip.readUInt16LE(eocd + 10);
  let p = zip.readUInt32LE(eocd + 16);
  for (let k = 0; k < nVoci; k++) {
    if (zip.readUInt32LE(p) !== 0x02014b50) throw new Error("ZIP rovinato");
    const metodo = zip.readUInt16LE(p + 10);
    const csize = zip.readUInt32LE(p + 20);
    const nl = zip.readUInt16LE(p + 28), el = zip.readUInt16LE(p + 30), cl = zip.readUInt16LE(p + 32);
    const loc = zip.readUInt32LE(p + 42);
    const nome = zip.toString("utf8", p + 46, p + 46 + nl);
    if (nome.toLowerCase().endsWith(suffisso)) {
      const inizio = loc + 30 + zip.readUInt16LE(loc + 26) + zip.readUInt16LE(loc + 28);
      const dati = zip.subarray(inizio, inizio + csize);
      if (metodo === 0) return Buffer.from(dati);
      if (metodo === 8) return zlib.inflateRawSync(dati);
      throw new Error("metodo di compressione ZIP " + metodo);
    }
    p += 46 + nl + el + cl;
  }
  throw new Error("file " + suffisso + " non trovato nello ZIP");
}

// GeoTIFF float32 a strisce, non compresso: restituisce larghezza, altezza, origine, passo e dati.
function leggiGeoTiffF32(t) {
  const le = t.toString("ascii", 0, 2) === "II";
  const u16 = (o) => (le ? t.readUInt16LE(o) : t.readUInt16BE(o));
  const u32 = (o) => (le ? t.readUInt32LE(o) : t.readUInt32BE(o));
  const f64 = (o) => (le ? t.readDoubleLE(o) : t.readDoubleBE(o));
  const ifd = u32(4), n = u16(ifd);
  const tag = {};
  for (let i = 0; i < n; i++) {
    const e = ifd + 2 + i * 12;
    const id = u16(e), tipo = u16(e + 2), cnt = u32(e + 4);
    const dim = { 3: 2, 4: 4, 12: 8 }[tipo] || 1;
    const off = dim * cnt <= 4 ? e + 8 : u32(e + 8);
    const vals = [];
    for (let k = 0; k < cnt && k < 100000; k++) {
      if (tipo === 3) vals.push(u16(off + 2 * k));
      else if (tipo === 4) vals.push(u32(off + 4 * k));
      else if (tipo === 12) vals.push(f64(off + 8 * k));
    }
    tag[id] = vals;
  }
  const w = tag[256][0], h = tag[257][0];
  if (tag[258][0] !== 32 || (tag[339] && tag[339][0] !== 3)) throw new Error("GeoTIFF non float32");
  if (tag[259][0] !== 1) throw new Error("GeoTIFF compresso: non gestito");
  if (tag[284] && tag[284][0] !== 1) throw new Error("GeoTIFF non a piano unico");
  const offs = tag[273], conta = tag[279];
  const dati = new Float32Array(w * h);
  let k = 0;
  for (let s = 0; s < offs.length; s++) {
    const nv = conta[s] / 4;
    for (let i = 0; i < nv && k < dati.length; i++, k++) {
      dati[k] = le ? t.readFloatLE(offs[s] + 4 * i) : t.readFloatBE(offs[s] + 4 * i);
    }
  }
  return { w, h, passo: tag[33550][0], x0: tag[33922][3], y0: tag[33922][4], dati };
}

module.exports = { decodificaPng, estraiDaZip, leggiGeoTiffF32 };
