// Simulazione PippaNet: il protocollo vero in WebAssembly (pippanet.js), con i collegamenti
// calcolati dalla mappa e i pacchetti animati.
"use strict";
(function () {
  const P = window.Pianificatore;
  const $ = (id) => document.getElementById(id);
  const RISCALDAMENTO_MS = 60000;

  let mod = null, api = null;
  let attiva = false, inPausa = false, avviata = false;
  let indici = new Map(), nodiSim = [], perIndirizzo = new Map();
  let ultimo = 0, resto = 0, ultimoStat = 0, timer = null;
  let sosAttivo = new Set();
  // Chiamata in diretta: l'ultima chiamata fatta, chi sta parlando e la conversazione automatica.
  let chiamata = null, copione = null;
  const strato = L.layerGroup().addTo(P.map);
  const animazioni = [];

  async function carica() {
    if (api) return;
    if (typeof CreaPippaNet !== "function") throw new Error("pippanet.js non trovato: compila con wasm/compila.sh");
    mod = await CreaPippaNet();
    const f = (n, r, a) => mod.cwrap(n, r, a);
    api = {
      nuova: f("sim_nuova", null, ["number"]),
      profilo: f("sim_profilo", null, ["number", "number", "number", "number", "number", "number"]),
      telefono: f("sim_telefono", "number", ["number"]),
      ripetitore: f("sim_ripetitore", "number", ["number"]),
      posizione: f("sim_posizione", null, ["number", "number", "number"]),
      collegamento: f("sim_collegamento", null, ["number", "number", "number", "number", "number", "number", "number"]),
      variabilita: f("sim_variabilita", null, ["number"]),
      scarto: f("sim_scarto", "number", ["number", "number"]),
      certificati: f("sim_certificati", null, []),
      avanza: f("sim_avanza", null, ["number"]),
      ora: f("sim_ora", "number", []),
      eventi: f("sim_eventi", "string", []),
      statistiche: f("sim_statistiche", "string", []),
      testo: f("sim_testo", "number", ["number", "number", "string"]),
      globale: f("sim_globale", "number", ["number", "string"]),
      vocale: f("sim_vocale", "number", ["number", "number", "number"]),
      sosGrezzo: f("sim_sos", null, ["number", "number", "number", "number"]),
      chiamataImp: f("sim_chiamata_imp", null, ["number"]),
      chiama: f("sim_chiama", "number", ["number", "number"]),
      parla: f("sim_parla", null, ["number", "number"]),
      riattacca: f("sim_riattacca", null, ["number"]),
      chiamate: f("sim_chiamate", "string", []),
    };
  }

  function nome(addr) {
    const n = perIndirizzo.get(addr);
    if (n) return n.nome;
    if (addr === 0xffff) return "tutti";
    return addr >= 0xf000 ? `ripetitore ${addr - 0xf000}` : String(addr);
  }

  function log(t, testo, classe) {
    const li = document.createElement("li");
    if (classe) li.className = classe;
    li.innerHTML = `<b>${P.fmt((t - RISCALDAMENTO_MS) / 1000, 1)} s</b>${testo}`;
    const ol = $("sim-log");
    ol.prepend(li);
    while (ol.children.length > 300) ol.lastChild.remove();
  }

  async function accendi() {
    const nodi = P.S.nodi;
    if (nodi.filter((n) => n.ruolo === "telefono").length < 1) return P.messaggio("Metti almeno un T-Deck sulla mappa.", 3000);
    $("sim-avvia").disabled = true;
    try {
      await P.aggiornaRete();
      await carica();
      ferma();
      api.nuova(Math.floor(Math.random() * 1e9));
      const pr = P.profiloRadio();
      api.profilo(pr.msg.f, pr.msg.bw, pr.msg.sf, pr.voce.f, pr.voce.bw, pr.voce.sf);
      const varia = $("sim-varia").checked;
      api.variabilita(varia ? P.S.imp.sigma : 0);
      indici = new Map();
      nodiSim = [];
      perIndirizzo = new Map();
      for (const n of nodi) {
        const i = n.ruolo === "telefono" ? api.telefono(n.numero) : api.ripetitore(n.numero);
        if (n.ruolo === "telefono") api.posizione(i, n.lat, n.lon);
        indici.set(n.id, i);
        nodiSim[i] = n;
        perIndirizzo.set(n.ruolo === "telefono" ? n.numero : 0xf000 + n.numero, n);
      }
      let collegamenti = 0;
      const fortuna = [];
      for (let x = 0; x < nodi.length; x++) for (let y = x + 1; y < nodi.length; y++) {
        const a = nodi[x], b = nodi[y];
        const info = P.infoCollegamento(a, b);
        if (!info) continue;
        const ia = indici.get(a.id), ib = indici.get(b.id);
        ["msg", "voce"].forEach((ch, c) => {
          const av = info.avanti[ch], ind = info.indietro[ch];
          api.collegamento(ia, ib, c, av.rssi, av.snr, av.prob, av.margine);
          api.collegamento(ib, ia, c, ind.rssi, ind.snr, ind.prob, ind.margine);
        });
        const s = api.scarto(ia, ib), m = Math.min(info.avanti.msg.margine, info.indietro.msg.margine);
        if (m + s > -3 || Math.max(info.avanti.msg.margine, info.indietro.msg.margine) + s > -3) collegamenti++;
        // Da raccontare: i collegamenti che la fortuna fa passare da "va" a "non va" o viceversa.
        if (varia && m > -15 && (m >= 0) !== (m + s >= 0)) fortuna.push(`${P.escape(a.nome)} ↔ ${P.escape(b.nome)} ${s > 0 ? "+" : ""}${P.fmt(s, 0)} dB`);
      }
      if ($("sim-cert").checked) api.certificati();
      api.avanza(RISCALDAMENTO_MS);
      api.eventi();
      sosAttivo = new Set();
      $("sim-sos").textContent = "SOS";
      chiamata = null;
      copione = null;
      bottoniParla();
      $("ch-stat").innerHTML = "";
      $("sim-log").innerHTML = "";
      log(RISCALDAMENTO_MS, `Rete accesa: ${nodi.length} dispositivi, ${collegamenti} collegamenti utilizzabili, ${pr.nome}. ` +
        "I primi 60 s (beacon e strade) sono già passati.");
      if (varia) log(RISCALDAMENTO_MS, fortuna.length
        ? `In questa prova la fortuna dei posti cambia l'esito di: ${fortuna.join(" · ")}. Riaccendi per pescare un'altra realtà.`
        : "In questa prova nessun collegamento cambia esito per la fortuna dei posti.");
      $("sim-pannello").hidden = false;
      $("sim-nota").textContent = "";
      attiva = true;
      avviata = true;
      inPausa = false;
      $("sim-pausa").textContent = "Pausa";
      ultimo = performance.now();
      timer = setInterval(passoSim, 40);
      requestAnimationFrame(giroAnimazione);
    } catch (e) {
      P.messaggio("Simulazione non avviata: " + e.message, 8000);
    } finally {
      $("sim-avvia").disabled = false;
    }
  }

  function ferma() {
    attiva = false;
    clearInterval(timer);
    strato.clearLayers();
    animazioni.length = 0;
  }

  // Il tempo simulato avanza con un timer (va avanti anche con la scheda in secondo piano);
  // l'animazione usa requestAnimationFrame solo per disegnare.
  function passoSim() {
    if (!attiva) return;
    const t = performance.now();
    const dt = Math.min(1000, t - ultimo);
    ultimo = t;
    if (!inPausa) {
      resto += dt * +$("sim-velocita").value;
      const passi = Math.floor(resto / 10) * 10;
      if (passi > 0) {
        resto -= passi;
        api.avanza(passi);
        gestisci(JSON.parse(api.eventi()));
        eseguiCopione();
      }
    }
    if (t - ultimoStat > 500) { ultimoStat = t; statistiche(); }
  }
  function giroAnimazione(t) {
    if (!attiva) return;
    anima(t);
    requestAnimationFrame(giroAnimazione);
  }

  const ESITI = {
    ok: { colore: "#16a34a", tratto: null, peso: 3 },
    collisione: { colore: "#dc2626", tratto: null, peso: 3 },
    perso: { colore: "#6b7280", tratto: "4 5", peso: 2 },
    occupato: { colore: "#f97316", tratto: "1 5", peso: 2 },
  };

  function gestisci(eventi) {
    const ora = performance.now(), v = +$("sim-velocita").value;
    while (animazioni.length > 300) animazioni.shift().strato.remove(); // scheda in secondo piano: niente accumuli
    for (const e of eventi) {
      if (e.e === "tx") {
        const n = nodiSim[e.n];
        const beacon = e.tipo === "beacon";
        const cerchio = L.circleMarker([n.lat, n.lon], {
          radius: 10, color: e.ch ? "#9333ea" : beacon ? "#94a3b8" : "#1d5fd1", weight: beacon ? 1 : 3, fill: false, interactive: false,
        }).addTo(strato);
        animazioni.push({ tipo: "onda", strato: cerchio, inizio: ora, durata: Math.max(250, e.aria / v), beacon });
        // La voce in diretta e' un pacchetto ogni 0,4 s: si anima ma non si scrive (si riassume nella tabella).
        if (!beacon && e.tipo !== "voce in diretta") {
          const chi = e.salti > 0 ? `${P.escape(n.nome)} ripete` : `${P.escape(n.nome)} trasmette`;
          const dest = e.dst === 0xffff ? "a tutti" : `per ${P.escape(nome(e.dst))}`;
          log(e.t, `${chi} <i>${e.tipo}</i> di ${P.escape(nome(e.src))} ${dest}` +
            (e.salti ? ` (salto ${e.salti + 1})` : "") + ` · ${e.byte} B, ${P.fmt(e.aria / 1000, 2)} s${e.ch ? " · canale voce" : ""}`);
        }
      } else if (e.e === "rx") {
        const a = nodiSim[e.daNodo], b = nodiSim[e.n];
        const s = ESITI[e.esito];
        if (!s) continue;
        const linea = L.polyline([[a.lat, a.lon], [b.lat, b.lon]], {
          color: s.colore, weight: s.peso, dashArray: s.tratto, opacity: 0.9, interactive: false,
        }).addTo(strato);
        animazioni.push({ tipo: "linea", strato: linea, inizio: ora, durata: 1200 });
        if (e.esito === "collisione") log(e.t, `${P.escape(b.nome)}: collisione, non sente ${P.escape(a.nome)}`, "no");
      } else if (e.e === "testo") {
        log(e.t, `📩 ${P.escape(nodiSim[e.n].nome)} riceve da ${P.escape(nome(e.da))}: “${P.escape(e.testo)}”`, "ok");
      } else if (e.e === "globale") {
        log(e.t, `📢 ${P.escape(nodiSim[e.n].nome)} riceve il messaggio a tutti di ${P.escape(nome(e.da))}`, "ok");
      } else if (e.e === "consegna") {
        const cl = e.stato === "consegnato" || e.stato === "letto" ? "ok" : e.stato === "non raggiungibile" ? "no"
          : e.stato === "riprova piu' tardi" ? "limite" : "";
        log(e.t, `${P.escape(nodiSim[e.n].nome)}: messaggio ${e.id} per ${P.escape(nome(e.a))} → <b>${e.stato}</b>`, cl);
      } else if (e.e === "vocaleInArrivo") {
        log(e.t, `🎙 ${P.escape(nodiSim[e.n].nome)}: vocale in arrivo da ${P.escape(nome(e.da))}`);
      } else if (e.e === "vocale") {
        // `mancanti` e' una maschera: un bit per pezzo perso.
        let persi = 0;
        for (let m = e.mancanti >>> 0; m; m >>>= 1) persi += m & 1;
        const pezzi = Math.ceil(e.ms / 2160);
        log(e.t, `🎙 ${P.escape(nodiSim[e.n].nome)} riceve il vocale di ${P.escape(nome(e.da))} (${P.fmt(e.ms / 1000, 0)} s` +
          (persi ? `, mancano ${persi} pezzi su ${pezzi}` : ", completo") + ")", persi ? "limite" : "ok");
      } else if (e.e === "statoVocale") {
        const cl = e.stato === "consegnato" ? "ok" : e.stato === "non raggiungibile" ? "no" : e.stato === "arrivato in parte" ? "limite" : "";
        log(e.t, `${P.escape(nodiSim[e.n].nome)}: vocale per ${P.escape(nome(e.a))} → <b>${e.stato}</b>`, cl);
      } else if (e.e === "sos") {
        log(e.t, `🆘 ${P.escape(nodiSim[e.n].nome)} riceve ${e.attivo ? "l'SOS" : "la fine dell'SOS"} di ${P.escape(nome(e.da))}`, e.attivo ? "no" : "");
      } else if (e.e === "sosVisto") {
        log(e.t, `🆘 ${P.escape(nodiSim[e.n].nome)}: ${P.escape(nome(e.da))} ha visto l'SOS`, "ok");
      } else if (e.e === "chiamata") {
        logChiamata(e);
      }
    }
  }

  function logChiamata(e) {
    const chi = P.escape(nodiSim[e.n].nome), con = P.escape(nome(e.con));
    const frasi = {
      chiama: [`📞 ${chi} chiama ${con}`, ""],
      squilla: [`📞 ${chi}: squilla, chiama ${con}`, ""],
      "sta squillando": [`📞 ${chi} sente che a ${con} sta squillando`, ""],
      "ha risposto": [`📞 ${con} ha risposto a ${chi}: si passa al canale voce`, "ok"],
      "sul canale voce": [`📞 ${chi} passa al canale voce per la chiamata (non sente più i messaggi)`, "limite"],
      "in chiamata": [`📞 ${chi} è in chiamata con ${con}`, "ok"],
      finita: [`📞 ${chi}: chiamata con ${con} finita`, ""],
      annullata: [`📞 ${chi}: chiamata a ${con} annullata`, ""],
      caduta: [`📞 ${chi}: chiamata con ${con} <b>caduta</b>`, "no"],
      "non raggiungibile": [`📞 ${chi}: ${con} <b>non raggiungibile</b>`, "no"],
      "nessuna risposta": [`📞 ${chi}: ${con} <b>non risponde</b>`, "no"],
      risponde: [`📞 ${chi} risponde a ${con}: passa al canale voce`, "ok"],
      rifiutata: [`📞 ${chi}: ${con} ha rifiutato la chiamata`, "no"],
      occupato: [`📞 ${chi}: ${con} è <b>occupato</b>`, "no"],
      persa: [`📞 ${chi}: chiamata di ${con} persa`, ""],
      "durata massima": [`📞 ${chi}: chiamata con ${con} chiusa alla durata massima (3 minuti)`, "limite"],
    };
    const [testo, classe] = frasi[e.stato] || [`📞 ${chi}: ${P.escape(e.stato)}`, ""];
    log(e.t, testo, classe);
    const finali = ["finita", "annullata", "caduta", "non raggiungibile", "nessuna risposta", "rifiutata", "occupato",
                    "persa", "durata massima"];
    if (chiamata && finali.includes(e.stato) &&
        (e.n === chiamata.iDa || e.n === chiamata.iA)) {
      chiamata.parla = { da: false, a: false };
      if (e.n === chiamata.iDa) copione = null;
      bottoniParla();
    }
  }

  function bottoniParla() {
    const p = chiamata ? chiamata.parla : { da: false, a: false };
    $("ch-parla-da").textContent = p.da ? "Basta Da" : "Parla Da";
    $("ch-parla-a").textContent = p.a ? "Basta A" : "Parla A";
  }

  function parla(lato, si) {
    if (!chiamata) return;
    chiamata.parla[lato] = si;
    api.parla(lato === "da" ? chiamata.iDa : chiamata.iA, si ? 1 : 0);
    bottoniParla();
  }

  // Conversazione di prova: parte quando la chiamata e' collegata, con il tempo della simulazione.
  function eseguiCopione() {
    if (!copione || !chiamata) return;
    const ora = api.ora();
    if (copione.inizio == null) {
      const c = JSON.parse(api.chiamate()).chiamate[chiamata.id];
      if (!c || c.esito === "squilla") return;  // non ancora collegata
      if (c.esito !== "in corso") { copione = null; return; }
      copione.inizio = ora;
    }
    while (copione && copione.passi.length && ora >= copione.inizio + copione.passi[0].t) copione.passi.shift().fn();
    if (copione && !copione.passi.length) copione = null;
  }

  function tabellaChiamate() {
    if (!chiamata) return;
    const s = JSON.parse(api.chiamate());
    if (!s.chiamate.length) return;
    const perso = (v) => (v.pezzi ? 100 * (1 - v.arrivati / v.pezzi) : null);
    const cella = (v) => {
      const p = perso(v);
      if (p == null) return "—";
      return `<span class="${p < 5 ? "ok" : p < 15 ? "limite" : "no"}">${P.fmt(p, 0)}%</span>`;
    };
    const ritardo = (v) => (v.arrivati ? `${P.fmt((s.ms + 40 + v.p95) / 1000, 1)} s` : "—");
    const esiti = { "in corso": "in corso", squilla: "squilla" };
    $("ch-stat").innerHTML = `<tr><th>Chiamata</th><th>Esito</th>
      <th title="voce persa da chi chiama a chi risponde">Perso →</th><th title="voce persa da chi risponde a chi chiama">Perso ←</th>
      <th title="pezzi persi di fila, nel verso peggiore">Buco</th>
      <th title="dalla bocca all'orecchio: voce di un pacchetto + 40 ms di Codec 2 + ritardo della rete nel 95% dei casi">Ritardo</th></tr>` +
      s.chiamate.slice(-4).map((c) => {
        const cl = c.esito === "in corso" || c.esito === "finita" ? "" : c.esito === "squilla" ? "" : "no";
        return `<tr><td>${P.escape(nodiSim[c.a].nome.split(" · ")[0])} → ${P.escape(nodiSim[c.b].nome.split(" · ")[0])}</td>
          <td class="${cl}">${P.escape(esiti[c.esito] || c.esito)}${c.attesa >= 0 ? ` <small>(${P.fmt(c.durata / 1000, 0)} s)</small>` : ""}</td>
          <td>${cella(c.versi[0])}</td><td>${cella(c.versi[1])}</td>
          <td>${P.fmt(Math.max(c.versi[0].buco, c.versi[1].buco) / 1000, 1)} s</td>
          <td>${ritardo(c.versi[0].arrivati ? c.versi[0] : c.versi[1])}</td></tr>`;
      }).join("") +
      `<tr><td colspan="6" class="nota">${s.byte} byte, ${P.fmt(s.aria / 1000, 2)} s in aria ogni ${P.fmt(s.ms / 1000, 2)} s di voce</td></tr>`;
  }

  function anima(t) {
    for (let i = animazioni.length - 1; i >= 0; i--) {
      const a = animazioni[i];
      const f = (t - a.inizio) / a.durata;
      if (f >= 1) { a.strato.remove(); animazioni.splice(i, 1); continue; }
      if (a.tipo === "onda") {
        a.strato.setRadius(10 + f * (a.beacon ? 18 : 34));
        a.strato.setStyle({ opacity: (1 - f) * (a.beacon ? 0.5 : 1) });
      } else {
        a.strato.setStyle({ opacity: 0.9 * (1 - f * f) });
      }
    }
  }

  function statistiche() {
    const s = JSON.parse(api.statistiche());
    const sec = (s.ora - RISCALDAMENTO_MS) / 1000;
    $("sim-orologio").textContent = `${P.fmt(sec, 1)} s` + (inPausa ? " · in pausa" : "");
    $("sim-stat").innerHTML = `<tr><th>Dispositivo</th><th title="pacchetti trasmessi">Tx</th><th title="tempo in aria">Aria</th>
      <th title="pacchetti ricevuti">Rx</th><th title="collisioni">Coll.</th><th title="persi per segnale debole">Persi</th>
      <th title="ripetuti / soppressi perché già ripetuti da altri / ritrasmessi dopo una perdita">Rip.</th></tr>` +
      s.nodi.map((x, i) => `<tr><td>${P.escape(nodiSim[i].nome.split(" · ")[0])}${x.canale ? " 🎙" : ""}</td><td>${x.tx}</td>
        <td>${P.fmt(x.ariaMs / 1000, 1)} s</td><td>${x.rxOk}</td>
        <td>${x.collisioni}</td><td>${x.persi}</td><td>${x.ripetuti != null ? `${x.ripetuti}/−${x.soppressi}/+${x.recuperati}` : "—"}</td></tr>`).join("");
    tabellaChiamate();
  }

  function telefonoScelto(id) {
    const n = P.nodo(+$(id).value);
    return n && indici.has(n.id) ? n : null;
  }

  function comando(fn) {
    if (!attiva) return P.messaggio("Accendi prima la rete.", 2500);
    const da = telefonoScelto("sim-da"), a = telefonoScelto("sim-a");
    if (!da) return P.messaggio("Scegli il telefono che manda.", 2500);
    fn(da, a, api.ora());
  }

  function collega() {
    $("sim-avvia").onclick = accendi;
    $("sim-pausa").onclick = () => { inPausa = !inPausa; $("sim-pausa").textContent = inPausa ? "Riprendi" : "Pausa"; };
    $("sim-msg").onclick = () => comando((da, a, t) => {
      if (!a || a === da) return P.messaggio("Scegli un destinatario diverso.", 2500);
      const id = api.testo(indici.get(da.id), a.numero, $("sim-testo").value || "ciao");
      log(t, `✉️ ${P.escape(da.nome)} scrive a ${P.escape(a.nome)} (messaggio ${id})`);
    });
    $("sim-voce").onclick = () => comando((da, a, t) => {
      if (!a || a === da) return P.messaggio("Scegli un destinatario diverso.", 2500);
      const id = api.vocale(indici.get(da.id), a.numero, 10);
      log(t, `🎙 ${P.escape(da.nome)} manda un vocale di 10 s a ${P.escape(a.nome)} (${id ? "id " + id : "non partito"})`);
    });
    $("sim-tutti").onclick = () => comando((da, a, t) => {
      api.globale(indici.get(da.id), $("sim-testo").value || "ciao a tutti");
      log(t, `📢 ${P.escape(da.nome)} scrive a tutti`);
    });
    $("sim-sos").onclick = () => comando((da, a, t) => {
      const attivo = !sosAttivo.has(da.id);
      const contatti = P.S.nodi.filter((n) => n.ruolo === "telefono" && n !== da).map((n) => n.numero);
      const p = mod._malloc(4 * Math.max(1, contatti.length));
      mod.HEAP32.set(contatti, p >> 2);
      api.sosGrezzo(indici.get(da.id), attivo ? 1 : 0, p, contatti.length);
      mod._free(p);
      attivo ? sosAttivo.add(da.id) : sosAttivo.delete(da.id);
      $("sim-sos").textContent = sosAttivo.size ? "Ferma SOS" : "SOS";
      log(t, `🆘 ${P.escape(da.nome)} ${attivo ? "lancia l'SOS ai contatti" : "ferma l'SOS"}`, attivo ? "no" : "");
    });
    $("ch-chiama").onclick = () => comando((da, a, t) => {
      if (!a || a === da) return P.messaggio("Scegli chi chiamare in “A”.", 2500);
      api.chiamataImp(4000);
      const iDa = indici.get(da.id), iA = indici.get(a.id);
      const id = api.chiama(iDa, a.numero);
      if (id < 0) return P.messaggio("Questo telefono è già in una chiamata.", 2500);
      chiamata = { id, iDa, iA, parla: { da: false, a: false } };
      bottoniParla();
      copione = null;
      if ($("ch-copione").checked) {
        const passi = [];
        let tt = 500;
        for (let giro = 0; giro < 4; giro++) {
          const lato = giro % 2 ? "a" : "da";
          passi.push({ t: tt, fn: () => parla(lato, true) }, { t: tt + 10000, fn: () => parla(lato, false) });
          tt += 11000;
        }
        passi.push({ t: tt, fn: () => api.riattacca(iDa) });
        copione = { inizio: null, passi };
      }
    });
    $("ch-parla-da").onclick = () => { if (chiamata) parla("da", !chiamata.parla.da); };
    $("ch-parla-a").onclick = () => { if (chiamata) parla("a", !chiamata.parla.a); };
    $("ch-riattacca").onclick = () => {
      if (!chiamata) return;
      copione = null;
      api.riattacca(chiamata.iDa);  // riattacca chi ha chiamato; l'altro lo sa dal pacchetto di fine
    };
  }

  window.Simulazione = {
    reteCambiata() {
      if (!avviata) return;
      ferma();
      avviata = false;
      $("sim-pannello").hidden = true;
      $("sim-nota").textContent = "La rete è cambiata: riaccendila per simulare con i dispositivi e i collegamenti nuovi.";
    },
  };
  collega();
})();
