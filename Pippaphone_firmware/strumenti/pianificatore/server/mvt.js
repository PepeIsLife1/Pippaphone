// Lettore minimo di tasselli vettoriali Mapbox (MVT, protobuf) per edifici e boschi.
"use strict";

function lettore(buf) {
  let p = 0;
  const r = {
    fine: () => p >= buf.length,
    varint() {
      let v = 0, mul = 1, b;
      do { b = buf[p++]; v += (b & 0x7f) * mul; mul *= 128; } while (b & 0x80);
      return v;
    },
    campo() { const k = r.varint(); return { n: Math.floor(k / 8), t: k & 7 }; },
    blocco() { const l = r.varint(); const s = buf.subarray(p, p + l); p += l; return s; },
    salta(t) {
      if (t === 0) r.varint();
      else if (t === 1) p += 8;
      else if (t === 2) p += r.varint();
      else if (t === 5) p += 4;
      else throw new Error("protobuf: tipo " + t);
    },
    double() { const v = buf.readDoubleLE(p); p += 8; return v; },
    float() { const v = buf.readFloatLE(p); p += 4; return v; },
  };
  return r;
}

function impaccati(buf) {
  const r = lettore(buf), out = [];
  while (!r.fine()) out.push(r.varint());
  return out;
}

function valore(buf) {
  const r = lettore(buf);
  let v = null;
  while (!r.fine()) {
    const { n, t } = r.campo();
    if (n === 1 && t === 2) v = r.blocco().toString("utf8");
    else if (n === 2 && t === 5) v = r.float();
    else if (n === 3 && t === 1) v = r.double();
    else if ((n === 4 || n === 5) && t === 0) v = r.varint();
    else if (n === 6 && t === 0) { const z = r.varint(); v = z % 2 ? -(z + 1) / 2 : z / 2; }
    else if (n === 7 && t === 0) v = !!r.varint();
    else r.salta(t);
  }
  return v;
}

// Anelli della geometria (coordinate del tassello, 0..extent).
function anelli(cmd) {
  const out = [];
  let x = 0, y = 0, cur = null;
  for (let i = 0; i < cmd.length;) {
    const c = cmd[i++], id = c & 7, n = c >> 3;
    if (id === 7) { if (cur && cur.length > 2) out.push(cur); cur = null; continue; }
    for (let k = 0; k < n; k++) {
      const dx = cmd[i++], dy = cmd[i++];
      x += dx % 2 ? -(dx + 1) / 2 : dx / 2;
      y += dy % 2 ? -(dy + 1) / 2 : dy / 2;
      if (id === 1) { if (cur && cur.length > 2) out.push(cur); cur = [[x, y]]; }
      else cur.push([x, y]);
    }
  }
  if (cur && cur.length > 2) out.push(cur);
  return out;
}

// Restituisce {nomeStrato: [{tag, anelli}]} per gli strati richiesti (solo poligoni).
function decodifica(buf, strati) {
  const r = lettore(buf), out = {};
  while (!r.fine()) {
    const { n, t } = r.campo();
    if (n !== 3 || t !== 2) { r.salta(t); continue; }
    const lr = lettore(r.blocco());
    let nome = "", extent = 4096;
    const chiavi = [], valori = [], grezze = [];
    while (!lr.fine()) {
      const f = lr.campo();
      if (f.n === 1 && f.t === 2) nome = lr.blocco().toString("utf8");
      else if (f.n === 2 && f.t === 2) grezze.push(lr.blocco());
      else if (f.n === 3 && f.t === 2) chiavi.push(lr.blocco().toString("utf8"));
      else if (f.n === 4 && f.t === 2) valori.push(valore(lr.blocco()));
      else if (f.n === 5 && f.t === 0) extent = lr.varint();
      else lr.salta(f.t);
    }
    if (!strati.includes(nome)) continue;
    const elementi = [];
    for (const g of grezze) {
      const fr = lettore(g);
      let tag = [], tipo = 0, geom = [];
      while (!fr.fine()) {
        const f = fr.campo();
        if (f.n === 2 && f.t === 2) tag = impaccati(fr.blocco());
        else if (f.n === 3 && f.t === 0) tipo = fr.varint();
        else if (f.n === 4 && f.t === 2) geom = impaccati(fr.blocco());
        else fr.salta(f.t);
      }
      if (tipo !== 3) continue;
      const tags = {};
      for (let i = 0; i + 1 < tag.length; i += 2) tags[chiavi[tag[i]]] = valori[tag[i + 1]];
      elementi.push({ tags, anelli: anelli(geom) });
    }
    out[nome] = { extent, elementi };
  }
  return out;
}

module.exports = { decodifica };
