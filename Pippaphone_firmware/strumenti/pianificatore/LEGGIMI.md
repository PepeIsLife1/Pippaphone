# Pianificatore radio PippaNet

Un "Packet Tracer" per la rete PippaNet sulla mappa d'Italia.
- **Dispositivi:** metti sulla mappa T-Deck, ripetitori da casa (Heltec V3), ripetitori da esterno (T3-S3 solare) e, per prova, il ripetitore da 1 W (E22).
- **Calcolo:** dal terreno vero, dagli edifici e dai boschi ricava segnale, margine e probabilità di arrivo.
- **Simulazione:** fa girare il protocollo vero (`componenti/pippanet`) sulla rete che hai disegnato.

Gira tutto sul tuo PC (`localhost`). Serve solo Node.js, senza librerie da installare.

## Avvio

```bash
node strumenti/pianificatore/server.js
```

Poi apri http://localhost:8787. Su Windows basta il doppio clic su `avvia.bat`. Su Mac (e Linux) c'è
`avvia.sh`, che apre anche il browser:

```bash
sh strumenti/pianificatore/avvia.sh
```

Serve Node.js 18 o più nuovo (sul Mac: `brew install node`).

La prima volta in una zona nuova il server scarica i dati. Dopo restano in cache e la zona si apre subito, anche senza rete.

## Cosa fa

| Scheda | Cosa ti dice |
|---|---|
| **Collegamento** | Seleziona un dispositivo, poi Maiusc + clic su un altro. Vedi il profilo del terreno con edifici e boschi, la linea di vista e la zona di Fresnel. Ci sono le perdite voce per voce, e RSSI, SNR, margine e probabilità nei due versi e sui due canali. Sotto: bit rate e tempi in aria. |
| **Copertura** | La mappa colorata del margine attorno a un dispositivo, oppure di tutti i ripetitori insieme. Scegli chi riceve (T-Deck in mano o in tasca, ripetitore) e il canale (messaggi o voce). Passando col mouse leggi il margine. |
| **Rete** | Chi sente chi, con linee colorate e la voce tratteggiata se non passa. Per due telefoni trova il percorso migliore attraverso i ripetitori. Mostra la probabilità di consegna (anche con i 4 tentativi del telefono), i tempi di un messaggio e di un vocale e il bit rate fino in fondo. |
| **Simulazione** | Il protocollo PippaNet vero, compilato in WebAssembly. Mandi messaggi, vocali, messaggi a tutti e SOS, e vedi i pacchetti animati: verde ricevuto, rosso collisione, grigio perso, arancio occupato. Ci sono il registro e le statistiche per dispositivo. In più la **chiamata in diretta** del firmware, a turni (vedi sotto). |
| **Impostazioni** | Fading, rumore, fattore k, altezza delle chiome, altezza degli edifici senza dati, perdite dei muri. |

**Profilo radio** (in alto):
- **SF7**: quello del firmware, messaggi a 62,5 kHz e voce a 125 kHz.
- **SF6**: prova, stesse bande. È veloce quasi il doppio ma sente 2,5 dB in meno. Il firmware non lo usa: prima andrebbe corretto `pn::airtimeMs` (vedi sotto).
- **SF8**: solo per confronto.
- **SF10**: il firmware fino al 9/11/2026, solo per confronto.

Cambiare profilo cambia i calcoli e la simulazione, non il firmware.

## Dati e cache

| Dato | Fonte | Licenza |
|---|---|---|
| Terreno a 10 m | TINITALY/1.1, INGV. Riquadri da 50 km, ~80 MB l'uno, scaricati quando servono | CC BY 4.0, Tarquini et al. 2023, doi:10.13127/tinitaly/1.1 |
| Terreno fuori Italia / mare | AWS Terrain Tiles (Terrarium, ~30 m) | dati aperti |
| Edifici (con altezza) e boschi | OpenStreetMap, tramite i tasselli vettoriali di OpenFreeMap (schema OpenMapTiles) | ODbL |
| Mappe | OpenTopoMap, OpenStreetMap | CC-BY-SA, ODbL |

La cache sta in `%LOCALAPPDATA%\PippaphonePianificatore\cache` (sul Mac in `~/.cache/pippaphone-pianificatore`), fuori dal progetto: il progetto è in OneDrive e non deve sincronizzare gigabyte di terreno. Ogni riquadro TINITALY occupa 50 MB, tutta Italia circa 10 GB. Si può cancellare quando vuoi, oppure spostare con la variabile `CACHE_PIANIFICATORE`.

`dati/tinitaly_indice.json` è l'indice dei 193 riquadri TINITALY: nome, angolo UTM 32N, dimensioni, peso dello zip. È stato letto dalle intestazioni dei file sul sito INGV.

## Come calcola (e quanto ci si può fidare)

- **Perdita del percorso:**
  - spazio libero;
  - diffrazione di Deygout (fino a 3 spigoli) su terreno + edifici, con la curvatura terrestre (k = 4/3). Gli spigoli secondari pesano come in ITU-R P.452-14: se il principale sfiora appena il raggio quasi non contano;
  - riflesso del terreno (due raggi), dal piano che approssima il terreno, con le altezze efficaci delle antenne come in Longley-Rice. In pianura, oltre qualche centinaio di metri, la perdita cresce come 40 log d. Conta il più grosso fra riflesso e diffrazione;
  - vegetazione: Weissberger per il tratto di bosco attraversato, oppure diffrazione sopra le chiome se costa meno;
  - ostacoli non in mappa (alberi in strada, auto, muretti) attorno a chi è basso in mezzo alle case: uno schermo di 4 m a 10–40 m. Conta solo se il segnale arriva radente; se arriva dall'alto ci passa sopra;
  - antenne dei ripetitori: diagramma verticale a dipolo (conta solo vicino al ripetitore, dove il segnale arriva molto inclinato);
  - muri: ITU-R P.2109 a 869 MHz, mediana 14 dB per gli edifici tradizionali e 31 dB per quelli isolati moderni;
  - corpo e cavo.
- **Il proprio edificio di un dispositivo** non gli fa ombra: al suo posto contano i muri, se è segnato "dentro".
- **Margine** = SNR − SNR minimo dello SF (SF7: −7,5 dB, SF6: −5 dB). La probabilità è quella che il posto vero vada bene: incertezza normale di σ = 9 dB.
- **Tempi in aria:** stessa formula di `pn::airtimeMs`, più il caso SF5/SF6 dell'SX126x: 6,25 simboli in più di preambolo invece di 4,25, e niente +8 nel carico (come RadioLib). I tempi dei vocali seguono il passo di `Phone::voiceSendStep`.

### Quanto ci si può fidare: confronto con misure vere (9/11/2026)

- **Casi di riferimento:**
  - in pianura aperta il modello ora dà la "terra piana" entro 0,5 dB. Prima, a 5 km fra un telefono e un ripetitore a 8 m, era ottimista di 15–18 dB;
  - in città, con il ripetitore sopra i tetti, dà 129 dB a 1 km e 139 dB a 3 km. Le misure LoRa di Bonn danno 132 e 140 dB, quelle di Oulu 132 e 145 dB.
- **Misure LoRa di Funchal** (Azevedo e Mendonça, Mendeley Data, doi:10.17632/79vh7wnwhg.1, CC BY 4.0): 1203 punti a 3 m da terra, 3 gateway a 7, 20 e 27 m, 868 MHz. La stima conta anche i pacchetti persi (dati censurati):

  | | Prima | Ora |
  |---|---|---|
  | Errore medio (+ = il modello è pessimista) | 0,0 dB | +1,3 dB |
  | Scarto tipico | 12,5 dB | 12,5 dB |
  | Punti quasi in vista | −12,0 dB | −7,3 dB |
  | Punti ostruiti 10–30 dB | −5,3 dB | −1,8 dB |
  | Punti ostruiti 30–50 dB | −0,5 dB | +0,5 dB |
  | Ombre profonde (oltre 50 dB) | +5,5 dB | +5,8 dB |

  Nelle ombre profonde il modello resta prudente di circa 5 dB: lì il segnale arriva anche per riflessi che il modello non vede. A Funchal il terreno è a 30 m (fuori Italia non c'è TINITALY), quindi in Italia lo scarto dovrebbe essere un po' più piccolo.
- **Errore tipico ±9–12 dB su un singolo punto.** I colori dicono bene "passa / al limite / non passa", non il dBm esatto. Le fonti d'errore più grosse:
  - l'altezza degli edifici che OSM non conosce (si usa quella delle impostazioni, 8 m);
  - il guadagno vero delle antenne;
  - dove tieni il telefono.
- **Simulazione** ([wasm/simulatore.cpp](wasm/simulatore.cpp)): l'aria è come in `test/sim.h`, ma più vicina a un SX1262 vero:
  - RSSI, SNR e margine presi dalla mappa per ogni verso e canale;
  - "fortuna del posto": ogni coppia pesca uno scarto dalla mappa (σ delle impostazioni), uguale nei due versi. Così un collegamento va o non va come nella realtà, invece di andare un pacchetto sì e uno no. Si toglie con la spunta "posti più o meno fortunati";
  - fading di ogni pacchetto (Rice: K 10 dB fra ripetitori, 3 dB con un telefono in mano) e curva d'errore ripida;
  - aggancio come l'SX1262: il ricevitore si aggancia al primo preambolo. Un segnale più forte di 6 dB gli ruba il ricevitore solo prima degli ultimi 5 simboli del preambolo; dopo, rovina il primo e si perde anche lui;
  - CAD come `radio_link.cpp`: canale occupato, si riprova fra 100 e 700 ms;
  - passo di 1 ms.

  Prova da riga di comando: `simulatore [sf] [prove] [sigma] [dB in più]` (il main in fondo al file, istruzioni in `wasm/compila.sh`).

### Chiamate in diretta (9/11/2026)

Le chiamate sono quelle del firmware (`pn::Phone::call`, a turni, descritte in `PROTOCOLLO.md`).
Il simulatore aggiunge solo la persona: chi è chiamato risponde dopo 4 s, chi tiene premuto il PTT
dà un pacchetto di voce ogni 400 ms (480 con 2 o più ripetitori) come il microfono, e si misura
quale voce arriva e quando. Prima di scriverla nel firmware la chiamata era stata provata qui come
prototipo; la versione del firmware va come il prototipo o un po' meglio.

- **Nella pagina:** scheda Simulazione → "Chiamata in diretta". C'è una conversazione di prova (4
  turni da 10 s, poi riattacca), oppure si parla a mano con "Parla Da" e "Parla A". La tabella dà la
  voce persa nei due versi, il buco più lungo e il ritardo dalla bocca all'orecchio.
- **Da riga di comando:** `CHIAMATA=1 simulatore 7 120 9 [dB in più]`. Cinque reti: diretto, 1
  ripetitore, 2 in fila, 2 in fila con un terzo in parallelo, 3 in fila. A chiama C e parlano a
  turni per 4 × 10 s; intanto B scrive a C. `DUMP=rete.prova` stampa gli eventi di un'apertura,
  `CADUTE=1` (o `2`) le chiamate cadute. Per provare altri pacchetti oltre un ripetitore si compila
  con `-DPN_CALL_FRAMES_FAR=13` (o altro).

**Risultati** (SF7, σ 9 dB, 120 prove per rete). Chiamata "buona" se si perde meno del 5% della
voce nel verso peggiore, "si capisce" sotto il 15%, poi "a scatti". "Collegata" quando chi chiama
sente l'altro. Il ritardo è dalla bocca all'orecchio.

| Rete | Collegate | Buone / si capisce / a scatti | Voce persa | Ritardo |
|---|---|---|---|---|
| **Rete d'esempio** (~8 dB di margine sui messaggi, ~5 sulla voce) | | | | |
| diretto (ridondanza) | 105/120 | 80 / 14 / 11 | 4% | 0,6 s |
| 1 ripetitore (ridondanza) | 80/120 | 55 / 11 / 14 | 7% | 1,2 s |
| 2 in fila (480 ms) | 63/120 | 4 / 18 / 41 | 25% | 0,9 s |
| 2 in fila + 1 parallelo | 77/120 | 4 / 16 / 57 | 28% | 0,9 s |
| 3 in fila | 54/120 | 8 / 10 / 36 | 27% | 1,1 s |
| **Stessa rete con 10 dB in più** su ogni collegamento | | | | |
| diretto | 120/120 | 114 / 3 / 3 | 1% | 0,6 s |
| 1 ripetitore | 112/120 | 98 / 8 / 6 | 1,5% | 0,8 s |
| 2 in fila | 109/120 | 53 / 36 / 20 | 7% | 0,9 s |
| 2 in fila + 1 parallelo | 113/120 | 58 / 34 / 21 | 7% | 0,9 s |
| 3 in fila | 112/120 | 48 / 39 / 25 | 8% | 1,1 s |

- **Le perdite sono quasi tutte di segnale**, non del meccanismo. Una rete pensata per le chiamate
  vuole circa 10 dB di margine in più che per i messaggi, e il canale voce ha già 3 dB in meno del
  canale messaggi. Se l'ultimo tratto passa sul canale messaggi ma non su quello voce, la chiamata
  squilla ma non si collega (chi chiama non sente il "ti sento" e dopo 6 s rinuncia).
- **Ridondanza** (ogni pacchetto porta anche il precedente) solo con 0 o 1 ripetitore: con 2 o più
  i pacchetti più lunghi non ci stanno nel canale.
- **12 frame (480 ms) con 2 o più ripetitori.** Qui 10, 12 o 13 frame danno la stessa voce persa
  (±1%), perché la radio simulata passa dalla ricezione alla trasmissione all'istante: con 10
  frame 3 trasmissioni da 133 ms ci stanno in 400 ms per un millisecondo. Sulla radio vera ci
  vogliono alcuni millisecondi, e nei test di `test/` (passo di 10 ms) con 10 frame si perde un
  pacchetto su due attraverso tre ripetitori.
- **Dal prototipo:** Codec 2 1300 con 2 o più ripetitori fa chiamate tutte a scatti; parlando
  insieme si perde il 20% in diretto e il 60–85% con i ripetitori. Per questo 700C e turni.
- **Messaggi durante una chiamata:** chi è in chiamata e i ripetitori sul percorso non sentono
  il canale messaggi. Il telefono che scrive fa i suoi 4 tentativi in una quarantina di secondi,
  poi ("riprovo più tardi") un altro giro 3,5 minuti dopo, quando la chiamata è finita per forza
  (dura al massimo 3 minuti). Un messaggio mandato 5 s dopo l'inizio di una chiamata di 45 s
  arriva così 85–100 volte su 100 nella rete d'esempio e 99–100 con 10 dB in più, in mediana
  4 minuti e mezzo dopo; senza il giro più tardi arrivava fra 1 e 3 volte su 10.

## Ricompilare il simulatore

`web/pippanet.js` e `web/pippanet.wasm` sono già compilati. Rifalli quando cambia `componenti/pippanet`:

```bash
source /c/emsdk/emsdk_env.sh
```

```bash
sh strumenti/pianificatore/wasm/compila.sh
```

I sorgenti del protocollo si leggono senza modificarli. I file diversi sono due:
- [wasm/include/pippanet/airtime.h](wasm/include/pippanet/airtime.h): tiene i parametri radio variabili per provare i profili, e solo questa compilazione lo trova per primo;
- [wasm/airtime.cpp](wasm/airtime.cpp): il tempo in aria giusto anche a SF5/SF6. Da SF7 in su dà gli stessi numeri del firmware.

## File

| File | Cosa |
|---|---|
| `server.js`, `server/` | Server locale: mappe, terreno (TINITALY/AWS → tasselli Int16), ostacoli (MVT → griglia), cache |
| `web/radio.js` | Profili LoRa, tempi in aria, bit rate, modello di propagazione |
| `web/terreno.js` | Tasselli di quota e ostacoli, profili |
| `web/copertura.js` | Worker della copertura (raggi in parallelo) |
| `web/app.js` | Interfaccia: mappa, dispositivi, collegamento, copertura, rete |
| `web/sim.js` | Simulazione PippaNet e animazione, chiamata in diretta |
| `wasm/` | Motore della simulazione e script di compilazione |
