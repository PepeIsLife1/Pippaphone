// Conversioni di coordinate: WGS84 <-> UTM 32N (serie di Krüger) e tasselli Web Mercator.
"use strict";

const A_WGS = 6378137.0;
const F_WGS = 1 / 298.257223563;
const K0 = 0.9996;
const LON0 = (9 * Math.PI) / 180; // meridiano centrale del fuso 32
const N = F_WGS / (2 - F_WGS);
const AA = (A_WGS / (1 + N)) * (1 + (N * N) / 4 + (N ** 4) / 64);
const ALFA = [
  N / 2 - (2 * N * N) / 3 + (5 * N ** 3) / 16 + (41 * N ** 4) / 180,
  (13 * N * N) / 48 - (3 * N ** 3) / 5 + (557 * N ** 4) / 1440,
  (61 * N ** 3) / 240 - (103 * N ** 4) / 140,
  (49561 * N ** 4) / 161280,
];
const C2N = (2 * Math.sqrt(N)) / (1 + N);

// Gradi -> UTM 32N (metri).
function utm32(latDeg, lonDeg) {
  const fi = (latDeg * Math.PI) / 180;
  const dl = (lonDeg * Math.PI) / 180 - LON0;
  const s = Math.sin(fi);
  const t = Math.sinh(Math.atanh(s) - C2N * Math.atanh(C2N * s));
  const xi1 = Math.atan2(t, Math.cos(dl));
  const eta1 = Math.atanh(Math.sin(dl) / Math.sqrt(1 + t * t));
  let xi = xi1, eta = eta1;
  for (let j = 1; j <= 4; j++) {
    xi += ALFA[j - 1] * Math.sin(2 * j * xi1) * Math.cosh(2 * j * eta1);
    eta += ALFA[j - 1] * Math.cos(2 * j * xi1) * Math.sinh(2 * j * eta1);
  }
  return { e: 500000 + K0 * AA * eta, n: K0 * AA * xi };
}

// Tasselli Web Mercator (256 px).
function lonDaX(x, z) { return (x / 2 ** z) * 360 - 180; }
function latDaY(y, z) {
  const m = Math.PI * (1 - (2 * y) / 2 ** z);
  return (Math.atan(Math.sinh(m)) * 180) / Math.PI;
}
function xDaLon(lon, z) { return ((lon + 180) / 360) * 2 ** z; }
function yDaLat(lat, z) {
  const r = (lat * Math.PI) / 180;
  return ((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * 2 ** z;
}
// Riquadro di un tassello: {s, w, n, e} in gradi.
function bboxTassello(z, x, y) {
  return { w: lonDaX(x, z), e: lonDaX(x + 1, z), n: latDaY(y, z), s: latDaY(y + 1, z) };
}

module.exports = { utm32, lonDaX, latDaY, xDaLon, yDaLat, bboxTassello };
