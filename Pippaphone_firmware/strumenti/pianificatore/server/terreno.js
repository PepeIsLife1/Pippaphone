// Terreno: tasselli di quota 256x256 (Int16, passo 0,2 m) nella griglia Web Mercator.
// Fonte principale TINITALY 1.1 (INGV, 10 m, CC BY 4.0): i riquadri da 50 km si scaricano la prima
// volta che servono e restano in cache. Dove TINITALY non c'e' (mare, estero) si usano i tasselli
// Terrarium di AWS (circa 30 m in Europa).
"use strict";

const fs = require("fs");
const path = require("path");
const geo = require("./geo");
const { decodificaPng, estraiDaZip, leggiGeoTiffF32 } = require("./formati");
const rete = require("./rete");

const BASE_TINITALY = "https://tinitaly.pi.ingv.it/data_1.1/";
const NODATA = -32768;
const MAX_IN_MEMORIA = 6;

let CACHE = "";
const indice = new Map(); // "kx,ky" (cella di 50 km) -> riquadri che la toccano
const NESSUN_RIQUADRO = [];
const caricati = new Map(); // nome -> {w,h,x0,y0,q:Int16Array}, in ordine d'uso
const inArrivo = new Map(); // nome -> Promise
const stato = { download: new Map() }; // nome -> {mb, ricevuti}

function inizia(cartellaCache, fileIndice) {
  CACHE = cartellaCache;
  for (const [nome, x0, y0, w, h, zip] of JSON.parse(fs.readFileSync(fileIndice, "utf8"))) {
    // I riquadri non sono tutti da 50 km ne' allineati alla griglia (coste, confini, isole):
    // ognuno si segna in tutte le celle che tocca.
    const r = { nome, x0, y0, w, h, zip };
    const kx0 = Math.floor(x0 / 50000), kx1 = Math.floor((x0 + w * 10) / 50000);
    const ky0 = Math.floor((y0 - h * 10) / 50000), ky1 = Math.floor(y0 / 50000);
    for (let kx = kx0; kx <= kx1; kx++) {
      for (let ky = ky0; ky <= ky1; ky++) {
        const k = kx + "," + ky;
        if (!indice.has(k)) indice.set(k, []);
        indice.get(k).push(r);
      }
    }
  }
}

// Riquadri della cella di 50 km in cui cade il punto (puo' essercene piu' d'uno, sovrapposti).
function riquadriVicini(e, n) {
  return indice.get(Math.floor(e / 50000) + "," + Math.floor(n / 50000)) || NESSUN_RIQUADRO;
}
function contiene(r, e, n) {
  return e >= r.x0 && e <= r.x0 + r.w * 10 && n <= r.y0 && n >= r.y0 - r.h * 10;
}

async function caricaRiquadro(r) {
  if (caricati.has(r.nome)) {
    const t = caricati.get(r.nome);
    caricati.delete(r.nome);
    caricati.set(r.nome, t); // piu' recente in fondo
    return t;
  }
  if (inArrivo.has(r.nome)) return inArrivo.get(r.nome);
  const p = (async () => {
    const dir = path.join(CACHE, "tinitaly");
    const fbin = path.join(dir, r.nome + ".bin");
    if (!fs.existsSync(fbin)) {
      stato.download.set(r.nome, { mb: Math.round(r.zip / 1e6), ricevuti: 0 });
      try {
        const url = `${BASE_TINITALY}${r.nome}_s10/${r.nome}_s10.zip`;
        const zip = await rete.scarica(url, (n) => (stato.download.get(r.nome).ricevuti = n));
        const tif = leggiGeoTiffF32(estraiDaZip(zip, ".tif"));
        const q = new Int16Array(tif.w * tif.h);
        for (let i = 0; i < q.length; i++) {
          const v = tif.dati[i];
          q[i] = v > -1000 && v < 6000 ? Math.round(v * 5) : NODATA;
        }
        fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(fbin + ".tmp", Buffer.from(q.buffer));
        fs.renameSync(fbin + ".tmp", fbin);
        fs.writeFileSync(path.join(dir, r.nome + ".json"),
          JSON.stringify({ w: tif.w, h: tif.h, x0: tif.x0, y0: tif.y0, passo: tif.passo }));
      } finally {
        stato.download.delete(r.nome);
      }
    }
    const meta = JSON.parse(fs.readFileSync(path.join(dir, r.nome + ".json"), "utf8"));
    const b = fs.readFileSync(fbin);
    const q = new Int16Array(b.buffer, b.byteOffset, b.length / 2);
    const t = { ...meta, q };
    caricati.set(r.nome, t);
    while (caricati.size > MAX_IN_MEMORIA) caricati.delete(caricati.keys().next().value);
    // (chi sta componendo un tassello tiene comunque i suoi riquadri finche' non ha finito)
    return t;
  })();
  inArrivo.set(r.nome, p);
  try {
    return await p;
  } finally {
    inArrivo.delete(r.nome);
  }
}

// Quota bilineare (metri) dal riquadro gia' caricato; NaN se manca il dato.
function quotaTinitaly(t, e, n) {
  const fx = (e - t.x0) / t.passo - 0.5, fy = (t.y0 - n) / t.passo - 0.5;
  const i = Math.floor(fx), j = Math.floor(fy);
  if (i < 0 || j < 0 || i >= t.w - 1 || j >= t.h - 1) return NaN;
  const a = t.q[j * t.w + i], b = t.q[j * t.w + i + 1], c = t.q[(j + 1) * t.w + i], d = t.q[(j + 1) * t.w + i + 1];
  if (a === NODATA || b === NODATA || c === NODATA || d === NODATA) return NaN;
  const u = fx - i, v = fy - j;
  return ((a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v) / 5;
}

async function tasselloAws(z, x, y) {
  const f = path.join(CACHE, "aws", String(z), String(x), y + ".png");
  let png;
  if (fs.existsSync(f)) png = fs.readFileSync(f);
  else {
    png = await rete.scarica(`https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${z}/${x}/${y}.png`);
    fs.mkdirSync(path.dirname(f), { recursive: true });
    fs.writeFileSync(f, png);
  }
  const { w, rgb } = decodificaPng(png);
  if (w !== 256) throw new Error("tassello Terrarium inatteso");
  const h = new Float32Array(65536);
  for (let i = 0; i < 65536; i++) {
    const v = rgb[i * 3] * 256 + rgb[i * 3 + 1] + rgb[i * 3 + 2] / 256 - 32768;
    h[i] = Math.max(0, v); // in mare conta la superficie dell'acqua
  }
  return h;
}

// Tassello di quota pronto da spedire (Buffer di 65536 Int16 little-endian).
async function tassello(z, x, y, fonte) {
  // "tinitaly2": i tasselli fatti con il vecchio indice potevano avere quote AWS dove TINITALY c'era.
  const f = path.join(CACHE, "dem", fonte === "tinitaly" ? "tinitaly2" : fonte, String(z), String(x), y + ".bin");
  if (fs.existsSync(f)) return fs.readFileSync(f);
  const out = new Int16Array(65536);
  const utm = new Float64Array(65536 * 2);
  const servono = new Map();
  if (fonte === "tinitaly") {
    for (let py = 0; py < 256; py++) {
      const lat = geo.latDaY(y + (py + 0.5) / 256, z);
      for (let px = 0; px < 256; px++) {
        const lon = geo.lonDaX(x + (px + 0.5) / 256, z);
        const u = geo.utm32(lat, lon);
        utm[(py * 256 + px) * 2] = u.e;
        utm[(py * 256 + px) * 2 + 1] = u.n;
        for (const r of riquadriVicini(u.e, u.n)) if (contiene(r, u.e, u.n)) servono.set(r.nome, r);
      }
    }
  }
  const riq = new Map();
  for (const r of servono.values()) riq.set(r.nome, await caricaRiquadro(r));
  let aws = null, mancanti = 0;
  for (let i = 0; i < 65536; i++) {
    let h = NaN;
    if (riq.size) {
      const e = utm[i * 2], n = utm[i * 2 + 1];
      // Dove due riquadri si sovrappongono vale il primo che ha il dato (sul bordo o in mare manca).
      for (const r of riquadriVicini(e, n)) {
        if (!contiene(r, e, n)) continue;
        h = quotaTinitaly(riq.get(r.nome), e, n);
        if (!Number.isNaN(h)) break;
      }
    }
    if (Number.isNaN(h)) {
      if (!aws) aws = await tasselloAws(z, x, y);
      h = aws[i];
      mancanti++;
    }
    out[i] = Math.max(-6000, Math.min(32000, Math.round(h * 5)));
  }
  const buf = Buffer.from(out.buffer);
  fs.mkdirSync(path.dirname(f), { recursive: true });
  fs.writeFileSync(f, buf);
  return buf;
}

function statoDownload() {
  return [...stato.download.entries()].map(([nome, s]) => ({ nome, mb: s.mb, ricevutiMb: Math.round(s.ricevuti / 1e6) }));
}

module.exports = { inizia, tassello, statoDownload, riquadriVicini, contiene };
