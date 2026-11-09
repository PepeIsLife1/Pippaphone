// Worker della copertura: per un gruppo di raggi attorno al trasmettitore calcola la perdita di
// percorso in ogni campione (dB, senza muri del ricevitore) e se il campione cade in un edificio.
"use strict";
importScripts("radio.js", "terreno.js");

const terr = new Terreno.Territorio("");

self.onmessage = async (ev) => {
  const j = ev.data;
  try {
    if (terr.fonte !== j.fonte) { terr.dem.clear(); terr.fonte = j.fonte; }
    terr.edificioIgnotoM = j.edificioIgnotoM;
    terr.chiomaM = j.chiomaM;
    const n = j.nCampioni;
    const cosLat = Math.cos((j.tx.lat * Math.PI) / 180);
    const nr = j.a1 - j.a0;
    const lat = new Float64Array(nr * (n + 1)), lon = new Float64Array(nr * (n + 1));
    const punti = new Array(nr * (n + 1) * 2);
    for (let k = 0; k < nr; k++) {
      const th = ((j.a0 + k) / j.nRadiali) * 2 * Math.PI;
      const sn = Math.sin(th), cs = Math.cos(th);
      for (let s = 0; s <= n; s++) {
        const r = s * j.passo, i = k * (n + 1) + s;
        lat[i] = j.tx.lat + (r * cs) / 110540;
        lon[i] = j.tx.lon + (r * sn) / (111320 * cosLat);
        punti[2 * i] = lat[i];
        punti[2 * i + 1] = lon[i];
      }
    }
    await terr.carica(punti, j.zDem, (f, t) => self.postMessage({ id: j.id, carico: f / Math.max(1, t) }));

    const L = new Float32Array(nr * n), dentro = new Uint8Array(nr * n);
    const prof = { d: new Float64Array(n + 1), terra: new Float64Array(n + 1), ed: new Float64Array(n + 1),
      bosco: new Uint8Array(n + 1), n };
    const o = { hTx: j.tx.hAnt, hRx: j.rx.hAnt, fMHz: j.fMHz, k: j.k, chioma: j.chiomaM, saltaTx: 0, saltaRx: 0,
      nonMappati: j.nonMappati || 0, dentroTx: !!j.tx.dentro, dentroRx: !!j.rx.dentro, dipoloTx: !!j.tx.dipolo, dipoloRx: !!j.rx.dipolo };
    // edPunto = edificio nel punto esatto del campione (dice se il ricevitore e' dentro una casa);
    // prof.ed = edificio piu' alto in tutto il tratto attorno al campione (e' l'ostacolo: con un
    // passo di 25 m il punto da solo salterebbe meta' delle case).
    const edPunto = new Float64Array(n + 1);
    for (let k = 0; k < nr; k++) {
      const th = ((j.a0 + k) / j.nRadiali) * 2 * Math.PI;
      const dLat = Math.cos(th) / 110540, dLon = Math.sin(th) / (111320 * cosLat);
      for (let s = 0; s <= n; s++) {
        const i = k * (n + 1) + s;
        prof.d[s] = s * j.passo;
        prof.terra[s] = terr.quota(lat[i], lon[i], j.zDem);
        const ob = terr.ostacolo(lat[i], lon[i]);
        edPunto[s] = ob.h;
        prof.ed[s] = s === 0 ? ob.h : Math.max(ob.h, terr.edificioNelTratto(lat[i], lon[i], dLat, dLon, j.passo));
        prof.bosco[s] = ob.bosco;
      }
      const scratch = Radio.preparaScratch(prof, j.chiomaM);
      o.saltaTx = Math.max(0, Radio.campioniProprioEdificio(prof.ed, false) - 1);
      let fila = 0; // campioni di edificio consecutivi che finiscono qui
      for (let s = 1; s <= n; s++) {
        fila = prof.ed[s] > 0 ? fila + 1 : 0;
        const inCasa = edPunto[s] > 0;
        // Chi e' dentro un edificio non ha davanti il proprio: si saltano i campioni che gli appartengono.
        o.saltaRx = inCasa ? Math.max(0, fila - 1) : 0;
        // Un ricevitore in mezzo a un edificio e' dentro: per lui contano i muri, non gli ostacoli di strada.
        o.dentroRx = !!j.rx.dentro || inCasa;
        const p = Radio.perditaPercorso(prof, s, o, scratch);
        L[k * n + s - 1] = p.totale;
        dentro[k * n + s - 1] = inCasa ? 1 : 0;
      }
      if (k % 16 === 0) self.postMessage({ id: j.id, fatto: k / nr });
    }
    self.postMessage({ id: j.id, finito: true, L, dentro, a0: j.a0, a1: j.a1, errori: terr.errori.slice(0, 3) },
      [L.buffer, dentro.buffer]);
  } catch (e) {
    self.postMessage({ id: j.id, errore: String(e.message || e) });
  }
};
