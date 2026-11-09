// Modello radio del pianificatore: parametri LoRa, tempi in aria, bilancio del collegamento e
// perdita di percorso su un profilo (terreno + edifici + boschi).
// Usato sia dalla pagina sia dai worker della copertura.
(function (g) {
  "use strict";

  // Profili radio. CR 5 = 4/5. Frequenze e preamboli come in componenti/pippanet/include/pippanet/airtime.h;
  // "sf7" e' quello del firmware.
  const PROFILI = {
    sf7: {
      nome: "SF7 · firmware (62,5 / 125 kHz)",
      msg: { f: 869.525, bw: 62.5, sf: 7, cr: 5, pre: 16 },
      voce: { f: 869.85, bw: 125, sf: 7, cr: 5, pre: 12 },
    },
    sf6: {
      nome: "SF6 · prova (62,5 / 125 kHz)",
      msg: { f: 869.525, bw: 62.5, sf: 6, cr: 5, pre: 16 },
      voce: { f: 869.85, bw: 125, sf: 6, cr: 5, pre: 12 },
    },
    sf8: {
      nome: "SF8 · messaggi 62,5 kHz · voce 125 kHz",
      msg: { f: 869.525, bw: 62.5, sf: 8, cr: 5, pre: 16 },
      voce: { f: 869.85, bw: 125, sf: 8, cr: 5, pre: 12 },
    },
    sf10: {
      nome: "SF10 · firmware fino al 9/11/2026 (125 / 250 kHz)",
      msg: { f: 869.525, bw: 125, sf: 10, cr: 5, pre: 16 },
      voce: { f: 869.85, bw: 250, sf: 10, cr: 5, pre: 12 },
    },
  };

  // SNR minimo per demodulare (Semtech SX1262).
  const SNR_MIN = { 5: -2.5, 6: -5, 7: -7.5, 8: -10, 9: -12.5, 10: -15, 11: -17.5, 12: -20 };

  // Pacchetti tipici di PippaNet (intestazione 14 B, tag 16 B, testo a 7 bit, ora d'invio 4 B).
  const testo = (car) => 14 + 4 + Math.ceil((car * 7) / 8) + 16;
  const PACCHETTI = {
    conferma: 30,
    testo20: testo(20),
    testo80: testo(80),
    testo160: testo(160),
    vocale: 254, // pezzo di vocale: 216 B di Codec2 700C = 2,16 s di audio
    fineVocale: 40,
  };
  const AUDIO_PER_PEZZO_S = 2.16;

  // Come pn::airtimeMs: header esplicito, CRC, ottimizzazione per simboli > 16 ms. A SF5/SF6
  // l'SX126x aggiunge 6,25 simboli al preambolo invece di 4,25 e nel carico manca il +8
  // (datasheet 6.1.4, RadioLib SX126x::calculateTimeOnAir).
  function tempoInAriaMs(p, len) {
    const tsym = 2 ** p.sf / (p.bw * 1000);
    const de = tsym > 0.016 ? 1 : 0;
    const basso = p.sf <= 6;
    const num = 8 * len - 4 * p.sf + (basso ? 20 : 28) + 16;
    const den = 4 * (p.sf - 2 * de);
    const nPay = 8 + Math.max(Math.ceil(num / den) * p.cr, 0);
    return Math.ceil((p.pre + (basso ? 6.25 : 4.25) + nPay) * tsym * 1000);
  }

  function bitRate(p) {
    const tsym = 2 ** p.sf / (p.bw * 1000);
    const de = tsym > 0.016 ? 1 : 0;
    return ((p.sf - 2 * de) / tsym) * (4 / p.cr);
  }

  function rumoreDbm(p, imp) {
    return -174 + 10 * Math.log10(p.bw * 1000) + imp.cifraRumore + imp.rumoreExtra;
  }
  function sensibilitaDbm(p, imp) { return rumoreDbm(p, imp) + SNR_MIN[p.sf]; }

  // Vocale di `secondi` secondi: tempo per spedirlo (come Phone::voiceSendStep / onTransmitted).
  function tempoVocaleS(p, secondi, conRipetitori) {
    const pezzi = Math.ceil(secondi / AUDIO_PER_PEZZO_S);
    const T = tempoInAriaMs(p, PACCHETTI.vocale) / 1000, E = tempoInAriaMs(p, PACCHETTI.fineVocale) / 1000;
    return conRipetitori ? pezzi * (3 * T + 0.16 + 0.7) + 3 * E + 0.86 : pezzi * (T + 0.2) + E;
  }

  // ---- propagazione ----

  const R_TERRA = 6371000;
  // Un capo del percorso e' "fra le case" se almeno un campione su 12 dei 150 m vicini e' un edificio.
  const SOGLIA_COSTRUITO = 1 / 12;

  // Perdita di diffrazione di un singolo spigolo (ITU-R P.526).
  function J(v) {
    if (v <= -0.78) return 0;
    const a = v - 0.1;
    return 6.9 + 20 * Math.log10(Math.sqrt(a * a + 1) + a);
  }

  // Vegetazione attraversata (Weissberger), d in metri, f in MHz.
  function weissberger(d, fMHz) {
    if (d <= 0) return 0;
    const fg = Math.pow(fMHz / 1000, 0.284);
    return d <= 14 ? 0.45 * fg * d : 1.33 * fg * Math.pow(Math.min(d, 400), 0.588);
  }

  // Funzione di ripartizione normale.
  function phi(x) {
    const t = 1 / (1 + 0.2316419 * Math.abs(x));
    const d = 0.3989423 * Math.exp((-x * x) / 2);
    const p = d * t * (0.3193815 + t * (-0.3565638 + t * (1.781478 + t * (-1.821256 + t * 1.330274))));
    return x > 0 ? 1 - p : p;
  }

  // Deygout fino a 3 spigoli. G = superficie (terreno + ostacoli), d = distanze, D = lunghezza del
  // percorso intero (per la curvatura), [lo, hi] = campioni dove cercare gli spigoli.
  // I due spigoli secondari pesano T = 1 - exp(-J/6) dello spigolo principale (ITU-R P.452-14):
  // se il principale sfiora appena il raggio, i secondari non contano quasi nulla. Senza questo il
  // terreno piatto vicino a un'antenna bassa diventa una fila di spigoli da 6 dB l'uno.
  // `spigoli` (facoltativo) riceve il primo e l'ultimo spigolo trovati: servono per l'angolo con cui
  // il segnale parte e arriva.
  function deygout(G, d, a, b, ha, hb, D, k2R, lam, lo, hi, livello, spigoli) {
    const i0 = Math.max(a + 1, lo), i1 = Math.min(b - 1, hi);
    if (i1 < i0) return 0;
    const da = d[a], L = d[b] - da;
    if (L <= 0) return 0;
    let best = -1e9, bi = -1;
    for (let i = i0; i <= i1; i++) {
      const d1 = d[i] - da, d2 = d[b] - d[i];
      if (d1 <= 0 || d2 <= 0) continue;
      const h = G[i] + (d[i] * (D - d[i])) / k2R - (ha + ((hb - ha) * d1) / L);
      const v = h * Math.sqrt((2 * L) / (lam * d1 * d2));
      if (v > best) { best = v; bi = i; }
    }
    if (best <= -0.78) return 0;
    if (spigoli) {
      if (spigoli.primo < 0 || bi < spigoli.primo) spigoli.primo = bi;
      if (bi > spigoli.ultimo) spigoli.ultimo = bi;
    }
    const principale = J(best);
    if (livello !== 0) return principale;
    const hm = G[bi] + (d[bi] * (D - d[bi])) / k2R;
    const t = 1 - Math.exp(-principale / 6);
    return principale + t * (deygout(G, d, a, bi, ha, hm, D, k2R, lam, lo, hi, 1, spigoli) +
      deygout(G, d, bi, b, hm, hb, D, k2R, lam, lo, hi, 1, spigoli));
  }

  // Ostacoli che le mappe non hanno (alberi in strada, auto, muretti, insegne) attorno a un
  // dispositivo basso in mezzo alle case: uno schermo alto `alto` metri a 10, 20 e 40 m, in media.
  // Conta solo se il segnale arriva radente: se arriva dall'alto (da un tetto vicino o da
  // un'antenna alta) passa sopra. theta = angolo di arrivo sull'orizzonte (rad), h = antenna (m).
  function ostacoliNonMappati(h, theta, alto, lam) {
    if (!(alto > 0)) return 0;
    const tg = Math.tan(Math.max(-0.5, Math.min(1.4, theta)));
    let somma = 0;
    for (const w of [10, 20, 40]) {
      const v = (alto - (h + w * tg)) * Math.sqrt(2 / (lam * w));
      somma += v > -0.78 ? J(v) : 0;
    }
    return somma / 3;
  }

  // Antenna verticale (dipolo): quanto perde verso un angolo theta sopra o sotto l'orizzonte.
  function diagrammaDipolo(theta) {
    const c = Math.cos(theta);
    if (c < 0.02) return 30;
    const g = Math.abs(Math.cos((Math.PI / 2) * Math.sin(theta)) / c);
    return Math.min(30, -20 * Math.log10(Math.max(g, 1e-3)));
  }

  // Riflesso sul terreno (due raggi). Il piano di riflessione e' la retta che approssima il terreno
  // nella parte centrale del percorso; le altezze efficaci delle antenne si contano da quella retta
  // (come in Longley-Rice). Suolo medio (eps 15, 5 mS/m), polarizzazione verticale, rugosita' di
  // Ament. Conta solo oltre l'ultimo massimo (dove il secondo raggio toglie e non aggiunge): li' la
  // perdita cresce come 40 log d invece di 20 log d. Restituisce i dB in piu' dello spazio libero.
  function riflessoSuolo(prof, j, o, scratch) {
    const D = prof.d[j];
    if (j < 4 || D < 20) return 0;
    const S = scratch.somme;
    let a = 0, b = j;
    if (j >= 20) { a = Math.floor(0.1 * j); b = Math.ceil(0.9 * j); }
    const n = b - a + 1;
    const sx = S.x[b + 1] - S.x[a], sz = S.z[b + 1] - S.z[a];
    const sxx = S.xx[b + 1] - S.xx[a], sxz = S.xz[b + 1] - S.xz[a], szz = S.zz[b + 1] - S.zz[a];
    const den = n * sxx - sx * sx;
    const pend = den > 1e-9 ? (n * sxz - sx * sz) / den : 0;
    const q = (sz - pend * sx) / n;
    // Rugosita': scarto quadratico del terreno attorno alla retta.
    const res = Math.max(0, (szz - 2 * pend * sxz - 2 * q * sz + pend * pend * sxx + 2 * pend * q * sx + n * q * q) / n);
    const sigmaH = Math.sqrt(res) + (o.rugosita || 0);
    const h1 = o.hTx + Math.max(0, prof.terra[0] - q);
    const h2 = o.hRx + Math.max(0, prof.terra[j] - (q + pend * D));
    const lam = 299.792458 / o.fMHz;
    const dphi = (4 * Math.PI * h1 * h2) / (lam * D);
    if (dphi >= Math.PI) return 0;
    const psi = Math.atan((h1 + h2) / D), s = Math.sin(psi), c2 = Math.cos(psi) ** 2;
    // Coefficiente di Fresnel verticale con eps complessa: (e s - r) / (e s + r), r = sqrt(e - cos^2).
    const er = 15, ei = -60 * 0.005 * lam;
    const ar = er - c2, ai = ei, m = Math.hypot(ar, ai);
    let rr = Math.sqrt((m + ar) / 2), ri = Math.sqrt(Math.max(0, (m - ar) / 2)) * (ai < 0 ? -1 : 1);
    const nr = er * s - rr, ni = ei * s - ri, dr = er * s + rr, di = ei * s + ri;
    const dd = dr * dr + di * di;
    let gr = (nr * dr + ni * di) / dd, gi = (ni * dr - nr * di) / dd;
    const amen = Math.exp(-2 * ((2 * Math.PI * sigmaH * s) / lam) ** 2);
    gr *= amen; gi *= amen;
    // |1 + G e^{-j dphi}|^2
    const cr = Math.cos(dphi), ci = -Math.sin(dphi);
    const er2 = 1 + gr * cr - gi * ci, ei2 = gr * ci + gi * cr;
    const e2 = er2 * er2 + ei2 * ei2;
    return Math.max(0, -10 * Math.log10(Math.max(e2, 1e-6)));
  }

  // Quota della frazione di campioni con edifici entro 150 m da un capo del profilo.
  function densitaEdifici(prof, scratch, i0, i1) {
    const n = i1 - i0 + 1;
    return n > 0 ? (scratch.edFinoA[i1 + 1] - scratch.edFinoA[i0]) / n : 0;
  }

  // Perdita sul percorso dal campione 0 al campione j del profilo.
  // prof = { d, terra, ed (altezza edifici), bosco (0/1), n }
  // o = { hTx, hRx (m dal suolo), fMHz, k, chioma (m), saltaTx, saltaRx (campioni del proprio edificio),
  //       nonMappati (m, altezza degli ostacoli non in mappa vicino a chi e' basso; 0 = no),
  //       dentroTx, dentroRx (dentro un edificio: niente ostacoli non mappati, contano i muri),
  //       dipoloTx, dipoloRx (antenna verticale fissa: conta il diagramma in elevazione) }
  function perditaPercorso(prof, j, o, scratch) {
    const { d, terra, ed, bosco } = prof;
    const D = d[j];
    if (D < 1) return { totale: 0, spazioLibero: 0, diffrazione: 0, vegetazione: 0, profonditaBosco: 0, suolo: 0, nonMappati: 0, antenne: 0 };
    const lam = 299.792458 / o.fMHz;
    const k2R = 2 * o.k * R_TERRA;
    const G = scratch.G, Gv = scratch.Gv;
    const ha = terra[0] + o.hTx, hb = terra[j] + o.hRx;
    const lo = 1 + (o.saltaTx || 0), hi = j - 1 - (o.saltaRx || 0);
    const sl = 32.45 + 20 * Math.log10(o.fMHz) + 20 * Math.log10(D / 1000);
    const spigoli = { primo: -1, ultimo: -1 };
    const difOstacoli = deygout(G, d, 0, j, ha, hb, D, k2R, lam, lo, hi, 0, spigoli);
    // Il terreno toglie il piu' grosso fra la diffrazione e il riflesso: dove il percorso e' libero
    // conta il riflesso, dietro una collina conta la diffrazione.
    const suolo = riflessoSuolo(prof, j, o, scratch);
    const dif = Math.max(difOstacoli, suolo);
    // Angoli di partenza e di arrivo (verso il primo/ultimo spigolo, o verso l'altra antenna).
    const quotaSpigolo = (i) => G[i] + (d[i] * (D - d[i])) / k2R;
    const thA = spigoli.primo >= 0 ? Math.atan2(quotaSpigolo(spigoli.primo) - ha, d[spigoli.primo]) : Math.atan2(hb - ha, D);
    const thB = spigoli.ultimo >= 0 ? Math.atan2(quotaSpigolo(spigoli.ultimo) - hb, D - d[spigoli.ultimo]) : Math.atan2(ha - hb, D);
    let nonMappati = 0;
    if (o.nonMappati > 0) {
      const passo = j > 0 ? D / j : 1, m = Math.max(1, Math.round(150 / passo));
      if (!o.dentroTx && densitaEdifici(prof, scratch, 0, Math.min(j, m)) >= SOGLIA_COSTRUITO)
        nonMappati += ostacoliNonMappati(o.hTx, thA, o.nonMappati, lam);
      if (!o.dentroRx && densitaEdifici(prof, scratch, Math.max(0, j - m), j) >= SOGLIA_COSTRUITO)
        nonMappati += ostacoliNonMappati(o.hRx, thB, o.nonMappati, lam);
    }
    const antenne = (o.dipoloTx ? diagrammaDipolo(thA) : 0) + (o.dipoloRx ? diagrammaDipolo(thB) : 0);
    let veg = 0, prof_ = 0;
    if (scratch.boschiFinoA[j] > 0) {
      // Tratto del raggio diretto che passa dentro le chiome.
      for (let i = Math.max(1, lo); i <= Math.min(j - 1, hi); i++) {
        if (!bosco[i]) continue;
        const raggio = ha + ((hb - ha) * d[i]) / D - (d[i] * (D - d[i])) / k2R;
        const suolo = terra[i] + ed[i];
        if (raggio > suolo && raggio < terra[i] + o.chioma) prof_ += (d[i + 1] - d[i - 1]) / 2;
      }
      const difChiome = deygout(Gv, d, 0, j, ha, hb, D, k2R, lam, lo, hi, 0);
      veg = Math.max(0, Math.min(difChiome, difOstacoli + weissberger(prof_, o.fMHz)) - difOstacoli);
    }
    return { totale: sl + dif + veg + nonMappati + antenne, spazioLibero: sl, diffrazione: difOstacoli,
      suolo: dif - difOstacoli, vegetazione: veg, profonditaBosco: prof_, nonMappati, antenne };
  }

  // Prepara i vettori di superficie del profilo (una volta per profilo), e le somme progressive
  // del terreno per la retta del riflesso (cosi' ogni punto del raggio costa O(1)).
  function preparaScratch(prof, chioma) {
    const n = prof.d.length;
    const G = new Float64Array(n), Gv = new Float64Array(n), boschiFinoA = new Int32Array(n);
    const somme = { x: new Float64Array(n + 1), z: new Float64Array(n + 1), xx: new Float64Array(n + 1),
      xz: new Float64Array(n + 1), zz: new Float64Array(n + 1) };
    const edFinoA = new Int32Array(n + 1);
    let c = 0;
    for (let i = 0; i < n; i++) {
      G[i] = prof.terra[i] + prof.ed[i];
      Gv[i] = prof.ed[i] > 0 ? G[i] : prof.terra[i] + (prof.bosco[i] ? chioma : 0);
      if (prof.bosco[i]) c++;
      boschiFinoA[i] = c;
      edFinoA[i + 1] = edFinoA[i] + (prof.ed[i] > 0 ? 1 : 0);
      const x = prof.d[i], z = prof.terra[i];
      somme.x[i + 1] = somme.x[i] + x;
      somme.z[i + 1] = somme.z[i] + z;
      somme.xx[i + 1] = somme.xx[i] + x * x;
      somme.xz[i + 1] = somme.xz[i] + x * z;
      somme.zz[i + 1] = somme.zz[i] + z * z;
    }
    return { G, Gv, boschiFinoA, somme, edFinoA };
  }

  // Campioni iniziali e finali che cadono dentro l'edificio del dispositivo (non gli fanno ombra).
  function campioniProprioEdificio(ed, daFine) {
    const n = ed.length;
    let k = 0;
    if (!daFine) { while (k < n && ed[k] > 0) k++; }
    else { while (k < n && ed[n - 1 - k] > 0) k++; }
    return k;
  }

  // Bilancio del collegamento su un canale: restituisce RSSI, SNR, margine, probabilita'.
  function bilancio(perdita, tx, rx, p, imp) {
    const rssi = tx.ptx + tx.guadagno - tx.perdite + rx.guadagno - rx.perdite - perdita;
    const snr = rssi - rumoreDbm(p, imp);
    const margine = snr - SNR_MIN[p.sf];
    return { rssi, snr, margine, prob: phi(margine / imp.sigma) };
  }

  g.Radio = {
    PROFILI, SNR_MIN, PACCHETTI, AUDIO_PER_PEZZO_S,
    tempoInAriaMs, bitRate, rumoreDbm, sensibilitaDbm, tempoVocaleS,
    J, weissberger, phi, perditaPercorso, preparaScratch, campioniProprioEdificio, bilancio,
  };
})(typeof self !== "undefined" ? self : this);
