// Dati del territorio per il calcolo: quote (tasselli Int16, 0,2 m) e ostacoli (edifici, boschi).
// Funziona sia nella pagina sia nei worker. I tasselli si chiedono al server locale.
(function (g) {
  "use strict";

  const Z_OST = 13, LATO_OST = 1024;
  const NESSUNO = 0, EDIFICIO_IGNOTO = 251, BOSCO = 252;

  function xDaLon(lon, z) { return ((lon + 180) / 360) * 2 ** z; }
  function yDaLat(lat, z) {
    const r = (lat * Math.PI) / 180;
    return ((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * 2 ** z;
  }
  function latDaY(y, z) { const m = Math.PI * (1 - (2 * y) / 2 ** z); return (Math.atan(Math.sinh(m)) * 180) / Math.PI; }
  function lonDaX(x, z) { return (x / 2 ** z) * 360 - 180; }

  class Territorio {
    constructor(base) {
      this.base = base || "";
      this.fonte = "tinitaly";
      this.dem = new Map(); // "z/x/y" -> Int16Array
      this.ost = new Map(); // "x/y" -> Uint8Array
      this.inArrivo = new Map();
      this.errori = [];
      this.edificioIgnotoM = 8;
      this.chiomaM = 15;
    }

    async _prendi(chiave, url, destinazione, conv) {
      if (destinazione.has(chiave)) return;
      if (this.inArrivo.has(url)) return this.inArrivo.get(url);
      const p = (async () => {
        for (let t = 0; t < 3; t++) {
          try {
            const r = await fetch(this.base + url);
            if (!r.ok) throw new Error(url + " -> " + r.status + " " + (await r.text()).slice(0, 200));
            destinazione.set(chiave, conv(await r.arrayBuffer()));
            return;
          } catch (e) {
            if (t === 2) { this.errori.push(String(e.message || e)); destinazione.set(chiave, null); }
            else await new Promise((ok) => setTimeout(ok, 1500));
          }
        }
      })();
      this.inArrivo.set(url, p);
      try { await p; } finally { this.inArrivo.delete(url); }
    }

    // Carica i tasselli che servono per una lista di punti [lat, lon, lat, lon, ...].
    async carica(punti, zDem, avanzamento) {
      const dem = new Set(), ost = new Set();
      for (let i = 0; i < punti.length; i += 2) {
        const lat = punti[i], lon = punti[i + 1];
        const px = Math.floor(xDaLon(lon, zDem) * 256 - 0.5), py = Math.floor(yDaLat(lat, zDem) * 256 - 0.5);
        const x0 = px >> 8, x1 = (px + 1) >> 8, y0 = py >> 8, y1 = (py + 1) >> 8;
        dem.add(x0 * 65536 + y0); dem.add(x1 * 65536 + y0); dem.add(x0 * 65536 + y1); dem.add(x1 * 65536 + y1);
        ost.add(Math.floor(xDaLon(lon, Z_OST)) * 65536 + Math.floor(yDaLat(lat, Z_OST)));
      }
      const lavori = [];
      for (const c of dem) {
        const k = zDem + "/" + Math.floor(c / 65536) + "/" + (c % 65536);
        if (!this.dem.has(k)) lavori.push(() => this._prendi(k, `/api/dem/${this.fonte}/${k}`, this.dem, (b) => new Int16Array(b)));
      }
      for (const c of ost) {
        const k = Math.floor(c / 65536) + "/" + (c % 65536);
        if (!this.ost.has(k)) lavori.push(() => this._prendi(k, `/api/ostacoli/${k}`, this.ost, (b) => new Uint8Array(b)));
      }
      let fatti = 0;
      const tot = lavori.length;
      const corri = async () => {
        while (lavori.length) {
          await lavori.shift()();
          fatti++;
          if (avanzamento) avanzamento(fatti, tot);
        }
      };
      await Promise.all(Array.from({ length: 6 }, corri));
    }

    _pixel(z, px, py) {
      const t = this.dem.get(z + "/" + (px >> 8) + "/" + (py >> 8));
      if (!t) return 0;
      return t[(py & 255) * 256 + (px & 255)] / 5;
    }

    // Quota bilineare in metri.
    quota(lat, lon, z) {
      const gx = xDaLon(lon, z) * 256 - 0.5, gy = yDaLat(lat, z) * 256 - 0.5;
      const px = Math.floor(gx), py = Math.floor(gy), u = gx - px, v = gy - py;
      if ((px & 255) < 255 && (py & 255) < 255) {
        const t = this.dem.get(z + "/" + (px >> 8) + "/" + (py >> 8));
        if (!t) return 0;
        const i = (py & 255) * 256 + (px & 255);
        return ((t[i] * (1 - u) + t[i + 1] * u) * (1 - v) + (t[i + 256] * (1 - u) + t[i + 257] * u) * v) / 5;
      }
      const a = this._pixel(z, px, py), b = this._pixel(z, px + 1, py);
      const c = this._pixel(z, px, py + 1), d = this._pixel(z, px + 1, py + 1);
      return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
    }

    // Ostacolo nel punto: {h: altezza edificio (0 se nessuno), bosco: 0/1}.
    ostacolo(lat, lon) {
      const gx = xDaLon(lon, Z_OST), gy = yDaLat(lat, Z_OST);
      const tx = Math.floor(gx), ty = Math.floor(gy);
      const t = this.ost.get(tx + "/" + ty);
      if (!t) return { h: 0, bosco: 0, ignoto: false };
      const c = t[Math.min(LATO_OST - 1, Math.floor((gy - ty) * LATO_OST)) * LATO_OST +
        Math.min(LATO_OST - 1, Math.floor((gx - tx) * LATO_OST))];
      if (c === NESSUNO) return { h: 0, bosco: 0, ignoto: false };
      if (c === BOSCO) return { h: 0, bosco: 1, ignoto: false };
      if (c === EDIFICIO_IGNOTO) return { h: this.edificioIgnotoM, bosco: 0, ignoto: true };
      return { h: c, bosco: 0, ignoto: false };
    }

    // Altezza dell'edificio nel punto (0 se non c'e'), senza creare oggetti.
    altezzaEdificio(lat, lon) {
      const gx = xDaLon(lon, Z_OST), gy = yDaLat(lat, Z_OST);
      const tx = Math.floor(gx), ty = Math.floor(gy);
      const t = this.ost.get(tx + "/" + ty);
      if (!t) return 0;
      const c = t[Math.min(LATO_OST - 1, Math.floor((gy - ty) * LATO_OST)) * LATO_OST +
        Math.min(LATO_OST - 1, Math.floor((gx - tx) * LATO_OST))];
      return c === NESSUNO || c === BOSCO ? 0 : c === EDIFICIO_IGNOTO ? this.edificioIgnotoM : c;
    }

    // Edificio piu' alto nel tratto lungo `passoM` centrato sul punto, nella direzione (dLat, dLon)
    // (gradi per metro). Un campione ogni 25 m da solo vedrebbe meta' delle case che il raggio
    // attraversa: qui si guarda ogni 4 m circa.
    edificioNelTratto(lat, lon, dLat, dLon, passoM) {
      const n = suddivisioni(passoM);
      let h = 0;
      for (let k = 0; k < n; k++) {
        const r = ((k + 0.5) / n - 0.5) * passoM;
        const v = this.altezzaEdificio(lat + r * dLat, lon + r * dLon);
        if (v > h) h = v;
      }
      return h;
    }

    // Profilo tra due punti: n+1 campioni. Ai due capi l'edificio e' quello del punto esatto (e'
    // quello su cui o in cui sta il dispositivo); in mezzo e' il piu' alto del tratto.
    profilo(a, b, n, zDem) {
      const prof = { d: new Float64Array(n + 1), terra: new Float64Array(n + 1), ed: new Float64Array(n + 1),
        bosco: new Uint8Array(n + 1), lat: new Float64Array(n + 1), lon: new Float64Array(n + 1), n };
      const D = distanzaM(a.lat, a.lon, b.lat, b.lon);
      const passo = D / n, fitto = suddivisioni(passo) > 1 && D > 0;
      const dLat = fitto ? (b.lat - a.lat) / D : 0, dLon = fitto ? (b.lon - a.lon) / D : 0;
      for (let i = 0; i <= n; i++) {
        const f = i / n;
        const lat = a.lat + (b.lat - a.lat) * f, lon = a.lon + (b.lon - a.lon) * f;
        prof.lat[i] = lat; prof.lon[i] = lon; prof.d[i] = D * f;
        prof.terra[i] = this.quota(lat, lon, zDem);
        const o = this.ostacolo(lat, lon);
        prof.ed[i] = fitto && i > 0 && i < n ? this.edificioNelTratto(lat, lon, dLat, dLon, passo) : o.h;
        prof.bosco[i] = o.bosco;
      }
      return prof;
    }
  }

  // In quanti punti guardare gli edifici dentro un passo del profilo (circa uno ogni 4 m).
  function suddivisioni(passoM) { return passoM > 5 ? Math.ceil(passoM / 4) : 1; }

  function distanzaM(lat1, lon1, lat2, lon2) {
    const r = Math.PI / 180;
    const dl = (lat2 - lat1) * r, dn = (lon2 - lon1) * r;
    const a = Math.sin(dl / 2) ** 2 + Math.cos(lat1 * r) * Math.cos(lat2 * r) * Math.sin(dn / 2) ** 2;
    return 2 * 6371000 * Math.asin(Math.min(1, Math.sqrt(a)));
  }

  // Punti lungo un segmento (per sapere quali tasselli caricare).
  function puntiSegmento(a, b, passoM) {
    const D = distanzaM(a.lat, a.lon, b.lat, b.lon);
    const n = Math.max(2, Math.ceil(D / passoM));
    const out = [];
    for (let i = 0; i <= n; i++) out.push(a.lat + ((b.lat - a.lat) * i) / n, a.lon + ((b.lon - a.lon) * i) / n);
    return out;
  }

  g.Terreno = { Territorio, distanzaM, puntiSegmento, xDaLon, yDaLat, latDaY, lonDaX, Z_OST, LATO_OST };
})(typeof self !== "undefined" ? self : this);
