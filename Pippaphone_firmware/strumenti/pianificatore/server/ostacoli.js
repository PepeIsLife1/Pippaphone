// Ostacoli da OpenStreetMap (tasselli vettoriali OpenFreeMap, schema OpenMapTiles): edifici con
// altezza e boschi, per tasselli z13 (4 tasselli vettoriali z14 ciascuno).
// Ogni tassello diventa una griglia 1024x1024 di byte (circa 3-4 m per cella in Italia):
//   0 = niente, 1..250 = edificio alto tanti metri, 251 = edificio senza altezza nota (OSM mette
//   5 m: la pagina usa quella scelta nelle impostazioni), 252 = bosco.
// La griglia si salva compressa (gzip) e si spedisce cosi' com'e'.
"use strict";

const fs = require("fs");
const path = require("path");
const zlib = require("zlib");
const rete = require("./rete");
const mvt = require("./mvt");

const Z = 13;
const LATO = 1024;
const TILEJSON = "https://tiles.openfreemap.org/planet";
const IGNOTO = 251, BOSCO = 252;

let CACHE = "";
let urlTasselli = null, urlLetto = 0;
const inArrivo = new Map();
const inCorso = new Set();

function inizia(cartellaCache) { CACHE = cartellaCache; }

async function modelloUrl() {
  if (!urlTasselli || Date.now() - urlLetto > 24 * 3600 * 1000) {
    const tj = JSON.parse((await rete.scarica(TILEJSON)).toString("utf8"));
    urlTasselli = tj.tiles[0];
    urlLetto = Date.now();
  }
  return urlTasselli;
}

// Riempimento a scansione pari-dispari di tutti gli anelli di un elemento (i buchi restano vuoti).
function riempi(anelli, fn) {
  let ymin = Infinity, ymax = -Infinity;
  for (const a of anelli) for (const p of a) { ymin = Math.min(ymin, p[1]); ymax = Math.max(ymax, p[1]); }
  const r0 = Math.max(0, Math.floor(ymin)), r1 = Math.min(LATO - 1, Math.ceil(ymax));
  const xs = [];
  for (let r = r0; r <= r1; r++) {
    const yc = r + 0.5;
    xs.length = 0;
    for (const a of anelli) {
      for (let i = 0, j = a.length - 1; i < a.length; j = i++) {
        const [xi, yi] = a[i], [xj, yj] = a[j];
        if ((yi > yc) !== (yj > yc)) xs.push(xi + ((yc - yi) * (xj - xi)) / (yj - yi));
      }
    }
    xs.sort((a, b) => a - b);
    for (let k = 0; k + 1 < xs.length; k += 2) {
      const c0 = Math.max(0, Math.ceil(xs[k] - 0.5)), c1 = Math.min(LATO - 1, Math.floor(xs[k + 1] - 0.5));
      for (let c = c0; c <= c1; c++) fn(r * LATO + c);
    }
  }
}

async function genera(x, y) {
  const modello = await modelloUrl();
  const cella = new Uint8Array(LATO * LATO);
  const meta = LATO / 2;
  for (let dy = 0; dy < 2; dy++) {
    for (let dx = 0; dx < 2; dx++) {
      const url = modello.replace("{z}", "14").replace("{x}", String(2 * x + dx)).replace("{y}", String(2 * y + dy));
      let pbf;
      try {
        pbf = await rete.scarica(url);
      } catch (e) {
        if (e.status === 404 || e.status === 204) continue; // mare aperto: tassello vuoto
        throw e;
      }
      const d = mvt.decodifica(pbf, ["building", "landcover"]);
      const conv = (ext) => (a) => a.map(([cx, cy]) => [dx * meta + (cx / ext) * meta, dy * meta + (cy / ext) * meta]);
      if (d.landcover) {
        const c = conv(d.landcover.extent);
        for (const el of d.landcover.elementi) {
          if (el.tags.class !== "wood") continue;
          riempi(el.anelli.map(c), (i) => { if (!cella[i]) cella[i] = BOSCO; });
        }
      }
      if (d.building) {
        const c = conv(d.building.extent);
        for (const el of d.building.elementi) {
          if (el.tags.hide_3d) continue;
          const h = Math.max(1, Math.min(250, Math.round(+el.tags.render_height || 5)));
          const v = h <= 5 ? IGNOTO : h;
          riempi(el.anelli.map(c), (i) => {
            const prima = cella[i];
            if (prima === 0 || prima === BOSCO || prima === IGNOTO || (v !== IGNOTO && v > prima)) cella[i] = v;
          });
        }
      }
    }
  }
  return zlib.gzipSync(Buffer.from(cella.buffer), { level: 6 });
}

// Tassello di ostacoli compresso (gzip).
async function tassello(x, y) {
  const f = path.join(CACHE, "ostacoli", "ofm1", String(Z), String(x), y + ".bin.gz");
  if (fs.existsSync(f)) return fs.readFileSync(f);
  const chiave = x + "/" + y;
  if (inArrivo.has(chiave)) return inArrivo.get(chiave);
  const p = (async () => {
    inCorso.add(chiave);
    try {
      const gz = await genera(x, y);
      fs.mkdirSync(path.dirname(f), { recursive: true });
      fs.writeFileSync(f, gz);
      return gz;
    } finally {
      inCorso.delete(chiave);
    }
  })();
  inArrivo.set(chiave, p);
  try {
    return await p;
  } finally {
    inArrivo.delete(chiave);
  }
}

function statoOstacoli() { return { inCorso: [...inCorso] }; }

module.exports = { inizia, tassello, statoOstacoli, Z, LATO };
