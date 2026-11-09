// Pianificatore radio PippaNet: server locale (solo Node.js, nessuna libreria da installare).
//   node server.js            -> http://localhost:8787
// Scarica e tiene in cache (cartella cache/) mappe, terreno TINITALY/AWS ed edifici/boschi OSM.
"use strict";

const http = require("http");
const fs = require("fs");
const path = require("path");
const terreno = require("./server/terreno");
const ostacoli = require("./server/ostacoli");
const rete = require("./server/rete");

const PORTA = +(process.env.PORTA || 8787);
const QUI = __dirname;
const WEB = path.join(QUI, "web");
// La cache (fino a qualche GB di terreno) sta fuori dal progetto: il progetto e' in OneDrive.
const CACHE = process.env.CACHE_PIANIFICATORE ||
  (process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA, "PippaphonePianificatore", "cache")
    : path.join(require("os").homedir(), ".cache", "pippaphone-pianificatore"));

terreno.inizia(CACHE, path.join(QUI, "dati", "tinitaly_indice.json"));
ostacoli.inizia(CACHE);

const MAPPE = {
  osm: { url: (z, x, y) => `https://tile.openstreetmap.org/${z}/${x}/${y}.png`, maxZ: 19 },
  topo: { url: (z, x, y) => `https://${"abc"[(x + y) % 3]}.tile.opentopomap.org/${z}/${x}/${y}.png`, maxZ: 17 },
};
const TIPI = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
  ".json": "application/json", ".wasm": "application/wasm", ".png": "image/png", ".svg": "image/svg+xml",
};
const errori = [];

function rispondi(res, codice, tipo, corpo, extra = {}) {
  res.writeHead(codice, { "Content-Type": tipo, ...extra });
  res.end(corpo);
}
function errore(res, e) {
  const msg = String(e && e.message ? e.message : e);
  errori.push({ quando: new Date().toISOString(), msg });
  if (errori.length > 20) errori.shift();
  rispondi(res, 502, "application/json", JSON.stringify({ errore: msg }));
}

async function mappa(res, stile, z, x, y) {
  const m = MAPPE[stile];
  if (!m || z > m.maxZ) return rispondi(res, 404, "text/plain", "no");
  const f = path.join(CACHE, "mappe", stile, String(z), String(x), y + ".png");
  let png;
  if (fs.existsSync(f)) png = fs.readFileSync(f);
  else {
    png = await rete.scarica(m.url(z, x, y));
    fs.mkdirSync(path.dirname(f), { recursive: true });
    fs.writeFileSync(f, png);
  }
  rispondi(res, 200, "image/png", png, { "Cache-Control": "public, max-age=604800" });
}

// Leaflet da unpkg, tenuto in cache (cosi' la pagina funziona anche senza rete).
async function libreria(res, rel) {
  const f = path.join(CACHE, "lib", rel);
  let b;
  if (fs.existsSync(f)) b = fs.readFileSync(f);
  else {
    b = await rete.scarica("https://unpkg.com/" + rel);
    fs.mkdirSync(path.dirname(f), { recursive: true });
    fs.writeFileSync(f, b);
  }
  rispondi(res, 200, TIPI[path.extname(f)] || "application/octet-stream", b, { "Cache-Control": "public, max-age=604800" });
}

function statico(res, url) {
  let rel = decodeURIComponent(url.split("?")[0]);
  if (rel === "/") rel = "/index.html";
  const f = path.normalize(path.join(WEB, rel));
  if (!f.startsWith(WEB) || !fs.existsSync(f) || !fs.statSync(f).isFile()) return rispondi(res, 404, "text/plain", "non trovato");
  rispondi(res, 200, TIPI[path.extname(f)] || "application/octet-stream", fs.readFileSync(f), { "Cache-Control": "no-cache" });
}

const server = http.createServer(async (req, res) => {
  const u = req.url;
  let m;
  try {
    if ((m = u.match(/^\/api\/dem\/(tinitaly|aws)\/(\d+)\/(\d+)\/(\d+)$/))) {
      const z = +m[2];
      if (z < 8 || z > 14) return rispondi(res, 400, "text/plain", "zoom 8-14");
      const buf = await terreno.tassello(z, +m[3], +m[4], m[1]);
      return rispondi(res, 200, "application/octet-stream", buf, { "Cache-Control": "public, max-age=31536000" });
    }
    if ((m = u.match(/^\/api\/ostacoli\/(\d+)\/(\d+)$/))) {
      const gz = await ostacoli.tassello(+m[1], +m[2]);
      return rispondi(res, 200, "application/octet-stream", gz,
        { "Content-Encoding": "gzip", "Cache-Control": "public, max-age=31536000" });
    }
    if (u === "/api/stato") {
      return rispondi(res, 200, "application/json",
        JSON.stringify({ terreno: terreno.statoDownload(), ostacoli: ostacoli.statoOstacoli(), errori: errori.slice(-5) }));
    }
    if ((m = u.match(/^\/mappa\/(\w+)\/(\d+)\/(\d+)\/(\d+)\.png$/))) return await mappa(res, m[1], +m[2], +m[3], +m[4]);
    if ((m = u.match(/^\/lib\/(leaflet@1\.9\.4\/dist\/[\w./-]+)$/)) && !m[1].includes("..")) return await libreria(res, m[1]);
    return statico(res, u);
  } catch (e) {
    return errore(res, e);
  }
});

server.listen(PORTA, "127.0.0.1", () => {
  console.log(`Pianificatore PippaNet pronto: http://localhost:${PORTA}`);
  console.log(`Cache: ${CACHE}`);
});
