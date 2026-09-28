# Ripetitore: scheda proposta

Il firmware del ripetitore gira su due schede (si sceglie in `idf.py menuconfig` →
"Pippaphone ripetitore"):

1. **La PCB del telefono**, per provare la rete subito, senza hardware nuovo. Potenza massima
   22 dBm (circa 160 mW). Con la PCB rev. 1.1 si accende premendo PWR e si spegne tenendo
   premuto PWR per 3 secondi; da solo, l'USB non la accende. Per un ripetitore fisso, che deve
   ripartire da solo quando torna la corrente, chiudi il ponticello JP3 (sempre acceso).
2. **La scheda ripetitore proposta qui**: ESP32-S3 e modulo Ebyte **E22-900M30S** (SX1262 con
   amplificatore da 1 W). È una proposta: la PCB è ancora da disegnare.

## Schema a blocchi

```
 pannello solare 6 V 5-10 W
          │
     caricatore MPPT (CN3791) ──── 2× 18650 in parallelo (1S, ~6 Ah)
          │                                │
          │                       ┌────────┴────────┐
          │                 buck-boost 5 V      LDO 3,3 V
          │                  (TPS63020)        (AP2112K)
          │                       │                 │
          │                 E22-900M30S  ◄─SPI─►  ESP32-S3-WROOM-1 (N4)
          │                       │                 │
          │                   SMA → antenna    USB-C (console, firmware)
          │
     partitore sulla batteria → ADC (GPIO1)
```

## Componenti

| Parte | Esempio | Note |
|---|---|---|
| Microcontrollore | ESP32-S3-WROOM-1-N4 | 4 MB di flash bastano (firmware circa 370 kB) |
| Radio | Ebyte E22-900M30S | SX1262 + PA 1 W, TCXO; alimentazione 5 V per avere 30 dBm |
| Alimentazione radio | TI TPS63020 (buck-boost) | 5 V da 1 cella; picchi di circa 650 mA in trasmissione |
| Alimentazione logica | AP2112K-3.3 | 600 mA |
| Batteria | 2× 18650 in parallelo | con protezione (DW01 + FS8205 o celle protette) |
| Carica solare | CN3791 (MPPT) | pannello da 6 V |
| Antenna | 868 MHz omnidirezionale, 3–5 dBi | in alto e in verticale, cavo corto |
| Contenitore | scatola IP65 con pressacavo | antenna fuori, pannello sopra |

## Collegamenti (come nel firmware, `main/main.cpp`)

| E22-900M30S | ESP32-S3 |
|---|---|
| SCK | GPIO12 |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| NSS | GPIO10 |
| BUSY | GPIO9 |
| DIO1 | GPIO8 |
| NRST | GPIO7 |
| TXEN | GPIO6 |
| RXEN | GPIO5 |
| VCC | 5 V (TPS63020) |
| GND | GND |

| Altro | ESP32-S3 |
|---|---|
| LED di stato (con resistenza da 1 kΩ) | GPIO2 |
| Partitore batteria 100 k / 100 k | GPIO1 (ADC1), da aggiungere al firmware |
| USB D− / D+ | GPIO19 / GPIO20 (console e caricamento) |
| Tasto BOOT | GPIO0 a massa |

Nel modulo E22 il commutatore d'antenna è comandato da TXEN/RXEN (il firmware li gestisce da
solo) e il TCXO si alimenta a 1,8 V da DIO3.

## Consumi stimati

| Stato | Corrente dalla batteria |
|---|---|
| In ascolto (ESP32-S3 a 160 MHz + SX1262 in ricezione) | circa 45 mA |
| In trasmissione a 30 dBm | circa 800 mA, per 1–2 s a pacchetto |

In ascolto sono circa 1,1 Ah al giorno: con 6 Ah il ripetitore dura circa 4–5 giorni senza
sole. Un pannello da 5–10 W basta in Italia anche d'inverno, se non è in ombra.

Con il light sleep dell'ESP32-S3 (sveglia su DIO1 della radio) l'ascolto scende sotto i
15 mA. Si può aggiungere dopo averlo misurato sulla scheda vera.

## Dove metterli

- In alto: tetti, pali, colline. La portata a SF10 dipende quasi solo dalla vista libera tra le
  antenne. Tra due ripetitori in vista si possono coprire diversi km.
- Ogni ripetitore deve sentire almeno un altro ripetitore. Con la console (`strade`, `info`)
  si vede chi sente chi e con che segnale.
- Numerali a mano (`numero 1`, `numero 2`...) così sul telefono il nome del ripetitore dice
  dove si trova.
