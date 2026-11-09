// Download da internet con nome del programma, tempo massimo e avanzamento.
"use strict";

const AGENTE = "PippaphonePianificatore/0.1 (strumento personale di pianificazione radio)";

async function scarica(url, avanzamento, opzioni = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), opzioni.timeoutMs || 15 * 60 * 1000);
  try {
    const r = await fetch(url, {
      method: opzioni.metodo || "GET",
      body: opzioni.corpo,
      headers: { "User-Agent": AGENTE, ...(opzioni.intestazioni || {}) },
      signal: ctrl.signal,
    });
    if (!r.ok) {
      const e = new Error(`${url} -> HTTP ${r.status}`);
      e.status = r.status;
      throw e;
    }
    const pezzi = [];
    let n = 0;
    for await (const p of r.body) {
      pezzi.push(p);
      n += p.length;
      if (avanzamento) avanzamento(n);
    }
    return Buffer.concat(pezzi.map((p) => Buffer.from(p)));
  } finally {
    clearTimeout(timer);
  }
}

module.exports = { scarica, AGENTE };
