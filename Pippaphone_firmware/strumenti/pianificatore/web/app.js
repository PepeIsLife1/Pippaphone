// Pianificatore PippaNet: interfaccia (mappa, dispositivi, collegamenti, copertura, rete).
"use strict";

// ------------------------------------------------------------------ dati fissi

const TIPI = {
  tdeck: { nome: "T-Deck Plus", breve: "T-Deck", ruolo: "telefono", colore: "#2563eb",
    ptx: 22, guadagno: 1, cavo: 0, corpo: 3, altezza: 1.5, dentro: false, piano: 0, muri: "tradizionale",
    nota: "telefono, 22 dBm, in mano" },
  casa: { nome: "Ripetitore da casa", breve: "Casa", ruolo: "ripetitore", colore: "#ea580c",
    ptx: 22, guadagno: 0, cavo: 0, corpo: 0, altezza: 1.5, dentro: true, piano: 1, muri: "tradizionale",
    nota: "Heltec V3, in casa, sempre acceso" },
  esterno: { nome: "Ripetitore da esterno", breve: "Esterno", ruolo: "ripetitore", colore: "#16a34a",
    ptx: 22, guadagno: 2.5, cavo: 0.5, corpo: 0, altezza: 8, dentro: false, piano: 0, muri: "tradizionale",
    nota: "T3-S3 solare, su tetto o palo" },
  e22: { nome: "Ripetitore 1 W", breve: "1 W", ruolo: "ripetitore", colore: "#9333ea",
    ptx: 30, guadagno: 3, cavo: 1, corpo: 0, altezza: 10, dentro: false, piano: 0, muri: "tradizionale",
    nota: "E22-900M30S, PCB da disegnare" },
};
const MURI = {
  tradizionale: { nome: "Muri tradizionali (mattoni, vetri normali)", db: 14 },
  cemento: { nome: "Cemento armato, lontano dalle finestre", db: 20 },
  moderno: { nome: "Edificio moderno isolato (vetri basso-emissivi)", db: 31 },
  finestra: { nome: "Vicino a una finestra", db: 7 },
};
const POSIZIONI = { mano: { nome: "In mano", db: 3 }, tasca: { nome: "In tasca o nello zaino", db: 10 },
  auto: { nome: "In auto", db: 8 }, appoggiato: { nome: "Appoggiato, libero", db: 0 } };
const RICEVITORI = {
  mano: { hAnt: 1.5, guadagno: 1, perdite: 3 },
  tasca: { hAnt: 1.2, guadagno: 1, perdite: 10 },
  casa: { hAnt: 4.5, guadagno: 0, perdite: 0, sempreDentro: true, dipolo: true },
  esterno: { hAnt: 8, guadagno: 2.5, perdite: 0.5, maiMuri: true, dipolo: true },
};
const Z_COLLEGAMENTO = 14;
const CHIAVE_SALVATAGGIO = "pianificatore-pippanet-v1";

// ------------------------------------------------------------------ stato

const S = {
  nodi: [],
  sel: null,
  profilo: "sf7",
  fonte: "tinitaly",
  mappa: "topo",
  // sigma 9 dB e ostacoli non in mappa 4 m: tarati sulle misure LoRa di Funchal (vedi LEGGIMI).
  imp: { cifraRumore: 6, rumoreExtra: 0, sigma: 9, k: 4 / 3, chiomaM: 15, edificioIgnotoM: 8, nonMappatiM: 4, distMaxKm: 60 },
  muri: Object.fromEntries(Object.entries(MURI).map(([k, v]) => [k, v.db])),
  vista: { lat: 42.6, lon: 12.4, zoom: 6 },
};
const terr = new Terreno.Territorio("");
let piazzaTipo = null;
let prossimoId = 1;
const cachePercorsi = new Map(); // geometria -> {prof, p}
let rete = new Map(); // "idA|idB" -> collegamento
let reteLinee = [];
let collegamentoAttuale = null;

const $ = (id) => document.getElementById(id);
const fmt = (v, d = 1) => (Number.isFinite(v) ? v.toFixed(d).replace(".", ",") : "—");
const profiloRadio = () => Radio.PROFILI[S.profilo];
const nodo = (id) => S.nodi.find((n) => n.id === id);

function altezzaAntenna(n) { return n.dentro ? 1.5 + 3 * n.piano : n.altezza; }
function muriDb(n) { return n.dentro ? S.muri[n.muri] : 0; }
function parametriNodo(n) {
  return { ptx: n.ptx, guadagno: n.guadagno, perdite: n.cavo + n.corpo + muriDb(n) };
}

function salva() {
  const c = map.getCenter();
  S.vista = { lat: c.lat, lon: c.lng, zoom: map.getZoom() };
  try {
    localStorage.setItem(CHIAVE_SALVATAGGIO, JSON.stringify(scenario()));
  } catch (e) { /* senza memoria locale si perde solo il ripristino */ }
}
function scenario() {
  const nodi = S.nodi.map(({ marcatore, ...resto }) => resto);
  return { versione: 2, nodi, profilo: S.profilo, fonte: S.fonte, mappa: S.mappa, imp: S.imp, muri: S.muri, vista: S.vista };
}
function caricaScenario(sc) {
  for (const n of S.nodi) n.marcatore && n.marcatore.remove();
  S.nodi = [];
  S.sel = null;
  const imp = { ...(sc.imp || {}) };
  // Versione 1: il fading di 7 dB era quello di prima della taratura, non una scelta.
  if ((sc.versione || 1) < 2 && imp.sigma === 7) delete imp.sigma;
  Object.assign(S.imp, imp);
  Object.assign(S.muri, sc.muri || {});
  S.profilo = Radio.PROFILI[sc.profilo] ? sc.profilo : "sf7";
  S.fonte = sc.fonte === "aws" ? "aws" : "tinitaly";
  S.mappa = sc.mappa === "osm" ? "osm" : "topo";
  prossimoId = 1;
  for (const n of sc.nodi || []) {
    const t = TIPI[n.tipo];
    if (!t) continue;
    const nn = { ...t, ...n };
    delete nn.marcatore;
    nn.id = prossimoId++;
    S.nodi.push(nn);
    aggiungiMarcatore(nn);
  }
  if (sc.vista) map.setView([sc.vista.lat, sc.vista.lon], sc.vista.zoom);
  terr.fonte = S.fonte;
  terr.dem.clear();
  terr.edificioIgnotoM = S.imp.edificioIgnotoM;
  terr.chiomaM = S.imp.chiomaM;
  cachePercorsi.clear();
  svuotaRete();
  togliCopertura();
  aggiornaTutto();
}

// ------------------------------------------------------------------ messaggi di stato

let timerStato = null, caricamenti = 0;
function messaggio(t, ms) {
  const el = $("stato");
  el.textContent = t;
  el.classList.toggle("visibile", !!t);
  clearTimeout(timerStato);
  if (ms) timerStato = setTimeout(() => el.classList.remove("visibile"), ms);
}
async function conCaricamento(testo, fn) {
  caricamenti++;
  messaggio(testo);
  const giro = setInterval(controllaServer, 1500);
  try {
    return await fn();
  } finally {
    clearInterval(giro);
    caricamenti--;
    if (!caricamenti) messaggio("");
    if (terr.errori.length) {
      messaggio("Alcuni dati non sono arrivati: " + terr.errori.slice(-1)[0], 8000);
      terr.errori.length = 0;
    }
  }
}
async function controllaServer() {
  try {
    const s = await (await fetch("/api/stato")).json();
    const parti = [];
    for (const d of s.terreno) parti.push(`Scarico TINITALY ${d.nome}: ${d.ricevutiMb}/${d.mb} MB`);
    if (s.ostacoli.inCorso.length) parti.push(`Edifici e boschi: ${s.ostacoli.inCorso.length} tasselli`);
    if (parti.length) messaggio(parti.join(" · "));
  } catch (e) { /* server spento */ }
}

// ------------------------------------------------------------------ mappa

const map = L.map("mappa", { zoomControl: true, preferCanvas: true }).setView([S.vista.lat, S.vista.lon], S.vista.zoom);
const strati = {
  topo: L.tileLayer("/mappa/topo/{z}/{x}/{y}.png", { maxZoom: 17,
    attribution: "© OpenStreetMap, SRTM · stile © OpenTopoMap (CC-BY-SA) · TINITALY INGV · OpenFreeMap" }),
  osm: L.tileLayer("/mappa/osm/{z}/{x}/{y}.png", { maxZoom: 19,
    attribution: "© OpenStreetMap · TINITALY INGV · OpenFreeMap" }),
};
let stratoBase = null;
function scegliMappa(k) {
  if (stratoBase) stratoBase.remove();
  stratoBase = strati[k].addTo(map);
  stratoBase.bringToBack();
}
map.createPane("copertura").style.zIndex = 350; // sotto le linee della rete
const gruppoRete = L.layerGroup().addTo(map);
const gruppoPercorso = L.layerGroup().addTo(map);

function iconaSvg(tipo, dim = 26) {
  const c = TIPI[tipo].colore;
  const corpo = {
    tdeck: `<rect x="6" y="2" width="12" height="20" rx="2.5" fill="${c}"/><rect x="8" y="4.5" width="8" height="6" rx="1" fill="#fff" opacity=".92"/>
      <g fill="#fff" opacity=".85"><rect x="8" y="12.5" width="8" height="1.5" rx=".6"/><rect x="8" y="15.2" width="8" height="1.5" rx=".6"/><rect x="8" y="17.9" width="5" height="1.5" rx=".6"/></g>`,
    casa: `<path d="M2.5 11.5 L12 3.5 L21.5 11.5 V21.5 H2.5 Z" fill="${c}"/><rect x="9.8" y="14" width="4.4" height="7.5" fill="#fff" opacity=".9"/>
      <path d="M17.5 7 V1.8" stroke="${c}" stroke-width="1.8" stroke-linecap="round"/>`,
    esterno: `<path d="M9 4 V22" stroke="${c}" stroke-width="2.2" stroke-linecap="round"/><circle cx="9" cy="3.5" r="2.2" fill="${c}"/>
      <path d="M12 12 L21.5 9.5 V15.5 L12 18 Z" fill="${c}"/><path d="M4 7.5 Q2.5 3.5 4.5 0.8 M14 7.5 Q15.5 3.5 13.5 0.8" stroke="${c}" stroke-width="1.4" fill="none"/>`,
    e22: `<path d="M12 4 L6 22 M12 4 L18 22 M8 16 H16 M9.5 11 H14.5" stroke="${c}" stroke-width="2" fill="none" stroke-linecap="round"/>
      <circle cx="12" cy="3.5" r="2.4" fill="${c}"/>`,
  }[tipo];
  return `<svg class="icona" viewBox="0 0 24 24" width="${dim}" height="${dim}"><g stroke="#fff" stroke-width="0" >${corpo}</g></svg>`;
}

function iconaMarcatore(n) {
  return L.divIcon({
    className: "marcatore" + (S.sel === n.id ? " sel" : ""),
    html: iconaSvg(n.tipo, 28) + `<span class="etichetta">${escape(n.nome)}</span>`,
    iconSize: [28, 28], iconAnchor: [14, 14],
  });
}
function escape(s) { return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]); }

function aggiungiMarcatore(n) {
  const m = L.marker([n.lat, n.lon], { draggable: true, icon: iconaMarcatore(n), autoPan: true });
  m.on("click", (e) => {
    if (e.originalEvent.shiftKey && S.sel && S.sel !== n.id) apriCollegamento(nodo(S.sel), n);
    else seleziona(n.id);
  });
  m.on("dragend", () => {
    const p = m.getLatLng();
    n.lat = p.lat; n.lon = p.lng;
    nodoCambiato(n);
  });
  m.addTo(map);
  n.marcatore = m;
}

map.on("click", (e) => {
  if (!piazzaTipo) return;
  const t = TIPI[piazzaTipo];
  const telefoni = S.nodi.filter((x) => x.ruolo === "telefono");
  const ripetitori = S.nodi.filter((x) => x.ruolo === "ripetitore");
  const numero = t.ruolo === "telefono"
    ? Math.max(1000, ...telefoni.map((x) => x.numero)) + 1
    : Math.max(0, ...ripetitori.map((x) => x.numero)) + 1;
  const n = { ...t, tipo: piazzaTipo, id: prossimoId++, lat: e.latlng.lat, lon: e.latlng.lng, numero,
    nome: `${t.breve} ${numero}`, posizione: "mano" };
  S.nodi.push(n);
  aggiungiMarcatore(n);
  if (!e.originalEvent.shiftKey) impostaPiazza(null);
  seleziona(n.id);
  nodoCambiato(n);
});
map.on("moveend", salva);
document.addEventListener("keydown", (e) => { if (e.key === "Escape") impostaPiazza(null); });

function impostaPiazza(t) {
  piazzaTipo = t;
  document.querySelectorAll(".tipo").forEach((b) => b.classList.toggle("attivo", b.dataset.tipo === t));
  map.getContainer().classList.toggle("piazza", !!t);
  $("nota-piazza").textContent = t
    ? `Clicca sulla mappa per mettere: ${TIPI[t].nome}. Maiusc + clic per metterne più di uno, Esc per smettere.`
    : "Scegli un dispositivo e poi clicca sulla mappa. Trascinali per spostarli.";
}

// ------------------------------------------------------------------ pannello sinistro

function costruisciTavolozza() {
  const el = $("tavolozza");
  el.innerHTML = "";
  for (const [k, t] of Object.entries(TIPI)) {
    const b = document.createElement("button");
    b.className = "tipo";
    b.dataset.tipo = k;
    b.innerHTML = iconaSvg(k) + `<span>${t.nome}<small>${t.nota}</small></span>`;
    b.onclick = () => impostaPiazza(piazzaTipo === k ? null : k);
    el.appendChild(b);
  }
}

function aggiornaLista() {
  const ul = $("lista-nodi");
  ul.innerHTML = "";
  $("conta-nodi").textContent = S.nodi.length ? `(${S.nodi.length})` : "";
  for (const n of S.nodi) {
    const li = document.createElement("li");
    li.className = S.sel === n.id ? "sel" : "";
    li.innerHTML = iconaSvg(n.tipo, 18) + `<span>${escape(n.nome)}</span><small>${fmt(altezzaAntenna(n), 1)} m</small>`;
    li.onclick = (e) => {
      if (e.shiftKey && S.sel && S.sel !== n.id) return apriCollegamento(nodo(S.sel), n);
      seleziona(n.id);
      map.panTo([n.lat, n.lon]);
    };
    ul.appendChild(li);
  }
  if (!S.nodi.length) ul.innerHTML = `<li class="nota">Ancora niente.</li>`;
}

function seleziona(id) {
  const prima = S.sel;
  S.sel = id;
  for (const n of S.nodi) if (n.id === prima || n.id === id) n.marcatore.setIcon(iconaMarcatore(n));
  aggiornaLista();
  formProprieta();
  aggiornaSelect();
}

function campoNumero(etichetta, valore, min, max, passo, cb, unita = "") {
  const d = document.createElement("div");
  d.className = "campo";
  d.innerHTML = `<span>${etichetta}</span><input type="number" min="${min}" max="${max}" step="${passo}" value="${valore}"> ${unita}`;
  d.querySelector("input").onchange = (e) => {
    const v = parseFloat(e.target.value);
    if (Number.isFinite(v)) cb(Math.max(min, Math.min(max, v)));
  };
  return d;
}
function campoSelect(etichetta, opzioni, valore, cb) {
  const d = document.createElement("div");
  d.className = "campo";
  d.innerHTML = `<span>${etichetta}</span><select>${Object.entries(opzioni)
    .map(([k, v]) => `<option value="${k}" ${k === valore ? "selected" : ""}>${escape(v)}</option>`).join("")}</select>`;
  d.querySelector("select").onchange = (e) => cb(e.target.value);
  return d;
}

function formProprieta() {
  const n = nodo(S.sel);
  $("proprieta").hidden = !n;
  if (!n) return;
  const f = $("form-proprieta");
  f.innerHTML = "";
  const t = TIPI[n.tipo];
  const testa = document.createElement("div");
  testa.className = "campo";
  testa.innerHTML = `<span>Nome</span><input value="${escape(n.nome)}">`;
  testa.querySelector("input").onchange = (e) => { n.nome = e.target.value || n.nome; nodoCambiato(n, true); };
  f.appendChild(testa);
  f.appendChild(Object.assign(document.createElement("p"), { className: "nota", textContent: `${t.nome} · ${t.nota}` }));
  const cambia = (k, geo = false) => (v) => { n[k] = v; nodoCambiato(n, !geo); };
  f.appendChild(campoNumero(n.ruolo === "telefono" ? "Numero" : "Numero rip.", n.numero, 1, 65000, 1, cambia("numero")));
  f.appendChild(campoNumero("Potenza", n.ptx, 0, 30, 1, cambia("ptx"), "dBm"));
  f.appendChild(campoNumero("Antenna", n.guadagno, -6, 9, 0.5, cambia("guadagno"), "dBi"));
  f.appendChild(campoNumero("Cavo", n.cavo, 0, 10, 0.1, cambia("cavo"), "dB"));
  if (n.ruolo === "telefono") {
    f.appendChild(campoSelect("Dove", Object.fromEntries(Object.entries(POSIZIONI).map(([k, v]) => [k, `${v.nome} (${v.db} dB)`])),
      n.posizione || "mano", (v) => { n.posizione = v; n.corpo = POSIZIONI[v].db; nodoCambiato(n, true); }));
  }
  const dentro = document.createElement("label");
  dentro.className = "spunta";
  dentro.innerHTML = `<input type="checkbox" ${n.dentro ? "checked" : ""}> Dentro un edificio`;
  dentro.querySelector("input").onchange = (e) => { n.dentro = e.target.checked; nodoCambiato(n); formProprieta(); };
  f.appendChild(dentro);
  if (n.dentro) {
    f.appendChild(campoNumero("Piano", n.piano, 0, 40, 1, cambia("piano", true), "(0 = terra)"));
    f.appendChild(campoSelect("Muri", Object.fromEntries(Object.entries(MURI).map(([k, v]) => [k, `${v.nome} (${S.muri[k]} dB)`])),
      n.muri, (v) => { n.muri = v; nodoCambiato(n, true); }));
  } else {
    f.appendChild(campoNumero("Altezza", n.altezza, 0.5, 120, 0.5, cambia("altezza", true), "m da terra"));
  }
  const info = document.createElement("p");
  info.className = "nota";
  info.id = "info-nodo";
  f.appendChild(info);
  const b = document.createElement("div");
  b.className = "bottoni";
  b.innerHTML = `<button data-a="cop">Copertura da qui</button><button data-a="del">Elimina</button>`;
  b.querySelector('[data-a="cop"]').onclick = () => { $("cop-tx").value = String(n.id); mostraScheda("copertura"); calcolaCopertura(); };
  b.querySelector('[data-a="del"]').onclick = () => elimina(n);
  f.appendChild(b);
  infoQuota(n);
}

async function infoQuota(n) {
  const el = $("info-nodo");
  if (!el) return;
  await terr.carica([n.lat, n.lon], Z_COLLEGAMENTO);
  if (S.sel !== n.id) return;
  const q = terr.quota(n.lat, n.lon, Z_COLLEGAMENTO);
  const o = terr.ostacolo(n.lat, n.lon);
  let t = `Terreno ${fmt(q, 0)} m s.l.m. · antenna a ${fmt(q + altezzaAntenna(n), 0)} m s.l.m.`;
  if (o.h > 0) t += ` · sopra un edificio di ${o.h} m${o.ignoto ? " (altezza stimata)" : ""}`;
  if (o.bosco) t += " · in un bosco";
  if (o.h > 0 && !n.dentro && altezzaAntenna(n) < o.h)
    t += `. Attenzione: l'antenna è più bassa dell'edificio, segna "Dentro un edificio" o alzala.`;
  el.textContent = t;
}

function elimina(n) {
  n.marcatore.remove();
  S.nodi = S.nodi.filter((x) => x !== n);
  if (S.sel === n.id) S.sel = null;
  for (const k of [...rete.keys()]) if (k.split("|").includes(String(n.id))) rete.delete(k);
  disegnaRete();
  if (collegamentoAttuale && (collegamentoAttuale.a === n || collegamentoAttuale.b === n)) chiudiCollegamento();
  aggiornaTutto();
}

// geo = la posizione o l'altezza sono cambiate (serve ricalcolare i percorsi).
function nodoCambiato(n, soloRadio = false) {
  n.marcatore.setIcon(iconaMarcatore(n));
  if (!soloRadio) {
    for (const k of [...rete.keys()]) if (k.split("|").includes(String(n.id))) rete.delete(k);
    coperturaVecchia(n.id);
  }
  aggiornaTutto();
  if (collegamentoAttuale && (collegamentoAttuale.a === n || collegamentoAttuale.b === n))
    apriCollegamento(collegamentoAttuale.a, collegamentoAttuale.b);
  if (rete.size) aggiornaRete();
}

function aggiornaTutto() {
  aggiornaLista();
  formProprieta();
  aggiornaSelect();
  salva();
  if (window.Simulazione) Simulazione.reteCambiata();
}

function aggiornaSelect() {
  const telefoni = S.nodi.filter((n) => n.ruolo === "telefono");
  const opz = (lista) => lista.map((n) => `<option value="${n.id}">${escape(n.nome)}</option>`).join("");
  for (const id of ["per-da", "per-a", "sim-da", "sim-a"]) {
    const s = $(id), v = s.value;
    s.innerHTML = opz(telefoni);
    if ([...s.options].some((o) => o.value === v)) s.value = v;
  }
  if (telefoni.length > 1 && $("per-da").value === $("per-a").value) $("per-a").value = String(telefoni[1].id);
  if (telefoni.length > 1 && $("sim-da").value === $("sim-a").value) $("sim-a").value = String(telefoni[1].id);
  const s = $("cop-tx"), v = s.value;
  s.innerHTML = opz(S.nodi) + (S.nodi.some((n) => n.ruolo === "ripetitore") ? `<option value="rip">Tutti i ripetitori</option>` : "");
  if ([...s.options].some((o) => o.value === v)) s.value = v;
  else if (S.sel) s.value = String(S.sel);
}

// ------------------------------------------------------------------ collegamento A-B

function chiaveGeometria(a, b) {
  return [a.lat, a.lon, altezzaAntenna(a), b.lat, b.lon, altezzaAntenna(b), S.imp.k, S.imp.chiomaM,
    S.imp.edificioIgnotoM, S.imp.nonMappatiM, !!a.dentro, !!b.dentro, a.ruolo, b.ruolo, S.fonte]
    .map((v) => (typeof v === "number" ? v.toFixed(6) : v)).join(",");
}

// Opzioni del modello che dipendono dai due capi: chi e' dentro casa non ha ostacoli di strada
// (contano i muri); i ripetitori hanno un'antenna verticale fissa (diagramma a dipolo).
function opzioniCapi(a, b) {
  return { nonMappati: S.imp.nonMappatiM, dentroTx: !!a.dentro, dentroRx: !!b.dentro,
    dipoloTx: a.ruolo === "ripetitore", dipoloRx: b.ruolo === "ripetitore" };
}

async function percorso(a, b) {
  const chiave = chiaveGeometria(a, b);
  if (cachePercorsi.has(chiave)) return cachePercorsi.get(chiave);
  const D = Terreno.distanzaM(a.lat, a.lon, b.lat, b.lon);
  const n = Math.min(4000, Math.max(100, Math.ceil(D / 5)));
  await terr.carica(Terreno.puntiSegmento(a, b, 5), Z_COLLEGAMENTO);
  const prof = terr.profilo(a, b, n, Z_COLLEGAMENTO);
  const scratch = Radio.preparaScratch(prof, S.imp.chiomaM);
  const o = { hTx: altezzaAntenna(a), hRx: altezzaAntenna(b), fMHz: profiloRadio().msg.f, k: S.imp.k, chioma: S.imp.chiomaM,
    saltaTx: Math.max(0, Radio.campioniProprioEdificio(prof.ed, false) - 1),
    saltaRx: Math.max(0, Radio.campioniProprioEdificio(prof.ed, true) - 1), ...opzioniCapi(a, b) };
  const p = Radio.perditaPercorso(prof, n, o, scratch);
  const r = { D, prof, p, o };
  cachePercorsi.set(chiave, r);
  if (cachePercorsi.size > 200) cachePercorsi.delete(cachePercorsi.keys().next().value);
  return r;
}

// Bilancio nei due versi e sui due canali.
function bilanci(a, b, L) {
  const pr = profiloRadio(), ta = parametriNodo(a), tb = parametriNodo(b);
  const f = (tx, rx, ch) => Radio.bilancio(L, tx, rx, pr[ch], S.imp);
  return { ab: { msg: f(ta, tb, "msg"), voce: f(ta, tb, "voce") }, ba: { msg: f(tb, ta, "msg"), voce: f(tb, ta, "voce") } };
}

async function collegamento(a, b) {
  const c = await percorso(a, b);
  return { a, b, ...c, ...bilanci(a, b, c.p.totale) };
}

function esito(margine) {
  if (margine >= 10) return `<span class="ok">passa bene</span>`;
  if (margine >= 0) return `<span class="limite">al limite</span>`;
  if (margine >= -5) return `<span class="limite">spesso perso</span>`;
  return `<span class="no">non passa</span>`;
}

async function apriCollegamento(a, b) {
  mostraScheda("collegamento");
  const c = await conCaricamento("Calcolo il profilo…", () => collegamento(a, b));
  collegamentoAttuale = c;
  $("collegamento-vuoto").hidden = true;
  $("collegamento").hidden = false;
  $("col-titolo").textContent = `${a.nome} → ${b.nome} · ${fmt(c.D / 1000, 2)} km`;
  disegnaProfilo(c);
  const p = c.p;
  $("col-perdite").innerHTML = `
    <tr><th>Perdita</th><th>dB</th></tr>
    <tr><td>Spazio libero</td><td>${fmt(p.spazioLibero)}</td></tr>
    <tr><td>Diffrazione (terreno + edifici)</td><td>${fmt(p.diffrazione)}</td></tr>
    <tr><td>Riflesso del terreno (oltre la diffrazione)</td><td>${fmt(p.suolo)}</td></tr>
    <tr><td>Vegetazione${p.profonditaBosco ? ` (${fmt(p.profonditaBosco, 0)} m di bosco)` : ""}</td><td>${fmt(p.vegetazione)}</td></tr>
    <tr><td>Ostacoli non in mappa (alberi, auto, muretti)</td><td>${fmt(p.nonMappati)}</td></tr>
    <tr><td>Antenne dei ripetitori (angolo verticale)</td><td>${fmt(p.antenne)}</td></tr>
    <tr><td>Muri, corpo e cavi (${escape(a.nome)} + ${escape(b.nome)})</td><td>${fmt(parametriNodo(a).perdite + parametriNodo(b).perdite)}</td></tr>
    <tr><td><b>Totale</b></td><td><b>${fmt(p.totale + parametriNodo(a).perdite + parametriNodo(b).perdite)}</b></td></tr>`;
  const pr = profiloRadio();
  const riga = (nome, f, d = 1) => `<tr><td>${nome}</td><td>${f("ab", "msg")}</td><td>${f("ab", "voce")}</td><td>${f("ba", "msg")}</td><td>${f("ba", "voce")}</td></tr>`;
  const v = (k, d = 1) => (dir, ch) => fmt(c[dir][ch][k], d);
  $("col-bilancio").innerHTML = `
    <tr><th></th><th colspan="2">${escape(a.nome)} → ${escape(b.nome)}</th><th colspan="2">${escape(b.nome)} → ${escape(a.nome)}</th></tr>
    <tr><th></th><th>Messaggi</th><th>Voce</th><th>Messaggi</th><th>Voce</th></tr>
    ${riga("RSSI (dBm)", v("rssi"))}
    ${riga("SNR (dB)", v("snr"))}
    <tr><td>Sensibilità (dBm)</td><td>${fmt(Radio.sensibilitaDbm(pr.msg, S.imp))}</td><td>${fmt(Radio.sensibilitaDbm(pr.voce, S.imp))}</td>
      <td>${fmt(Radio.sensibilitaDbm(pr.msg, S.imp))}</td><td>${fmt(Radio.sensibilitaDbm(pr.voce, S.imp))}</td></tr>
    ${riga("<b>Margine (dB)</b>", (dir, ch) => `<b>${fmt(c[dir][ch].margine)}</b>`)}
    ${riga("Probabilità di arrivo", (dir, ch) => `${fmt(c[dir][ch].prob * 100, 0)}%`)}
    ${riga("Esito", (dir, ch) => esito(c[dir][ch].margine))}`;
  const ab = c.ab;
  const ta = (len, ch) => fmt(Radio.tempoInAriaMs(pr[ch], len) / 1000, 2) + " s";
  $("col-bitrate").innerHTML = `
    <tr><th></th><th>Messaggi</th><th>Voce</th></tr>
    <tr><td>Bit rate grezzo</td><td>${fmt(Radio.bitRate(pr.msg), 0)} bit/s</td><td>${fmt(Radio.bitRate(pr.voce), 0)} bit/s</td></tr>
    <tr><td>Conferma (${Radio.PACCHETTI.conferma} B)</td><td>${ta(Radio.PACCHETTI.conferma, "msg")}</td><td>—</td></tr>
    <tr><td>Testo di 80 caratteri</td><td>${ta(Radio.PACCHETTI.testo80, "msg")}</td><td>—</td></tr>
    <tr><td>Testo di 160 caratteri</td><td>${ta(Radio.PACCHETTI.testo160, "msg")}</td><td>—</td></tr>
    <tr><td>Pezzo di vocale (2,16 s di audio)</td><td>—</td><td>${ta(Radio.PACCHETTI.vocale, "voce")}</td></tr>
    <tr><td>Vocale di 10 s, diretto</td><td>—</td><td>${fmt(Radio.tempoVocaleS(pr.voce, 10, false), 1)} s</td></tr>`;
}

function chiudiCollegamento() {
  collegamentoAttuale = null;
  $("collegamento-vuoto").hidden = false;
  $("collegamento").hidden = true;
}

function disegnaProfilo(c) {
  const cv = $("grafico-profilo");
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth || 400, H = Math.round(W * 0.52);
  cv.width = W * dpr; cv.height = H * dpr;
  cv.style.height = H + "px";
  const g = cv.getContext("2d");
  g.scale(dpr, dpr);
  g.clearRect(0, 0, W, H);
  const { prof, D, o } = c;
  const n = prof.n, k2R = 2 * S.imp.k * 6371000;
  const cur = (i) => (prof.d[i] * (D - prof.d[i])) / k2R;
  const ha = prof.terra[0] + o.hTx, hb = prof.terra[n] + o.hRx;
  const lam = 299.792458 / profiloRadio().msg.f;
  let ymin = Infinity, ymax = -Infinity;
  for (let i = 0; i <= n; i++) {
    const t = prof.terra[i] + cur(i);
    ymin = Math.min(ymin, t);
    ymax = Math.max(ymax, t + prof.ed[i] + (prof.bosco[i] ? S.imp.chiomaM : 0));
    const d1 = prof.d[i], d2 = D - d1;
    ymax = Math.max(ymax, ha + ((hb - ha) * d1) / D + Math.sqrt((lam * d1 * d2) / Math.max(D, 1)));
  }
  ymax = Math.max(ymax, ha, hb);
  const pad = (ymax - ymin) * 0.08 + 2;
  ymin -= pad; ymax += pad;
  const sx = (d) => 40 + (d / D) * (W - 50), sy = (h) => 8 + (1 - (h - ymin) / (ymax - ymin)) * (H - 30);
  // bosco
  g.fillStyle = "rgba(63,157,75,.55)";
  for (let i = 0; i < n; i++) {
    if (!prof.bosco[i]) continue;
    const t = prof.terra[i] + cur(i);
    g.fillRect(sx(prof.d[i]), sy(t + S.imp.chiomaM), Math.max(1, sx(prof.d[i + 1]) - sx(prof.d[i])), sy(t) - sy(t + S.imp.chiomaM));
  }
  // edifici
  g.fillStyle = "#7b8494";
  for (let i = 0; i < n; i++) {
    if (!(prof.ed[i] > 0)) continue;
    const t = prof.terra[i] + cur(i);
    g.fillRect(sx(prof.d[i]), sy(t + prof.ed[i]), Math.max(1, sx(prof.d[i + 1]) - sx(prof.d[i])), sy(t) - sy(t + prof.ed[i]));
  }
  // terreno
  g.beginPath();
  g.moveTo(sx(0), H - 22);
  for (let i = 0; i <= n; i++) g.lineTo(sx(prof.d[i]), sy(prof.terra[i] + cur(i)));
  g.lineTo(sx(D), H - 22);
  g.closePath();
  g.fillStyle = "#a98c6a";
  g.fill();
  // Fresnel e vista
  g.strokeStyle = "#1d5fd1";
  g.setLineDash([4, 3]);
  g.lineWidth = 1;
  for (const s of [1, -1]) {
    g.beginPath();
    for (let i = 0; i <= n; i += Math.max(1, Math.floor(n / 300))) {
      const d1 = prof.d[i];
      const r = Math.sqrt((lam * d1 * (D - d1)) / Math.max(D, 1));
      const y = ha + ((hb - ha) * d1) / D + s * r;
      i === 0 ? g.moveTo(sx(d1), sy(y)) : g.lineTo(sx(d1), sy(y));
    }
    g.lineTo(sx(D), sy(hb));
    g.stroke();
  }
  g.setLineDash([]);
  g.lineWidth = 2;
  g.beginPath(); g.moveTo(sx(0), sy(ha)); g.lineTo(sx(D), sy(hb)); g.stroke();
  // antenne
  g.strokeStyle = "#1f2430"; g.lineWidth = 2;
  for (const [d, base, cima, col] of [[0, prof.terra[0], ha, TIPI[c.a.tipo].colore], [D, prof.terra[n], hb, TIPI[c.b.tipo].colore]]) {
    g.beginPath(); g.moveTo(sx(d), sy(base)); g.lineTo(sx(d), sy(cima)); g.stroke();
    g.fillStyle = col; g.beginPath(); g.arc(sx(d), sy(cima), 4, 0, 7); g.fill();
  }
  // assi
  g.fillStyle = "#5d6575"; g.font = "11px system-ui";
  g.textAlign = "right";
  g.fillText(`${Math.round(ymax)} m`, 37, 16);
  g.fillText(`${Math.round(ymin)} m`, 37, H - 24);
  g.textAlign = "center";
  g.fillText("0", sx(0), H - 8);
  g.fillText(`${fmt(D / 2000, 2)} km`, sx(D / 2), H - 8);
  g.textAlign = "right";
  g.fillText(`${fmt(D / 1000, 2)} km`, sx(D), H - 8);
}

// ------------------------------------------------------------------ rete

function svuotaRete() {
  rete = new Map();
  disegnaRete();
  $("rete-tabella").innerHTML = "";
  $("per-esito").innerHTML = "";
  gruppoPercorso.clearLayers();
}

function coppie() {
  const out = [];
  for (let i = 0; i < S.nodi.length; i++) for (let j = i + 1; j < S.nodi.length; j++) {
    const a = S.nodi[i], b = S.nodi[j];
    if (Terreno.distanzaM(a.lat, a.lon, b.lat, b.lon) <= S.imp.distMaxKm * 1000) out.push([a, b]);
  }
  return out;
}

// Calcola i collegamenti che mancano.
async function aggiornaRete(barra = false) {
  const lista = coppie().filter(([a, b]) => !rete.has(a.id + "|" + b.id));
  let fatti = 0;
  if (barra) avanzamento("rete-barra", 0, "");
  await conCaricamento("Calcolo chi sente chi…", async () => {
    for (const [a, b] of lista) {
      const c = await percorso(a, b);
      rete.set(a.id + "|" + b.id, { a, b, D: c.D, p: c.p });
      fatti++;
      if (barra) avanzamento("rete-barra", fatti / lista.length, `${fatti} / ${lista.length}`);
    }
  });
  if (barra) $("rete-barra").hidden = true;
  disegnaRete();
}

function margineMin(c, ch) { return Math.min(c.ab[ch].margine, c.ba[ch].margine); }

function collegamentiAttuali() {
  const out = [];
  for (const [k, c] of rete) {
    if (!nodo(c.a.id) || !nodo(c.b.id)) continue;
    out.push({ ...c, ...bilanci(c.a, c.b, c.p.totale) });
  }
  return out;
}

function coloreMargine(m) {
  if (m >= 10) return "#16a34a";
  if (m >= 0) return "#eab308";
  if (m >= -5) return "#f97316";
  return "#dc2626";
}

function disegnaRete() {
  gruppoRete.clearLayers();
  const tutti = $("rete-tutti").checked;
  const righe = [];
  for (const c of collegamentiAttuali()) {
    const m = margineMin(c, "msg"), mv = margineMin(c, "voce");
    righe.push(c);
    if (m < -5 && !tutti) continue;
    if (c.a.ruolo === "telefono" && c.b.ruolo === "telefono" && m < 0 && !tutti) continue;
    const linea = L.polyline([[c.a.lat, c.a.lon], [c.b.lat, c.b.lon]], {
      color: coloreMargine(m), weight: m >= 0 ? 4 : 2.5, opacity: 0.85, dashArray: mv < 0 && m >= 0 ? "7 6" : null,
    });
    linea.bindTooltip(`${escape(c.a.nome)} ↔ ${escape(c.b.nome)}: ${fmt(c.D / 1000, 2)} km<br>` +
      `messaggi ${fmt(m)} dB · voce ${fmt(mv)} dB`, { sticky: true });
    linea.on("click", () => apriCollegamento(c.a, c.b));
    linea.addTo(gruppoRete);
  }
  righe.sort((x, y) => margineMin(y, "msg") - margineMin(x, "msg"));
  $("rete-tabella").innerHTML = righe.length ? `<tr><th>Collegamento</th><th>km</th><th>Messaggi</th><th>Voce</th></tr>` +
    righe.map((c, i) => `<tr class="cliccabile" data-i="${i}"><td>${escape(c.a.nome)} ↔ ${escape(c.b.nome)}</td><td>${fmt(c.D / 1000, 2)}</td>
      <td style="color:${coloreMargine(margineMin(c, "msg"))}">${fmt(margineMin(c, "msg"))} dB</td>
      <td style="color:${coloreMargine(margineMin(c, "voce"))}">${fmt(margineMin(c, "voce"))} dB</td></tr>`).join("") : "";
  $("rete-tabella").querySelectorAll("tr.cliccabile").forEach((tr) => {
    tr.onclick = () => { const c = righe[+tr.dataset.i]; apriCollegamento(c.a, c.b); };
  });
}

function infoCollegamento(a, b) {
  const c = rete.get(a.id + "|" + b.id) || rete.get(b.id + "|" + a.id);
  if (!c) return null;
  const bl = bilanci(c.a, c.b, c.p.totale);
  return c.a === a ? { D: c.D, avanti: bl.ab, indietro: bl.ba } : { D: c.D, avanti: bl.ba, indietro: bl.ab };
}

// Percorso migliore: meno salti, poi il collegamento peggiore piu' forte. Ripetono solo i ripetitori.
function trovaPercorso(da, a) {
  const migliore = new Map([[da.id, { salti: 0, collo: Infinity, prima: null }]]);
  const fatti = new Set();
  for (;;) {
    let cur = null;
    for (const [id, v] of migliore) {
      if (fatti.has(id)) continue;
      if (!cur || v.salti < cur.v.salti || (v.salti === cur.v.salti && v.collo > cur.v.collo)) cur = { id, v };
    }
    if (!cur) return null;
    if (cur.id === a.id) break;
    fatti.add(cur.id);
    const n = nodo(cur.id);
    if (n !== da && n.ruolo !== "ripetitore") continue;
    for (const m of S.nodi) {
      if (m.id === cur.id || fatti.has(m.id)) continue;
      const info = infoCollegamento(n, m);
      if (!info) continue;
      const mm = Math.min(info.avanti.msg.margine, info.indietro.msg.margine);
      if (mm < 0) continue;
      const cand = { salti: cur.v.salti + 1, collo: Math.min(cur.v.collo, mm), prima: cur.id };
      const v = migliore.get(m.id);
      if (!v || cand.salti < v.salti || (cand.salti === v.salti && cand.collo > v.collo)) migliore.set(m.id, cand);
    }
  }
  const strada = [];
  for (let id = a.id; id != null; id = migliore.get(id).prima) strada.unshift(nodo(id));
  return strada;
}

async function cercaPercorso() {
  const da = nodo(+$("per-da").value), a = nodo(+$("per-a").value);
  if (!da || !a || da === a) return ($("per-esito").innerHTML = `<p class="nota">Servono due telefoni diversi.</p>`);
  await aggiornaRete();
  const strada = trovaPercorso(da, a);
  gruppoPercorso.clearLayers();
  if (!strada) {
    $("per-esito").innerHTML = `<p class="no">Nessun percorso: manca un collegamento con margine ≥ 0 dB.</p>`;
    return;
  }
  L.polyline(strada.map((n) => [n.lat, n.lon]), { color: "#1d5fd1", weight: 9, opacity: 0.35 }).addTo(gruppoPercorso);
  const pr = profiloRadio();
  const salti = strada.length - 1;
  // La probabilita' di un salto e' quella che il posto sia buono (Phi(margine / sigma)): e' la
  // stessa nei due versi (il percorso e' quello) e non cambia ritentando. I tentativi del
  // telefono servono contro il fading di un pacchetto, non contro un posto sfortunato.
  let pMsg = 1, pVoce = 1, tNota = 0, tFlood = 0, peggioVoce = null;
  const righe = [];
  const air = Radio.tempoInAriaMs(pr.msg, Radio.PACCHETTI.testo80) / 1000;
  for (let i = 0; i < salti; i++) {
    const info = infoCollegamento(strada[i], strada[i + 1]);
    pMsg *= Math.min(info.avanti.msg.prob, info.indietro.msg.prob);
    pVoce *= Math.min(info.avanti.voce.prob, info.indietro.voce.prob);
    if (Math.min(info.avanti.voce.margine, info.indietro.voce.margine) < 0 && !peggioVoce) peggioVoce = `${strada[i].nome} → ${strada[i + 1].nome}`;
    const ritardo = i === 0 ? 0 : 0.06 + 0.08 * (salti - 1 - i) + 0.1;
    tNota += air + ritardo;
    tFlood += air + (i === 0 ? 0 : air * 1.25);
    righe.push(`<tr><td>${escape(strada[i].nome)} → ${escape(strada[i + 1].nome)}</td><td>${fmt(info.D / 1000, 2)}</td>
      <td style="color:${coloreMargine(info.avanti.msg.margine)}">${fmt(info.avanti.msg.margine)} dB</td>
      <td style="color:${coloreMargine(info.avanti.voce.margine)}">${fmt(info.avanti.voce.margine)} dB</td></tr>`);
  }
  const ripetitori = salti > 1;
  $("per-esito").innerHTML = `
    <table class="tabella"><tr><th>Salto</th><th>km</th><th>Messaggi</th><th>Voce</th></tr>${righe.join("")}</table>
    <table class="tabella">
      <tr><td>Salti</td><td>${salti} (${salti - 1} ripetitori)</td></tr>
      <tr><td>Probabilità che la strada regga per i messaggi (tutti i salti)</td><td><b>${fmt(pMsg * 100, 0)}%</b></td></tr>
      <tr><td>…e anche per la voce</td><td>${fmt(pVoce * 100, 0)}%</td></tr>
      <tr><td>Testo di 80 caratteri, strada già nota</td><td>~${fmt(tNota, 1)} s</td></tr>
      <tr><td>Testo di 80 caratteri, prima volta (flood)</td><td>~${fmt(tFlood, 1)} s</td></tr>
      <tr><td>Bit rate grezzo fino in fondo</td><td>${fmt(Radio.bitRate(pr.msg) / salti, 0)} bit/s</td></tr>
      <tr><td>Vocale di 10 s</td><td>~${fmt(Radio.tempoVocaleS(pr.voce, 10, ripetitori), 1)} s</td></tr>
    </table>
    <p class="nota">Le probabilità dicono quanto è facile che i posti scelti vadano bene davvero (incertezza della mappa, σ ${fmt(S.imp.sigma, 0)} dB). Ritentare aiuta contro il fading, non contro un posto sfortunato: se una strada non regge, sposta o alza un ripetitore.</p>
    ${peggioVoce ? `<p class="limite">La voce probabilmente non passa su ${escape(peggioVoce)}: il canale voce ha ${fmt(Radio.sensibilitaDbm(pr.voce, S.imp) - Radio.sensibilitaDbm(pr.msg, S.imp))} dB in meno.</p>` : ""}`;
}

// ------------------------------------------------------------------ copertura

const NUM_WORKER = Math.max(2, Math.min(8, (navigator.hardwareConcurrency || 4) - 1));
let workers = [];
let lavoroId = 0;
const coperture = new Map(); // id nodo -> risultato per raggi
let coperturaStrato = null, coperturaGriglia = null;

function prendiWorker() {
  if (!workers.length) workers = Array.from({ length: NUM_WORKER }, () => new Worker("copertura.js"));
  return workers;
}

function avanzamento(id, f, testo) {
  const b = $(id);
  b.hidden = false;
  b.querySelector("div").style.width = `${Math.round(f * 100)}%`;
  b.querySelector("span").textContent = testo;
}

function coperturaVecchia(id) {
  coperture.delete(id);
}

function chiaveCopertura(n, rx, R, passo) {
  return [n.lat, n.lon, altezzaAntenna(n), rx.hAnt, R, passo, S.fonte, S.imp.k, S.imp.chiomaM, S.imp.edificioIgnotoM,
    S.imp.nonMappatiM, !!n.dentro, n.ruolo, !!rx.sempreDentro, !!rx.dipolo].join(",");
}

async function coperturaNodo(n, rx, R, passo, testo) {
  const chiave = chiaveCopertura(n, rx, R, passo);
  const c = coperture.get(n.id);
  if (c && c.chiave === chiave) return c;
  const nCampioni = Math.round(R / passo);
  const nRadiali = Math.min(4000, Math.ceil((2 * Math.PI * R) / passo));
  const ws = prendiWorker();
  const id = ++lavoroId;
  const L = new Float32Array(nRadiali * nCampioni), dentro = new Uint8Array(nRadiali * nCampioni);
  const stato = ws.map(() => ({ carico: 0, fatto: 0 }));
  await new Promise((ok, ko) => {
    let finiti = 0;
    ws.forEach((w, i) => {
      const a0 = Math.floor((i * nRadiali) / ws.length), a1 = Math.floor(((i + 1) * nRadiali) / ws.length);
      w.onmessage = (ev) => {
        const m = ev.data;
        if (m.id !== id) return;
        if (m.errore) return ko(new Error(m.errore));
        if (m.carico != null) stato[i].carico = m.carico;
        if (m.fatto != null) stato[i].fatto = m.fatto;
        if (m.finito) {
          L.set(m.L, m.a0 * nCampioni);
          dentro.set(m.dentro, m.a0 * nCampioni);
          stato[i].fatto = 1;
          if (m.errori && m.errori.length) terr.errori.push(...m.errori);
          if (++finiti === ws.length) ok();
        }
        const car = stato.reduce((s, x) => s + x.carico, 0) / ws.length;
        const fat = stato.reduce((s, x) => s + x.fatto, 0) / ws.length;
        avanzamento("cop-barra", car < 1 ? car * 0.3 : 0.3 + fat * 0.7, car < 1 ? `${testo}: carico terreno ed edifici…` : `${testo}: calcolo ${Math.round(fat * 100)}%`);
      };
      w.postMessage({ id, fonte: S.fonte, edificioIgnotoM: S.imp.edificioIgnotoM, chiomaM: S.imp.chiomaM, k: S.imp.k,
        zDem: R <= 4000 ? 14 : 13, fMHz: profiloRadio().msg.f, passo, nCampioni, nRadiali, a0, a1,
        nonMappati: S.imp.nonMappatiM,
        tx: { lat: n.lat, lon: n.lon, hAnt: altezzaAntenna(n), dentro: !!n.dentro, dipolo: n.ruolo === "ripetitore" },
        rx: { hAnt: rx.hAnt, dentro: !!rx.sempreDentro, dipolo: !!rx.dipolo } });
    });
  });
  const ris = { chiave, nodo: n, R, passo, nCampioni, nRadiali, L, dentro };
  coperture.set(n.id, ris);
  return ris;
}

function ricevitoreScelto() {
  return RICEVITORI[$("cop-rx").value];
}

async function calcolaCopertura() {
  const scelta = $("cop-tx").value;
  const tx = scelta === "rip" ? S.nodi.filter((n) => n.ruolo === "ripetitore") : [nodo(+scelta)].filter(Boolean);
  if (!tx.length) return messaggio("Metti prima un dispositivo sulla mappa.", 3000);
  const R = +$("cop-raggio").value * 1000;
  const passo = Math.max(8, R / 400) * +$("cop-dettaglio").value;
  const rx = ricevitoreScelto();
  $("cop-calcola").disabled = true;
  try {
    const t0 = performance.now();
    const ris = [];
    await conCaricamento("Calcolo la copertura…", async () => {
      for (let i = 0; i < tx.length; i++) {
        ris.push(await coperturaNodo(tx[i], rx, R, passo, tx.length > 1 ? `${tx[i].nome} (${i + 1}/${tx.length})` : tx[i].nome));
      }
    });
    coperturaGriglia = { ris, R, passo };
    $("cop-esito").dataset.secondi = fmt((performance.now() - t0) / 1000, 1);
    disegnaCopertura();
  } catch (e) {
    messaggio("Copertura non riuscita: " + e.message, 8000);
  } finally {
    $("cop-calcola").disabled = false;
    $("cop-barra").hidden = true;
  }
}

function togliCopertura() {
  if (coperturaStrato) coperturaStrato.remove();
  coperturaStrato = null;
  coperturaGriglia = null;
  $("cop-esito").textContent = "";
}

const COLORI = [[15, [11, 122, 59]], [10, [34, 160, 74]], [5, [132, 204, 22]], [0, [250, 204, 21]], [-5, [249, 115, 22]]];

function disegnaCopertura() {
  if (!coperturaGriglia) return;
  const { ris, R, passo } = coperturaGriglia;
  const pr = profiloRadio();
  const ch = $("cop-canale").value;
  const rx = ricevitoreScelto();
  const muriRx = S.muri.tradizionale;
  const usaMuri = $("cop-muri").checked;
  let nord = -Infinity, sud = Infinity, est = -Infinity, ovest = Infinity;
  for (const r of ris) {
    const c = Math.cos((r.nodo.lat * Math.PI) / 180);
    nord = Math.max(nord, r.nodo.lat + R / 110540); sud = Math.min(sud, r.nodo.lat - R / 110540);
    est = Math.max(est, r.nodo.lon + R / (111320 * c)); ovest = Math.min(ovest, r.nodo.lon - R / (111320 * c));
  }
  const latC = (nord + sud) / 2;
  const larghezzaM = (est - ovest) * 111320 * Math.cos((latC * Math.PI) / 180), altezzaM = (nord - sud) * 110540;
  let pix = passo;
  while (Math.max(larghezzaM, altezzaM) / pix > 2200) pix *= 1.25;
  const W = Math.ceil(larghezzaM / pix), H = Math.ceil(altezzaM / pix);
  const cv = document.createElement("canvas");
  cv.width = W; cv.height = H;
  const g = cv.getContext("2d");
  const img = g.createImageData(W, H);
  const margini = new Float32Array(W * H).fill(-Infinity);
  const yN = Terreno.yDaLat(nord, 20), yS = Terreno.yDaLat(sud, 20);
  const bilancioBase = ris.map((r) => {
    const t = parametriNodo(r.nodo);
    return t.ptx + t.guadagno - t.perdite + rx.guadagno - rx.perdite - Radio.rumoreDbm(pr[ch], S.imp) - Radio.SNR_MIN[pr[ch].sf];
  });
  const cosLat = ris.map((r) => Math.cos((r.nodo.lat * Math.PI) / 180));
  let coperti = 0, dentroCerchio = 0;
  for (let py = 0; py < H; py++) {
    const lat = Terreno.latDaY(yN + ((yS - yN) * (py + 0.5)) / H, 20);
    for (let px = 0; px < W; px++) {
      const lon = ovest + ((est - ovest) * (px + 0.5)) / W;
      let best = -Infinity;
      for (let k = 0; k < ris.length; k++) {
        const r = ris[k];
        const dy = (lat - r.nodo.lat) * 110540, dx = (lon - r.nodo.lon) * 111320 * cosLat[k];
        const dist = Math.hypot(dx, dy);
        if (dist > R) continue;
        let th = Math.atan2(dx, dy);
        if (th < 0) th += 2 * Math.PI;
        const a = Math.round((th / (2 * Math.PI)) * r.nRadiali) % r.nRadiali;
        const s = Math.min(r.nCampioni - 1, Math.max(0, Math.round(dist / r.passo) - 1));
        const i = a * r.nCampioni + s;
        let m = bilancioBase[k] - r.L[i];
        if (rx.sempreDentro || (!rx.maiMuri && usaMuri && r.dentro[i])) m -= muriRx;
        if (m > best) best = m;
      }
      const j = py * W + px;
      margini[j] = best;
      if (best === -Infinity) continue;
      dentroCerchio++;
      if (best >= 0) coperti++;
      for (const [soglia, col] of COLORI) {
        if (best >= soglia) {
          img.data[j * 4] = col[0]; img.data[j * 4 + 1] = col[1]; img.data[j * 4 + 2] = col[2]; img.data[j * 4 + 3] = 255;
          break;
        }
      }
    }
  }
  g.putImageData(img, 0, 0);
  if (coperturaStrato) coperturaStrato.remove();
  coperturaStrato = L.imageOverlay(cv.toDataURL(), [[sud, ovest], [nord, est]], { opacity: +$("cop-opacita").value, interactive: false, pane: "copertura" })
    .addTo(map);
  coperturaGriglia.vista = { margini, W, H, nord, sud, est, ovest, yN, yS };
  const areaPix = (larghezzaM / W) * (altezzaM / H) / 1e6;
  const sec = $("cop-esito").dataset.secondi;
  $("cop-esito").textContent = `Coperto (margine ≥ 0 dB): ${fmt(coperti * areaPix, 1)} km² su ${fmt(dentroCerchio * areaPix, 1)} km² ` +
    `(${fmt((100 * coperti) / Math.max(1, dentroCerchio), 0)}%) · canale ${ch === "msg" ? "messaggi" : "voce"}, ${pr.nome}` +
    (sec ? ` · calcolata in ${sec} s` : "") + ". Passa sopra la mappa per leggere il margine.";
}

map.on("mousemove", (e) => {
  const v = coperturaGriglia && coperturaGriglia.vista;
  if (!v || caricamenti) return;
  const { lat, lng } = e.latlng;
  if (lat > v.nord || lat < v.sud || lng < v.ovest || lng > v.est) return messaggio("");
  const py = Math.floor(((Terreno.yDaLat(lat, 20) - v.yN) / (v.yS - v.yN)) * v.H);
  const px = Math.floor(((lng - v.ovest) / (v.est - v.ovest)) * v.W);
  const m = v.margini[py * v.W + px];
  if (!Number.isFinite(m)) return messaggio("");
  messaggio(`Margine ${fmt(m)} dB · probabilità di arrivo ${fmt(Radio.phi(m / S.imp.sigma) * 100, 0)}%`);
});

// ------------------------------------------------------------------ impostazioni

function formImpostazioni() {
  const f = $("form-impostazioni");
  f.innerHTML = "";
  const imp = (k) => (v) => {
    S.imp[k] = v;
    terr.edificioIgnotoM = S.imp.edificioIgnotoM;
    terr.chiomaM = S.imp.chiomaM;
    impostazioniCambiate(["k", "chiomaM", "edificioIgnotoM", "nonMappatiM"].includes(k));
  };
  f.appendChild(campoNumero("Incertezza σ", S.imp.sigma, 1, 15, 0.5, imp("sigma"), "dB (misure: 9–12)"));
  f.appendChild(campoNumero("Ostacoli non in mappa", S.imp.nonMappatiM, 0, 10, 0.5, imp("nonMappatiM"), "m, fra le case (0 = no)"));
  f.appendChild(campoNumero("Rumore extra", S.imp.rumoreExtra, 0, 30, 1, imp("rumoreExtra"), "dB (città: 3–6)"));
  f.appendChild(campoNumero("Rumore ricevitore", S.imp.cifraRumore, 2, 12, 0.5, imp("cifraRumore"), "dB"));
  f.appendChild(campoNumero("Fattore k", +S.imp.k.toFixed(3), 0.5, 4, 0.01, imp("k"), "(4/3 = 1,333)"));
  f.appendChild(campoNumero("Chioma alberi", S.imp.chiomaM, 3, 40, 1, imp("chiomaM"), "m"));
  f.appendChild(campoNumero("Edifici senza altezza", S.imp.edificioIgnotoM, 3, 30, 1, imp("edificioIgnotoM"), "m"));
  f.appendChild(campoNumero("Rete: distanza max", S.imp.distMaxKm, 1, 200, 1, imp("distMaxKm"), "km"));
  const h = document.createElement("h3");
  h.textContent = "Muri (dB)";
  f.appendChild(h);
  for (const [k, v] of Object.entries(MURI)) {
    f.appendChild(campoNumero(v.nome.split(" (")[0], S.muri[k], 0, 50, 1, (x) => { S.muri[k] = x; impostazioniCambiate(false); }, "dB"));
  }
}

function impostazioniCambiate(geometria) {
  if (geometria) {
    cachePercorsi.clear();
    rete = new Map();
    coperture.clear();
    gruppoRete.clearLayers();
  }
  $("cop-muri-db").textContent = S.muri.tradizionale;
  salva();
  if (collegamentoAttuale) apriCollegamento(collegamentoAttuale.a, collegamentoAttuale.b);
  if (geometria) {
    if (coperturaGriglia) togliCopertura();
  } else {
    disegnaRete();
    disegnaCopertura();
  }
  formProprieta();
}

async function statoCache() {
  try {
    const s = await (await fetch("/api/stato")).json();
    $("stato-cache").textContent = s.errori.length ? "Ultimi errori del server: " + s.errori.map((e) => e.msg).join(" · ") : "";
  } catch (e) {
    $("stato-cache").textContent = "Il server non risponde: è acceso?";
  }
}

// ------------------------------------------------------------------ schede e comandi

function mostraScheda(nome) {
  document.querySelectorAll("#schede button").forEach((b) => b.classList.toggle("attiva", b.dataset.scheda === nome));
  document.querySelectorAll(".scheda").forEach((s) => s.classList.toggle("attiva", s.id === "s-" + nome));
  if (nome === "impostazioni") statoCache();
}

function scaricaFile(nome, testo) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([testo], { type: "application/json" }));
  a.download = nome;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

const ESEMPIO = {
  versione: 1, profilo: "sf7", fonte: "tinitaly", mappa: "topo",
  vista: { lat: 43.316, lon: 11.334, zoom: 14 },
  nodi: [
    { tipo: "esterno", nome: "Esterno 1 · Fortezza", numero: 1, lat: 43.32335, lon: 11.32370, altezza: 10 },
    { tipo: "casa", nome: "Casa 2 · Via Pantaneto", numero: 2, lat: 43.31735, lon: 11.33860, piano: 2 },
    { tipo: "esterno", nome: "Esterno 3 · Porta Romana", numero: 3, lat: 43.30870, lon: 11.34330, altezza: 8 },
    { tipo: "tdeck", nome: "T-Deck 1001 · Il Campo", numero: 1001, lat: 43.31845, lon: 11.33170 },
    { tipo: "tdeck", nome: "T-Deck 1002 · Stazione", numero: 1002, lat: 43.33010, lon: 11.31890 },
    { tipo: "tdeck", nome: "T-Deck 1003 · Ospedale", numero: 1003, lat: 43.29920, lon: 11.31330, posizione: "tasca", corpo: 10 },
  ],
};

function collega() {
  const sel = $("profilo");
  sel.innerHTML = Object.entries(Radio.PROFILI).map(([k, p]) => `<option value="${k}">${p.nome}</option>`).join("");
  sel.value = S.profilo;
  sel.onchange = () => { S.profilo = sel.value; impostazioniCambiate(false); if (window.Simulazione) Simulazione.reteCambiata(); };
  $("fonte").value = S.fonte;
  $("fonte").onchange = (e) => {
    S.fonte = e.target.value;
    terr.fonte = S.fonte;
    terr.dem.clear();
    impostazioniCambiate(true);
  };
  $("mappa-base").value = S.mappa;
  $("mappa-base").onchange = (e) => { S.mappa = e.target.value; scegliMappa(S.mappa); salva(); };
  document.querySelectorAll("#schede button").forEach((b) => (b.onclick = () => mostraScheda(b.dataset.scheda)));
  $("rete-calcola").onclick = () => aggiornaRete(true);
  $("rete-tutti").onchange = disegnaRete;
  $("per-trova").onclick = cercaPercorso;
  $("cop-calcola").onclick = calcolaCopertura;
  $("cop-togli").onclick = togliCopertura;
  $("cop-raggio").oninput = (e) => ($("cop-raggio-v").textContent = e.target.value + " km");
  for (const id of ["cop-canale", "cop-muri"]) $(id).onchange = disegnaCopertura;
  $("cop-rx").onchange = () => { if (coperturaGriglia) calcolaCopertura(); };
  $("cop-opacita").oninput = (e) => coperturaStrato && coperturaStrato.setOpacity(+e.target.value);
  $("cop-muri-db").textContent = S.muri.tradizionale;
  $("btn-salva").onclick = () => scaricaFile("scenario-pippanet.json", JSON.stringify(scenario(), null, 2));
  $("btn-apri").onclick = () => $("file-apri").click();
  $("file-apri").onchange = async (e) => {
    const f = e.target.files[0];
    if (!f) return;
    try { caricaScenario(JSON.parse(await f.text())); messaggio("Scenario aperto.", 2500); }
    catch (err) { messaggio("File non valido: " + err.message, 5000); }
    e.target.value = "";
  };
  $("btn-nuovo").onclick = () => { if (!S.nodi.length || confirm("Togliere tutti i dispositivi?")) caricaScenario({ ...scenario(), nodi: [] }); };
  $("btn-esempio").onclick = () => caricaScenario(ESEMPIO);
}

// ------------------------------------------------------------------ avvio

function avvia() {
  let sc = null;
  try { sc = JSON.parse(localStorage.getItem(CHIAVE_SALVATAGGIO) || "null"); } catch (e) { sc = null; }
  costruisciTavolozza();
  if (sc) {
    S.profilo = sc.profilo || S.profilo;
    S.fonte = sc.fonte || S.fonte;
    S.mappa = sc.mappa || S.mappa;
  }
  terr.fonte = S.fonte;
  scegliMappa(S.mappa);
  collega();
  if (sc) caricaScenario(sc);
  else aggiornaTutto();
  formImpostazioni();
  $("profilo").value = S.profilo;
  $("fonte").value = S.fonte;
  $("mappa-base").value = S.mappa;
}

window.Pianificatore = {
  S, nodo, map, profiloRadio, altezzaAntenna, parametriNodo, aggiornaRete, infoCollegamento, messaggio, escape, fmt, TIPI,
  conCaricamento,
};
avvia();
