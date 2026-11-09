# Ripetitore: schede

Il firmware del ripetitore gira su quattro schede; si sceglie in compilazione:

| Scheda | Per cosa | Potenza | Come si sceglie |
|---|---|---|---|
| **Heltec WiFi LoRa 32 V3** (o un clone) | ripetitori **in casa**, sempre alimentati dalla USB | 22 dBm (~160 mW) | `sdkconfig.defaults.heltec` |
| **LilyGO T3-S3 V1.2 con SX1262** | ripetitori **all'aperto** con pannello solare e batteria | 22 dBm (~160 mW) | `sdkconfig.defaults.t3s3` |
| PCB del telefono | provare la rete subito, senza hardware nuovo | 22 dBm | predefinita |
| ESP32-S3 + Ebyte E22-900M30S | ripetitori lontani (1 W): **proposta**, la PCB è da disegnare | ~30 dBm | `menuconfig` |

Heltec e T3-S3 hanno lo stesso chip radio del telefono (SX1262 con TCXO), uno schermo OLED
128×64 e un tasto. Sono le schede che usano anche Meshtastic e MeshCore: i pin vengono dagli
schemi dei produttori e coincidono con quelli di quei progetti.

## Compilare e caricare

Dalla cartella `ripetitore`, con ESP-IDF 5.5 attivo (su Windows, in PowerShell, le stesse righe):

```sh
# Heltec WiFi LoRa 32 V3
idf.py -B build-heltec -D SDKCONFIG=build-heltec/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.heltec" build
idf.py -B build-heltec -D SDKCONFIG=build-heltec/sdkconfig -p COM6 flash monitor

# LilyGO T3-S3
idf.py -B build-t3s3 -D SDKCONFIG=build-t3s3/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.t3s3" build
idf.py -B build-t3s3 -D SDKCONFIG=build-t3s3/sdkconfig -p COM7 flash monitor
```

Se si cambia un file `sdkconfig.defaults*` bisogna cancellare il `sdkconfig` nella cartella di
compilazione: ESP-IDF non riapplica i valori predefiniti a un sdkconfig che esiste già. Le
soglie della batteria e i secondi dello schermo si cambiano anche in `idf.py -B ... menuconfig`
→ "Pippaphone ripetitore".

Poi, dalla console (`monitor`), si dà a ogni ripetitore il suo numero: `numero 3` → R3.

**Prima di accenderli, avvita l'antenna.** Il ripetitore trasmette da solo (il primo beacon
parte entro 25 secondi): un SX1262 che trasmette senza antenna si può rovinare.

## Schermo e tasto (Heltec e T3-S3)

Lo schermo si accende all'avvio e quando si preme il tasto (**PRG** sulla Heltec, **BOOT** sulla
T3-S3), e si spegne da solo dopo 60 secondi: un OLED sempre acceso si consuma e la scritta resta
impressa. Con lo schermo acceso il tasto passa alla pagina dopo:

1. **stato**: numero del ripetitore, potenza (o "VOCE" durante un vocale), batteria, pacchetti
   ripetuti e ricevuti, quanto tempo fa è arrivato l'ultimo e con che segnale, vicini, da
   quanto è acceso;
2. **vicini diretti**: chi sente senza intermediari, prima i ripetitori, con l'SNR e da quanto
   non lo sente. È la pagina da guardare mentre si sceglie dove montarlo, senza il computer;
3. **radio**: frequenza, banda, SF, potenza, pacchetti trasmessi, tempo in aria, soppressi,
   recuperati, scartati, errori.

Dalla console: `schermo 120` (secondi prima di spegnersi), `schermo 0` (sempre acceso),
`schermo gira` (immagine capovolta, se la scheda è montata al contrario). Restano salvati.

Il LED lampeggia brevemente ogni 5 secondi; se lampeggia lento la radio non risponde.

## Heltec WiFi LoRa 32 V3 (in casa)

ESP32-S3FN8 (8 MB di flash), SX1262 con TCXO, OLED 0,96", convertitore USB-seriale CP2102.
Si alimenta dalla USB-C (5 V, basta un caricatore da telefono).

- **V3, non V2.** Molti annunci mettono insieme V2 e V3: la V2 ha un SX1276, che con questo
  firmware non funziona. La V3 ha scritto "V3" sulla scheda.
- La USB-C arriva al **CP2102**, non alla USB interna dell'ESP32-S3: per questo la console va
  sulla UART0 (`sdkconfig.defaults.heltec`). Su Windows compare come porta COM "Silicon Labs
  CP210x" (se manca il driver si scarica da Silicon Labs). Per caricare il firmware non serve
  premere niente.
- Il connettore della batteria (JST 1,25 mm) è facoltativo: una LiPo collegata lì si carica dalla
  USB e tiene acceso il ripetitore se manca la corrente. Il firmware non ne legge la tensione.
- Dentro casa la portata dipende da muri e finestre: meglio in alto, vicino a una finestra che
  guarda verso gli altri ripetitori, con l'antenna verticale.

| Collegamento | GPIO |
|---|---|
| SX1262 SCK / MOSI / MISO / NSS | 9 / 10 / 11 / 8 |
| SX1262 RESET / BUSY / DIO1 | 12 / 13 / 14 |
| SX1262 DIO2 (commutatore d'antenna), DIO3 (TCXO 1,8 V) | interni al modulo |
| OLED SDA / SCL / RESET | 17 / 18 / 21 |
| Vext (alimenta l'OLED, attivo basso) | 36 |
| LED bianco | 35 |
| Tasto PRG | 0 |
| Console (CP2102) | UART0, GPIO 43 / 44 |

## LilyGO T3-S3 V1.2 (all'aperto, solare)

ESP32-S3FH4R2 (4 MB di flash, 2 MB di PSRAM non usata), modulo SX1262 con TCXO, OLED 0,96",
caricabatterie TP4065 (circa 500 mA dalla USB), connettore della batteria PH 2,0 mm. La USB-C è
quella interna dell'ESP32-S3, come sulla PCB del telefono.

- La T3-S3 esiste anche con SX1276, SX1280 e LR1121: serve quella **con SX1262, 868 MHz**.
- Nel kit col pannello la batteria deve essere collegata al connettore della T3-S3: solo così
  il firmware ne legge la tensione (partitore 100k/100k su GPIO1) e la protegge. Se il kit
  alimenta la scheda dalla USB-C con un suo regolatore, la protezione è quella del kit.
- La CPU va a 80 MHz (bastano e consumano meno).

**Batteria scarica.** Ogni minuto il ripetitore misura la batteria. Se resta sotto **3,30 V** per
tre misure di fila spegne radio, schermo e LED e mette l'ESP32-S3 nel sonno profondo (radio nel
sonno anche lei: si stimano qualche decina di µA in tutto, da misurare). Ogni **15 minuti** si sveglia, misura e
riparte solo sopra **3,60 V**: così non si accende e spegne di continuo appena il pannello dà
un po' di corrente, e la cella non scende mai troppo. Premendo BOOT mentre dorme si sveglia e
mostra per qualche secondo la tensione e la soglia per ripartire. Se la USB è collegata a un
computer non va mai a dormire (si può usare la console anche a batteria scarica). Le tre
soglie si cambiano in `menuconfig`.

**Se non entra in programmazione** (per esempio perché sta dormendo): tieni premuto BOOT, premi
e rilascia RST, rilascia BOOT; poi `flash` e, alla fine, RST.

| Collegamento | GPIO |
|---|---|
| SX1262 SCK / MOSI / MISO / NSS | 5 / 6 / 3 / 7 |
| SX1262 RESET / BUSY / DIO1 | 8 / 34 / 33 |
| SX1262 DIO2 (commutatore d'antenna), DIO3 (TCXO 1,8 V) | interni al modulo |
| OLED SDA / SCL | 18 / 17 (il reset ce l'ha già sulla scheda) |
| LED verde | 37 |
| Tasto BOOT | 0 |
| Batteria (metà della tensione) | 1 |
| microSD CS / MOSI / MISO / SCK (non usata) | 13 / 11 / 2 / 14 |

**Consumi, da misurare sulla scheda vera.** In ascolto si stimano 35–45 mA (ESP32-S3 sveglio
più l'SX1262 in ricezione), cioè circa 1 Ah al giorno: con 5200 mAh sono 4–5 giorni senza sole.
Un pannello da 5 W rende in Italia, d'estate, diverse volte quel che serve; a dicembre e gennaio
è più al limite, ed è lì che entra in gioco il sonno per batteria scarica. Il passo successivo è
il light sleep dell'ESP32-S3 (sveglia sull'interruzione della radio): l'ascolto scenderebbe
sotto i 15 mA. Va provato e misurato sulla scheda vera.

## Scheda proposta: ESP32-S3 + Ebyte E22-900M30S (1 W)

Per i ripetitori che devono arrivare più lontano: modulo Ebyte **E22-900M30S** (SX1262 con
amplificatore da 1 W). È una proposta: la PCB è ancora da disegnare. Si sceglie in
`idf.py menuconfig` → "Pippaphone ripetitore" → "Scheda".

### Schema a blocchi

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

### Componenti

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

### Collegamenti (come nel firmware, `main/main.cpp`)

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

### Consumi stimati

| Stato | Corrente dalla batteria |
|---|---|
| In ascolto (ESP32-S3 a 160 MHz + SX1262 in ricezione) | circa 45 mA |
| In trasmissione a 30 dBm | circa 800 mA, per 1–2 s a pacchetto |

In ascolto sono circa 1,1 Ah al giorno: con 6 Ah il ripetitore dura circa 4–5 giorni senza
sole. Un pannello da 5–10 W basta in Italia anche d'inverno, se non è in ombra.

## PCB del telefono come ripetitore

Per provare la rete subito, senza hardware nuovo (scelta predefinita). Potenza massima 22 dBm
(circa 160 mW). Con la PCB rev. 1.1 si accende premendo PWR e si spegne tenendo premuto PWR per
3 secondi; da solo, l'USB non la accende. Per un ripetitore fisso, che deve ripartire da solo
quando torna la corrente, chiudi il ponticello JP3 (sempre acceso).

## Dove metterli

- In alto: tetti, pali, colline. La portata a SF7 dipende quasi solo dalla vista libera tra le
  antenne. Tra due ripetitori in vista si possono coprire diversi km; in città, dietro i
  palazzi, molto meno. Prima di montarli prova i punti con `strumenti/pianificatore`.
- Ogni ripetitore deve sentire almeno un altro ripetitore. Sulle schede con lo schermo lo dice
  la pagina "vicini diretti"; dalla console, `strade` e `info`.
- Numerali a mano (`numero 1`, `numero 2`...) così sul telefono il nome del ripetitore dice
  dove si trova.
