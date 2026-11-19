# Pippaphone: firmware

Firmware del telefono LoRa **Pippaphone** (PCB in `Pippaphone pcb/`) e dei **ripetitori** della
rete PippaNet: LoRa a 868 MHz con SF7, messaggi cifrati end-to-end, gruppi, canale globale,
walkie-talkie, chiamate in diretta a turni, posizione su richiesta e SOS.

> La cartella si chiama `Pippaphone_firmware` (senza spazio) perché ESP-IDF non compila in
> percorsi che contengono spazi.

## Cartelle

```
Pippaphone_firmware/
├── telefono/            firmware del telefono (ESP32-S3R8, display 480×480, tastiera)
│   ├── main/app/        rete, dati, vocali, aggiornamento via Wi-Fi
│   └── main/ui/         interfaccia grafica (LVGL): schermate, widget, font
├── ripetitore/          firmware del ripetitore (+ HARDWARE.md: le schede su cui gira)
├── componenti/
│   ├── pippanet/        protocollo di rete (indipendente dall'hardware, testato sul PC)
│   ├── pippa_radio/     SX1262 con RadioLib, CAD prima di trasmettere, cambio canale
│   ├── scheda/          driver della PCB: expander AW9523B, tastiera TCA8418, MAX17048,
│   │                    display ST7701S, audio I2S, GPS AT6558R; in src/tdeck quelli del
│   │                    LilyGO T-Deck Plus (vedi "LilyGO T-Deck Plus" qui sotto)
│   ├── lvgl/            LVGL 9.2.2 (MIT)
│   ├── RadioLib/        RadioLib 7.7.1 (MIT)
│   ├── monocypher/      Monocypher 4.0.2 (BSD-2 / CC0)
│   ├── codec2/          Codec 2, modo 700C (LGPL 2.1)
│   ├── minimp3/         decodificatore MP3 per la musica del T-Deck (CC0)
│   └── usb_audio/       cuffie USB-C del T-Deck: driver USB Audio 1.0/2.0 (nostro, sulla USB host
│                        di ESP-IDF), ricampionatore e coda, coi loro test sul PC
├── test/                test del protocollo sul PC (rete simulata)
├── strumenti/           generazione dei font (Atkinson Hyperlegible, IBM Plex Mono, Font Awesome),
│   ├── flasher/         pagina per caricare il firmware dal browser (T-Deck e ripetitori)
│   ├── simulatore/      l'interfaccia del telefono sul PC (screenshot di tutte le schermate)
│   │                    e nel browser col T-Deck disegnato (web/)
│   └── tastiera_c3/     firmware a basso consumo per la tastiera (ESP32-C3) del T-Deck
└── PROTOCOLLO.md        descrizione del protocollo PippaNet
```

## Compilare e caricare

Serve **ESP-IDF 5.5** (provato con la 5.5.5). Le librerie sono già tutte nella cartella
`componenti`, quindi non si scarica nulla durante la compilazione.

```sh
. ~/esp/esp-idf/export.sh          # oppure il percorso della tua installazione

cd Pippaphone_firmware/telefono
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # su Windows: -p COM5

cd ../ripetitore
idf.py build flash monitor         # ripetitore sulla PCB del telefono; per Heltec WiFi LoRa 32 V3,
                                   # LilyGO T3-S3 e scheda E22 vedi ripetitore/HARDWARE.md
```

La porta USB-C della PCB è collegata alla USB interna dell'ESP32-S3: non servono adattatori.
Se il chip non entra in programmazione, tieni GPIO0 a massa (punto di test **TP6**) mentre
accendi la scheda (vedi sotto: col cavo USB da solo la PCB rev. 1.1 resta spenta).

### Caricare dal browser (T-Deck Plus e ripetitori)

`strumenti/flasher` prepara una pagina sola per caricare il firmware senza ESP-IDF: il telefono
sul LilyGO T-Deck Plus e il ripetitore su Heltec WiFi LoRa 32 V3 e LilyGO T3-S3 (la PCB per ora
no). Si apre con Chrome o Edge sul computer, anche con un doppio clic e senza internet: i firmware
sono dentro la pagina e li scrive esptool-js di Espressif. Prima di scrivere controlla che la
scheda collegata sia quella scelta (chip ESP32-S3 e memoria da 16, 8 o 4 MB). **Aggiorna** cambia
solo il programma e lascia numero, chiavi e impostazioni; **Installa da zero** cancella tutta la
memoria (serve la prima volta, sopra il firmware di fabbrica o Meshtastic). Alla fine si apre la
console: sui ripetitori ci sono le caselle per dare il numero e la potenza.

```sh
python strumenti/flasher/prepara.py                 # con ESP-IDF attivo: compila i tre firmware
python strumenti/flasher/prepara.py --solo-pagina   # usa le compilazioni che ci sono già
# -> build-flasher/pippaphone_flasher.html (circa 4 MB)
```

### Accensione e spegnimento (PCB rev. 1.1)

Nella rev. 1.1 il 3,3 V si accende col tasto **PWR** e resta acceso perché il firmware, come
prima cosa, porta a 1 l'uscita PWR_HOLD dell'expander (P1_7). Quindi:

- **prima programmazione** (ESP32 vuoto): il cavo USB da solo non accende la scheda. Tieni
  premuto PWR per tutta la programmazione, oppure chiudi il ponticello **JP3** (sempre acceso)
  e riaprilo dopo. Una volta caricato il firmware, il telefono resta acceso anche durante
  `idf.py flash`;
- **accendere**: tieni premuto PWR finché vibra (poco più di un secondo); se lo rilasci
  prima, si rispegne. Nel primo mezzo secondo LED e tastiera si accendono per un attimo: è
  normale, l'expander parte con tutte le uscite a 0 finché il firmware non lo configura;
- **spegnere**: tieni premuto PWR → "Spegnere il telefono?" → Spegni. Lo spegnimento è vero:
  restano alimentati solo il caricabatterie BQ24074 e l'indicatore MAX17048;
- il tasto **RESET** fa ripartire il telefono senza spegnerlo;
- **batteria scarica**: sotto il 5% compare "Batteria quasi scarica". Se non è in carica e
  resta sotto il 2%, oppure sotto 3,3 V, per 30 secondi, il telefono avvisa e si spegne da
  solo, per non rovinare la cella. Con un SOS attivo resta acceso fino a 3,0 V;
- **carica**: il LED rosso si accende col caricabatterie anche a telefono spento. L'icona di
  carica sullo schermo viene dal MAX17048 (la percentuale sale) e compare con un po' di
  ritardo;
- con **JP3 chiuso** il telefono è sempre alimentato: "spegni" torna a essere il deep sleep
  della rev. 1 e si riaccende tenendo premuto PWR.

### Partizioni del telefono (flash da 16 MB)

| Partizione | Dimensione | Uso |
|---|---|---|
| `nvs` | 24 kB | chiavi, numero, certificato, impostazioni |
| `ota_0`, `ota_1` | 4 MB + 4 MB | firmware (due copie per l'aggiornamento via Wi-Fi) |
| `dati` | 7,9 MB (FAT) | rubrica, conversazioni, gruppi, certificati, vocali |

Il firmware occupa circa 2,2 MB. Se un aggiornamento non parte, al riavvio si torna da soli
alla versione precedente.

**Aggiornamento via Wi-Fi** (Impostazioni > Aggiorna il firmware): il telefono crea la rete
`Pippaphone-<numero>` con la password che mostra, a **2,4 GHz** (canale 6: l'ESP32-S3 non fa i
5 GHz). Ci si collega col computer o col telefonino, si apre `http://192.168.4.1` e si carica il
file `.bin` della cartella build. Se la rete non compare nell'elenco, il computer potrebbe non
sentire i 2,4 GHz: è il caso di una scheda Wi-Fi che mostra solo reti a 5 GHz (canali da 36 in
su). Se invece il Wi-Fi del telefono non parte, la schermata scrive il motivo. Sul T-Deck,
dalla console della pagina del flasher: `@RETI?` elenca le reti che il telefono sente, `@WIFI?`
accende la rete per 20 secondi e `@OTA!` prova tutto l'aggiornamento senza un altro apparecchio
(il telefono manda a sé stesso il firmware che ha e riparte dall'altra partizione). Per l'ora:
`@ORA?` dice l'ora del telefono, da dove viene e l'ultima salvata, `@ORA=<secondi Unix>` la mette
a mano, `@SPEGNI!` fa "Spegni" e si risveglia da solo dopo 10 secondi, `@RIAVVIA!` riavvia.

## LilyGO T-Deck Plus

Lo stesso firmware del telefono gira anche sul **LilyGO T-Deck Plus** (ESP32-S3, display 2,8"
320×240, tastiera BlackBerry, trackball, LoRa SX1262, GPS, microSD). Serve la versione **868 MHz**.
L'hardware si sceglie in compilazione (`menuconfig` → "Pippaphone: scheda"): la PCB resta la
scelta predefinita e il suo firmware è identico a prima, byte per byte, come quello del ripetitore
(controllato confrontando i binari: cambiano solo data, ora e impronte SHA-256 della compilazione).

```sh
cd Pippaphone_firmware/telefono
idf.py -B build-tdeck -D SDKCONFIG=build-tdeck/sdkconfig \
       -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.tdeck" build
idf.py -B build-tdeck -D SDKCONFIG=build-tdeck/sdkconfig -p COM5 flash monitor
```

Su Windows (PowerShell) le stesse righe senza `\` a capo. Se si cambia `sdkconfig.defaults.tdeck`
bisogna cancellare `build-tdeck/sdkconfig`: ESP-IDF non riapplica i valori predefiniti a un
sdkconfig che esiste già. Se il T-Deck non entra in programmazione: tieni premuto il clic del
trackball mentre lo accendi o mentre premi RST. Caricando il firmware si cancella quello di
fabbrica LilyGO (si può rimettere dai loro file). La partizione e la memoria sono le stesse della
PCB (16 MB di flash, 8 MB di PSRAM).

### Tasti

La tastiera del T-Deck è la stessa della PCB 2 (BlackBerry Q10): lettere, ALT (numeri e
punteggiatura), SHIFT, SYM (accenti) e $ funzionano come sulla PCB. Al posto dei tasti che il
T-Deck non ha:

| Pippaphone | T-Deck Plus |
|---|---|
| frecce / OK | trackball / clic del trackball |
| PTT | tenere premuto il tasto 🎤 (lo 0); lo 0 si scrive con ALT+🎤, nei campi numerici direttamente |
| BACK | ⌫ quando non c'è testo da cancellare |
| MENU / CALL / END | SYM+M / SYM+C / SYM+H |
| VOL+ / VOL− | SYM + trackball su/giù |
| SOS (VOL+ e VOL− per 3 s) | i due SHIFT insieme per 3 s |
| PWR (schermo acceso/spento; tenuto: spegni) | SYM+spazio |
| SYM + altro tasto | il simbolo stampato sul tasto (come col firmware di fabbrica) |
| frecce, anche così | **W A S D** (su, sinistra, giù, destra) e **Invio** = OK, quando non si sta scrivendo; tenuti premuti si ripetono |

W A S D fanno da frecce in tutte le schermate senza un campo di testo (e nelle finestre di scelta e
nella griglia dei simboli); dove si scrive restano lettere, e dove si scrivono cifre (data e ora,
sveglia, timer) restano le cifre stampate sui tasti. Sulla schermata principale e nella rubrica
quelle quattro lettere non aprono e non cercano più i contatti (le altre sì).

La barra in basso mostra sempre i tasti giusti. Il modo "raw" (tasti premuti e rilasciati) c'è nel
firmware della tastiera (ESP32-C3) dal giugno 2025. Il telefono capisce il modo al primo tasto
premuto e fino ad allora suppone il raw. Con un firmware più vecchio se ne accorge da solo (la barra
in basso si corregge in un paio di secondi) e passa a un **ripiego**: i caratteri arrivano già
composti dal C3 (i numeri con SYM tenuto), il PTT è SHIFT+🎤 premuto una volta per parlare e una per
inviare, MENU è SHIFT+$, CALL è ALT+C, END è SYM+SHIFT+H, l'SOS si annulla tenendo premuto il
trackball, e non ci sono il riquadro degli accenti, il volume dai tasti, la scorciatoia dell'SOS e PWR
(SYM+spazio): niente "Spegni", si spegne solo con l'interruttore e l'ora si perde.
Per avere tutto basta aggiornare il firmware del C3 (esempio Keyboard_ESP32C3 di LilyGO, con un
adattatore USB-seriale sui pin accanto al tasto RST), oppure caricare quello del Pippaphone
(`strumenti/tastiera_c3`: stesso protocollo, consuma molto meno, vedi "Risparmio di energia"). Il
monitor seriale e `info.txt` scrivono quale modo è in uso.

### Differenze dalla PCB

- **Accensione**: con l'interruttore laterale. "Spegni" (e la batteria scarica) spegne le
  periferiche e mette l'ESP32-S3 in sonno profondo; si riaccende **premendo il trackball** (o per
  una sveglia), e l'ora resta giusta perché l'orologio del chip continua a contare. L'interruttore
  laterale invece stacca la batteria: l'ora si perde e arriva poi dalla rete o dal GPS (o va messa
  a mano in Impostazioni > Data e ora). Il GPS allora riparte da freddo: quando resta acceso e quanto
  ci mette a trovarti è in "GPS" più sotto.
- **Vibrazione**: non c'è il motore; la voce "Vibrazione" sparisce dalle impostazioni.
- **LED di notifica**: lampeggia la luce della tastiera, solo a schermo spento.
- **Batteria**: c'è solo la tensione della cella. La percentuale è stimata dalla curva di una
  LiPo; "in carica" si capisce dal salto di tensione quando si collega il caricabatterie, dalla
  percentuale che sale o dalla USB collegata a un computer.
- **GPS**: u-blox M10Q (38400 baud) oppure Quectel L76K (9600), riconosciuto da solo. Quando non
  serve, l'u-blox va in standby; l'L76K resta acceso (non ha un comando per spegnersi). Vedi "GPS"
  più sotto.
- **Display, LoRa e microSD** sono sullo stesso bus SPI, con un turno unico
  (`scheda::busSpiPrendi()`): col solo lucchetto di ESP-IDF ogni tanto la microSD partiva mentre
  il display stava ancora trasmettendo e il telefono si riavviava (assert in `spi_hal_setup_trans`,
  visto dopo i riavvii software).

### microSD

Con la microSD (FAT32: il telefono non la formatta mai) i dati stanno nella cartella
`/pippaphone`: rubrica, chat, gruppi, vocali, certificati degli altri e `impostazioni.txt`, che si
può modificare sul computer a telefono spento. In `/pippaphone/info.txt` il telefono scrive
all'accensione e ogni 10 minuti numero, codici, versione, radio, tastiera e batteria. **Chiavi,
certificato proprio e PIN restano sempre nella memoria interna**: la scheda non contiene niente
che permetta di leggere i messaggi cifrati degli altri telefoni. Senza scheda si usa la memoria
interna come sulla PCB; la prima volta che si mette una scheda, i dati della memoria interna
vengono copiati lì. Non togliere la scheda a telefono acceso.

### Mappe offline

Nel menu c'è **Mappa** (la mia posizione e l'ultima posizione nota di ogni contatto), e le
posizioni ricevute e gli SOS si vedono sulla mappa invece che sulla bussola. Le mappe sono tile
OpenStreetMap PNG 256×256 nella cartella `/mappe/<zoom>/<x>/<y>.png` della microSD: lo schema
"slippy map" che usano quasi tutti i programmi per scaricare mappe offline (per esempio quelli
per le mappe del T-Deck con Meshtastic). Bastano pochi livelli di zoom, per esempio 10–16 per la
zona dove si usa la rete. Attenzione alle condizioni di chi fornisce i tile: i server di
tile.openstreetmap.org non permettono di scaricare zone intere; si usano fornitori che lo
consentono o tile generati da sé. Sulla mappa: trackball = sposta, OK o I = più vicino, O = più
lontano, C = di nuovo tutti i segnaposto. Senza tile i segnaposto si vedono lo stesso, con la
scala delle distanze.

**La mappa e il GPS.** La mappa tiene acceso il GPS finché è aperta e ogni 2 secondi sposta il mio
punto (e quelli dei contatti, se arriva una posizione nuova) senza spostare la mappa né cambiare lo
zoom. La prima volta che compaio la mappa mi inquadra insieme agli altri, se non l'hai spostata a
mano; se no per qualche secondo dice "Ti ho trovato · C per vedere tutti". Lo stesso quando arriva
la posizione di adesso e sulla mappa c'era solo una mia posizione vecchia. Finché non c'è la
posizione, in alto a sinistra c'è "Cerco i satelliti · ne sento N" (o "Il GPS non risponde"). Una
mappa senza nessun segnaposto si apre sulla zona dei tile della microSD (l'Italia centrale solo se
non ce ne sono). Lo stesso punto che si muove c'è sulla mappa della posizione di un contatto.

**Le mappe dal flasher.** La pagina per caricare il firmware (sezione "5. Mappe sulla microSD")
disegna i tile della zona scelta su una cartina, coi dati OpenStreetMap di OpenFreeMap (serve
internet sul computer), e li manda al telefono lungo lo stesso cavo USB, anche subito dopo
l'aggiornamento: il telefono li scrive in `/mappe` della sua microSD. Il protocollo è in
`telefono/main/app/usb_mappe.h` (righe di testo `@...`, file a pezzi da 4 kB con conferma e
CRC32, solo percorsi `zoom/x/y.png`). Sul telefono compare "Mappe dal computer" e alla fine
"Mappe arrivate"; la mappa vede subito i tile nuovi.

**Zone pronte e cartelle.** In "Zone pronte" c'è già la Liguria da Finale Ligure a Sestri Levante,
Genova compresa; con "Salva questa zona" se ne aggiungono altre, che restano nel browser di quel
computer. Oltre a "Copia sulla microSD" (col cavo, circa 8 tile al secondo, al massimo 400 000 per
volta) ci sono due modi con le cartelle del computer, solo con Chrome o Edge:

- **Salva in una cartella…** disegna i tile e li scrive in una cartella, senza telefono. Il modo più
  veloce è scegliere la microSD stessa nel lettore di schede del computer (per esempio `E:`): i tile
  vanno subito in `mappe/` della sua cartella principale, l'unico posto dove il telefono li cerca.
  In un'altra cartella del computer vanno nella sua sottocartella `mappe`, che poi si copia nella
  cartella principale della microSD. Accanto agli zoom c'è `cartina.txt`, con la cartina usata (il
  telefono non lo guarda). Al massimo un milione di tile per volta.
- **Manda da una cartella…** manda al telefono, col cavo, i tile di una cartella salvata prima,
  senza disegnarli di nuovo (col cavo vale lo stesso limite di 400 000).

In tutti e tre i modi "Ferma" si può premere quando si vuole: "Continua" o "Riprova" saltano i tile
che ci sono già. La microSD dev'essere FAT32: quelle fino a 32 GB lo sono già, quelle più grandi di
solito sono exFAT e il telefono non le legge.

### GPS

**Cosa vuol dire.** La schermata Posizione, la mappa, la richiesta di posizione e l'SOS dicono:

- **in vista**: i satelliti che il ricevitore elenca nel cielo (frasi GSV), sentiti o no;
- **ne sento**: quelli in vista di cui riceve davvero il segnale. "max 31 dB" è il segnale migliore
  (C/N0, in dB-Hz): all'aperto col cielo libero di solito supera i 30, al chiuso spesso resta sotto 20;
- **usati**: quelli con cui ha calcolato la posizione (frase GGA). Sono 0 per forza finché la
  posizione non c'è: per questo prima la schermata diceva "0 satelliti" anche sentendone parecchi.

L'icona del GPS nella barra di stato è grigia col GPS spento, ambra mentre cerca i satelliti (o
quando non risponde), verde con una posizione di adesso (degli ultimi 5 secondi). "Il GPS non
risponde" vuol dire che il ricevitore, acceso, tace da più di 5 secondi: il telefono intanto lo
risveglia o lo cerca; se dura più di un minuto, spegni e riaccendi il telefono.

**La prima posizione.** Dopo "Spegni" o l'interruttore (il GPS resta senza corrente) e senza l'ora
il ricevitore riparte da freddo: all'aperto, col cielo libero e il telefono fermo, la prima
posizione può richiedere qualche minuto; al chiuso spesso non arriva.

**Quando è acceso** (solo T-Deck: sulla PCB la regola è quella di prima). Lo decide
`telefono/main/app/gps_politica.h`, una volta al secondo:

- all'accensione, con o senza l'ora, fino alla prima posizione più 3 minuti, al massimo 20 minuti
  (prima si spegneva appena arrivava un'ora, dalla rete o dal GPS, spesso prima della posizione);
- finché è aperta una schermata che lo usa (Posizione, Mappa, la posizione di un contatto, una
  richiesta di posizione che aspetta il GPS, al massimo 3 minuti, gli SOS), anche sotto altre
  schermate o sotto il PIN; e come prima per le richieste di posizione e l'SOS attivo;
- finite le richieste, se nelle ultime 4 ore non c'è stata una posizione, ancora 5 minuti o fino
  alla prima posizione;
- ricerche periodiche, finito l'avvio: ogni 15 minuti senza ora; ogni 30 con l'ora messa a mano o
  ritrovata nell'orologio del chip, oppure senza una posizione nelle ultime 24 ore. Ognuna dura al
  massimo 3 minuti (o fino a 1 minuto dopo la posizione); se in 60 secondi nessun satellite arriva a
  20 dB-Hz (al chiuso) si ferma e l'intervallo raddoppia, fino a 2 ore. Una posizione, un'ora buona
  nuova (dal GPS o dalla rete) o una schermata del GPS lo riportano all'inizio.

Il registro scrive ogni accensione e spegnimento col motivo, per esempio `GPS acceso: schermata` o
`GPS spento: ricerca finita: al chiuso (nessun satellite a 20 dB-Hz) (prossima ricerca tra 30 min)`.
Il ricevitore lo guida `componenti/scheda/src/tdeck/gnss_pilota.cpp`: standby col PMREQ e controllo
che le frasi si fermino davvero (se l'u-blox lo ignora resta sempre acceso), sveglia con fino a 5
tentativi, e se tace da 5 secondi lo risveglia o lo cerca di nuovo. Il PMREQ ha i flag backup e
force (0x06). La prova al banco del 15/11/2026, su un MIA-M10Q con ROM SPG 5.10 e senza quarzo
dell'orologio (`xtalAbsent 1`), ha dato questi risultati:
- col solo backup (0x02) si fermava, ma per svegliarlo servivano da 1 a 5 byte, fino a 8 secondi;
- con force si sveglia sempre al primo byte, in 0,3 secondi.

**Dal computer** (console della pagina del flasher, come gli altri comandi `@`):

- `@GPS?`: ricevitore (u-blox, L76K o muto), acceso, satelliti in vista e usati, posizione, ora; e
  una riga `gps2` con standby, stato del pilota, millisecondi dall'ultima frase valida e
  dall'ultima RMC, GGA e GSV, satelliti con segnale e segnale migliore, frasi buone e scartate,
  ultima sveglia, PMREQ rispettato o ignorato e stack libero del compito del GPS;
- `@GPS!STORIA`: gli ultimi 64 eventi (acceso e spento col motivo, sveglie, standby, ricerca della
  velocità, prima ora e prima posizione, e ogni minuto senza posizione quanti satelliti sente);
- `@GPS!VER`: versione dell'u-blox (UBX-MON-VER) e UBX-MON-HW3, che tra l'altro dice se manca il
  quarzo dell'orologio (`xtalAbsent`);
- `@GPS!PROVA [cicli] [forza]`: prova al banco dello standby (5 cicli se non si dice, al massimo
  10; `forza` resta per compatibilità: il PMREQ normale ha già il flag "force"): per ogni ciclo se
  le frasi si fermano, se dorme davvero e quanto ci mette a svegliarsi, poi un riassunto;
- `@GPS!FREDDO`: lo accende per mezz'ora e lo fa ripartire da freddo, come dopo l'interruttore;
- `@NMEA!`: le frasi NMEA nel registro per 60 secondi.

### Cuffie USB-C

La musica può andare a **cuffie USB-C digitali** (classe USB Audio 1.0 o 2.0, full speed) invece
che alla cassa. Di solito però la porta USB-C del T-Deck è del computer (log, pagina del flasher,
mappe via cavo, comandi `@`): con le cuffie accese fa da "host" per le cuffie e il computer non vede
più il telefono. Per questo si accendono a mano e valgono per una sessione: a ogni avvio la porta è
del computer.

**Cosa serve**

- Cuffie USB-C **digitali**, cioè col convertitore dentro (collegate al computer, Windows le vede
  come un'uscita audio), oppure un adattatore USB-C → jack 3,5 mm con cuffie normali (quello di
  Apple è il più sicuro). Non vanno: cuffie USB-C senza convertitore (analogiche), Bluetooth,
  dispositivi solo USB Audio 3.0 o con pacchetti oltre 600 byte, hub e dock alimentati dalla porta.
- Uno **sdoppiatore OTG alimentato** ("sdoppiatore OTG USB-C 2 in 1 con porta di ricarica, senza
  hub"): la USB-C del T-Deck non dà corrente, quindi le cuffie la prendono dallo sdoppiatore,
  attaccato a un caricabatterie o a un power bank da almeno 1 A (intanto il T-Deck si carica).

**Come si usa**

- Impostazioni → **Cuffie USB-C** → OK → "Attiva", oppure dal lettore musicale MENU → "Ascolta
  con le cuffie USB-C". Non si accendono col computer collegato ("Scollega il computer"), con
  l'aggiornamento Wi-Fi acceso o con poca RAM interna.
- La riga dice "in attesa", poi il nome delle cuffie quando suonano ("non supportate" o "errore"
  con un dispositivo che non va, e un avviso che dice il perché). Nella barra di stato c'è l'icona
  delle cuffie: grigia in attesa, ambra quando suonano.
- Alle cuffie va **solo la musica** (stessa uscita a sinistra e a destra); vocali, chiamate,
  suonerie, sveglie e avvisi restano sulla cassa.
- Il volume è a parte: **Volume cuffie** (0–10, all'inizio 4) in Impostazioni, oppure su/giù nel
  lettore e i tasti del volume mentre le cuffie suonano ("volume 4 · cuffie"). Lo fa il telefono,
  con una rampa: il volume delle cuffie stesse resta a 0 dB.
- Staccando le cuffie la musica **va in pausa** (dalla cassa non esce niente); OK la fa ripartire
  dalla cassa.

**Cosa non funziona con le cuffie accese**: log sulla USB, pagina del flasher (collegamento,
caricamento e console), mappe via cavo, tutti i comandi `@` (anche `@PIPPA?`), il reset automatico
di esptool, il JTAG, e il segno "collegato a un computer" della carica (la carica si capisce dalla
tensione). L'aggiornamento via Wi-Fi non sta in memoria insieme alle cuffie: aprendo "Aggiorna il
firmware" le cuffie si spengono prima. Il log non si perde: finché le cuffie sono accese va in una
"scatola nera" (32 kB in PSRAM) che si stampa sulla console appena torna.

**Come si torna al computer**

- dalla stessa riga (OK) o dal MENU del lettore ("Spegni le cuffie USB-C");
- da solo, dopo **2 minuti** senza cuffie che suonano;
- a ogni riavvio, con "Spegni" e aprendo "Aggiorna il firmware";
- sempre: col tasto RST; per caricare il firmware anche così, trackball tenuto premuto + RST.

Al banco, dal computer: `@CUFFIE! <secondi>` (5–600) accende le cuffie anche col computer
collegato e la porta torna da sola dopo quei secondi; `@CUFFIE?` dice com'è andata l'ultima
sessione (stato, dispositivo, formato, secondi suonati, buchi nella musica, pacchetti saltati,
errori, RAM interna prima, durante e dopo).

Il driver è nostro (`componenti/usb_audio`, sopra la USB host di ESP-IDF: niente componenti
scaricati); il modo cuffie è `telefono/main/app/cuffie.cpp`. Sulla PCB non c'è niente di tutto
questo e il suo firmware è rimasto identico.

### Risparmio di energia

Con una carica (2000 mAh) il T-Deck durava circa 20 ore: circa 100 mA di media. A schermo spento
restava acceso quasi tutto. Le stime qui sotto vengono dai datasheet, non da misure:

| Dove | Prima (mA, stima) | Adesso |
|---|---|---|
| ESP32-S3 sempre a 240 MHz, mai in sonno | 30–40 | 40 MHz a riposo e light sleep a schermo spento: pochi mA |
| ESP32-C3 della tastiera, sempre sveglio a 80 MHz | 8–13 | col firmware di `strumenti/tastiera_c3`: dorme |
| display acceso al buio (solo la luce spenta) | 3–6 | pannello in sonno (SLPIN) |
| I2S dell'altoparlante sempre acceso + MAX98357A | 1–3 | acceso solo mentre suona qualcosa |
| touch GT911 (non usato) | qualche mA (?) | a dormire dall'avvio |
| SX1262 in ricezione | circa 5 | uguale: la radio deve sentire |

Cosa fa il firmware (tre fasi, un commit ciascuna; prima il bug della radio):

- **radio**: il compito della radio non gira più ogni 5 ms a vuoto quando un pacchetto aspetta il suo
  turno o un beacon scade durante una chiamata (vale anche per i ripetitori; 3 test nuovi);
- **fase 1**: frequenza della CPU variabile (`CONFIG_PM_ENABLE`), UART del GPS sul quarzo, I2S a
  richiesta, pannello in sonno e niente disegni a schermo spento, GT911 a dormire, ES7210 spento davvero,
  luce dello schermo al minimo dopo 10 s senza tasti, luce della tastiera spenta 5 s dopo l'ultimo
  tasto, interfaccia a schermo spento ogni 250 ms. Corregge anche il lampeggio degli avvisi, che non
  finiva mai (ora si ferma quando non resta niente da leggere: anche sulla PCB);
- **fase 2**: light sleep automatico (`CONFIG_FREERTOS_USE_TICKLESS_IDLE`). Tengono il chip sveglio lo
  schermo acceso, il GPS che parla, l'I2S acceso, le cuffie, il Wi-Fi e un computer sulla USB. I pin
  della scheda restano com'erano (se no ESP-IDF li isolerebbe, POWERON compreso). DIO1 della LoRa
  sveglia il chip; tastiera e trackball si leggono ogni 40 ms;
- **fase 3**: il firmware del C3 (`strumenti/tastiera_c3`, da caricare a parte). Con quello l'S3 a
  schermo spento non legge più la tastiera: aspetta la linea `C3_INT`.

**Compilare.** Le opzioni nuove sono in `sdkconfig.defaults.tdeck`: prima di compilare cancella
`build-tdeck/sdkconfig` (ESP-IDF non riapplica i valori predefiniti a un sdkconfig che esiste già).
Per tornare indietro di una fase: senza `CONFIG_FREERTOS_USE_TICKLESS_IDLE` resta la fase 1; senza
anche `CONFIG_PM_ENABLE` il chip va sempre a 240 MHz come prima.

**Misurare.** Il modo più semplice: batteria carica al 100%, un misuratore USB tra caricatore e T-Deck
(non un computer: col computer collegato il light sleep è spento apposta) e lo schermo spento da un
minuto. Quello che segna è quasi solo il consumo del telefono. Confronta prima e dopo ogni fase, e col
firmware del C3 nuovo.

### Da provare sul T-Deck vero

Scritto dagli schemi e dagli esempi di LilyGO, provato nel simulatore ma non ancora su un T-Deck:

1. **display**: orientamento e colori (`SWAP_XY`, `MIRROR_X`, `INVERT` e ordine RGB in
   `scheda/src/tdeck/display_tdeck.cpp`) e i 16 livelli della retroilluminazione;
2. **tastiera**: riconoscimento del modo raw e del ripiego; il monitor seriale stampa ogni tasto
   (colonna, riga, nome) e il modo scelto;
3. **trackball**: sensibilità (`TB_EDGES_PER_STEP` in `tastiera_tdeck.cpp`);
4. **audio**: canale e guadagni del microfono (ES7210, 24 dB + `MIC_DIGITAL_GAIN` in
   `audio.cpp`), volume dell'altoparlante;
5. **GPS**:
   - provati il 15/11/2026:
     - riconoscimento della velocità: u-blox a 38400 in 350 ms;
     - standby col flag force: 5 sveglie su 5 al primo byte (`@GPS!PROVA`);
     - quarzo dell'orologio assente (`@GPS!VER`);
   - da provare:
     - la prima posizione all'aperto dopo "Spegni" e dopo l'interruttore;
     - la politica nuova (`@GPS!STORIA` e le righe `GPS acceso` e `GPS spento` del registro);
6. **batteria**: curva tensione → percentuale e riconoscimento della carica
   (`batteria_tdeck.cpp`);
7. **microSD**: montaggio, copia dei dati dalla memoria interna, mappe;
8. **radio** sul bus SPI condiviso col display;
9. **sonno profondo**: il consumo da "spento". Il risveglio è provato sul T-Deck vero (14/11/2026):
   col trackball e da solo dopo un tempo (come per una sveglia), anche più volte di fila, e l'ora
   resta giusta al secondo. Serviva tenere acceso nel sonno l'oscillatore che fa da orologio del
   chip: dopo un riavvio (aggiornamento, crash, caricamento via USB) ESP-IDF 5.5 lo spegneva, e il
   T-Deck non si svegliava più;
10. **cuffie USB-C**: tutto (finora solo test sul PC e simulatore). Prima con le cuffie e lo
    sdoppiatore veri: quali cuffie vanno (il log dei descrittori), se la porta torna sempre al
    computer (anche dopo un riavvio, un crash e il sonno profondo presi a cuffie accese), buchi e
    clic nella musica con radio, schermo e microSD al lavoro, RAM interna dopo tante accensioni.
11. **risparmio di energia** (vedi sopra), a schermo spento:
    - un messaggio arriva e suona subito (DIO1 sveglia il chip), anche dopo ore;
    - un tasto e il clic del trackball riaccendono lo schermo; l'immagine compare già aggiornata;
    - sveglie, timer, musica, chiamate e vocali come prima; il GPS trova la posizione;
    - suoni brevi (il clic del volume) non tagliati all'inizio: se lo sono, aumentare l'attesa dopo
      `amp(true)` in `audio.cpp`;
    - collegando il computer la console compare (con la CPU a 40 MHz al massimo qualche secondo); se
      no, `pm.min_freq_mhz = 80` in `scheda_tdeck.cpp`;
    - nel registro `risparmio: CPU da 40 a 240 MHz, light sleep a schermo spento` e
      `touch GT911 (0x5d): a dormire`;
    - il consumo, prima e dopo, e poi col firmware nuovo del C3.

## Mettere in piedi la rete

1. **Primo telefono = amministratore.** Al primo avvio il telefono mostra il suo codice e
   aspetta un numero. Premi **MENU → "Crea una rete nuova"** e scegli il tuo numero: questo
   telefono diventa l'amministratore e firma i numeri degli altri.
2. **Altri telefoni.** Accendi il telefono nuovo vicino all'amministratore. Sull'amministratore:
   **Impostazioni → Amministrazione**: compare il codice del telefono nuovo. Controlla che sia
   uguale, premi OK e scegli il numero (1–59999). Sul telefono nuovo compare il numero con il
   codice dell'amministratore: se è uguale, premi OK.
3. **Ripetitori.** Carica il firmware e, dalla console USB (`idf.py monitor`), dai a ognuno un
   numero diverso:

   ```
   numero 3        → diventa R3
   potenza 22      → dBm dell'SX1262 (con l'E22-900M30S escono circa 30 dBm)
   info            → stato, batteria, pacchetti ripetuti
   strade          → telefoni che sa raggiungere e da che parte
   schermo 120     → (Heltec, T3-S3) secondi di schermo acceso; 0 = sempre; "gira" = capovolto
   riavvia
   ```

   I ripetitori non hanno bisogno di chiavi: ripetono pacchetti che non possono leggere.
   Sulla Heltec V3 e sulla LilyGO T3-S3 lo schermo mostra stato, vicini diretti e radio (tasto
   PRG o BOOT): per scegliere dove montarli non serve il computer.

## Uso del telefono

| Tasto | Cosa fa |
|---|---|
| **MENU** / OK | menu principale (dalla schermata principale) |
| **CALL** | messaggi; nelle liste: scrivi al contatto |
| **END** | torna alla schermata principale |
| **BACK** | indietro |
| **PTT** (lato destro) | walkie-talkie: tieni premuto, parla, lascia |
| **VOL+ / VOL−** | volume |
| **VOL+ e VOL− insieme per 3 s** | **SOS** da qualsiasi schermata |
| **PWR** | da spento, tenuto premuto finché vibra: accendi; da acceso: schermo acceso/spento, tenuto premuto: spegni |
| **SYM** | lettere accentate e simboli |
| **ALT** | numeri e punteggiatura (due volte: blocca) |
| **SHIFT** | maiuscola (due volte: tutto maiuscolo) |

Da una chat: su/giù scorre i messaggi, OK apre un vocale o una posizione, PTT registra un
vocale, MENU mostra le opzioni (chiedi la posizione, walkie-talkie, scheda contatto...).

**Chiamate in diretta.** Dalla scheda del contatto: **Chiama**. Chi è chiamato sente lo squillo e
risponde con **CALL** (o OK), rifiuta con **END**. In chiamata si parla **a turni**: tieni premuto
**PTT** e parla, poi lascia; quando l'altro ha finito si sente un bip. Mentre parla l'altro il PTT
non parte. **END** riattacca. La voce arriva dopo circa 0,6–1,1 secondi. Una chiamata dura al
massimo **3 minuti**: chi è in chiamata, e i ripetitori sul percorso, non ricevono messaggi. Chi
ti scrive in quel momento vede "Non raggiungibile: riprovo" e il telefono rimanda il messaggio da
solo 3,5 minuti dopo (o appena sente il tuo telefono).
Un SOS chiude la chiamata. In chat resta una riga: chiamata fatta, ricevuta, persa o non riuscita.

**Grafica.** Due temi, in Impostazioni > Tema. **Carta** (quello normale): fondo bianco e testo
nero come un foglio stampato, che si legge anche al sole; bordi neri decisi al posto delle superfici
grigie, il selezionato pieno di blu, l'arancio per le cose urgenti, la barra di stato nera. **Scuro**:
la grafica di prima, per la notte (lo schermo bianco abbaglia; sui display LCD di T-Deck e PCB il tema
non cambia il consumo, che dipende dalla luminosità). Tutti e due usano
solo i colori che il pannello della PCB mostra senza sfumature (rosso e blu a 5 livelli). Il tema
cambia subito, senza riavviare. Nel file `impostazioni.txt` della microSD è la chiave `tema`
(0 Carta, 1 Scuro).

**Menu a pagine.** Le icone stanno in pagine, come sui telefoni: la prima è quella di sempre, nella
seconda ci sono **Orologio**, **Giochi** e **Musica**. Andando oltre il bordo destro (o sinistro)
di una pagina si passa all'altra con uno scorrimento; i pallini in basso dicono dove si è. Con Carta
le icone sono nere su tessere bianche col bordo nero, e la scelta è piena di blu.

**Orologio** (tre schede, sinistra/destra per cambiare):
- **Sveglie**: fino a 10, ognuna una volta sola, tutti i giorni, lun–ven, sab e dom o un giorno
  della settimana. L'ora si scrive con le cifre dei tasti o con su/giù. Suonano da qualsiasi
  schermata e a schermo spento (anche col volume a zero si sentono, almeno a 6/10): OK ferma, un
  altro tasto posticipa di 5 minuti, dopo 2 minuti si posticipa da sola. Durante una chiamata, un
  SOS o una registrazione aspettano. Nella barra di stato c'è il campanello se una sveglia è
  accesa. **Sul T-Deck suonano anche col telefono spento** ("Spegni" è un sonno profondo: si
  riaccende un minuto prima e chiede se restare acceso); sulla PCB no, lo spegnimento è vero.
- **Timer**: la durata si scrive con le cifre (entrano da destra, come sui telefoni) o su/giù di
  un minuto; OK avvia e mette in pausa, giù azzera. Suona anche uscendo dall'orologio.
- **Cronometro**: OK avvia e ferma, giù segna un giro (o azzera, da fermo).

**Data e ora** (Impostazioni): giorno, mese, anno, ore e minuti. Valgono **solo per questo
telefono**: nei beacon l'ora messa a mano esce come non sincronizzata, così non passa ai
ripetitori, e appena il GPS ha l'ora la corregge (se l'ha data da meno di 6 ore, a mano non si
può cambiare). L'ora non si perde più dopo un aggiornamento o un riavvio chiesto, e quando il
T-Deck si risveglia per una sveglia: la tiene l'orologio del chip ("Data e ora" dice "dal
telefono") e la corregge la prima ora della rete o del GPS, perché l'orologio del chip si sposta un
po'. Dopo un crash, un watchdog, "Spegni" senza sveglia (sulla PCB col ponticello JP3) o quando manca
la corrente, l'ora arriva dalla rete o dal GPS come prima; e "Data e ora" riparte dall'ultima ora
nota (il telefono la salva ogni 10 minuti), così di solito basta correggere ore e minuti.

**Giochi**:
- **Serpente** (si gira con le frecce, OK pausa) e **Campo minato** (facile, medio, difficile:
  OK scopre, F o spazio mettono la bandierina, OK su un numero scopre i vicini), coi record;
- **Tris** e **Battaglia navale via radio** contro un contatto, una partita alla volta, mossa
  per mossa col pacchetto `GAME` (vedi `PROTOCOLLO.md`). Chi sfida comincia. Se arriva una sfida
  si risponde subito o "Dopo" (resta in Giochi); se una mossa non arriva, OK la rimanda; MENU
  abbandona, BACK esce senza abbandonare (la partita si riapre da Giochi). Nella battaglia le navi
  si mettono a caso (su/giù per rimescolarle).

**Musica** (solo T-Deck): i file **MP3 e WAV** della cartella `/musica` della microSD (anche in
sottocartelle, un livello; nomi fino a 64 caratteri). La microSD si toglie e si rimette **a telefono
spento**: si prepara solo all'accensione, e una scheda rimessa a telefono acceso non risponde (la
schermata lo dice: basta spegnere e riaccendere). OK pausa, sinistra/destra brano, su/giù
volume, MENU avanti o indietro di 10 secondi o ferma. Continua uscendo dal lettore e a schermo
spento (nella barra c'è la nota); avvisi, vocali, chiamate e sveglie la interrompono e poi
riparte da sola; mentre si registra col microfono resta zitta. MP3 a tutte le frequenze, WAV PCM
a 8 o 16 bit da 8 a 48 kHz. La musica è alzata di 6 dB (la cassa è piccola) e un limitatore
abbassa solo le punte, così non satura; suonerie, vocali e chiamate restano come sono. L'amplificatore del T-Deck (MAX98357A) accetta solo 8, 16, 32, 44,1 e
48 kHz: i brani a 11, 12, 22 e 24 kHz si suonano a 44,1 o 48 kHz, interpolati. Sulla PCB per ora
c'è solo un avviso: lo slot microSD (J6) c'è, ma il firmware non lo usa ancora.

**Cuffie.** L'ESP32-S3 (PCB e T-Deck) ha solo il Bluetooth Low Energy: niente A2DP né LE Audio,
quindi niente cuffie Bluetooth. Nessuna delle due schede ha una presa per cuffie. Sul T-Deck la
musica può andare a cuffie USB-C **digitali** (USB Audio 1.0 o 2.0, full speed) attaccate con uno
sdoppiatore OTG alimentato: vedi "Cuffie USB-C" più sopra. Sulla PCB no. Gli adattatori passivi
USB-C→jack non possono funzionare (quelli col convertitore dentro, come quello di Apple, sì). Per
una presa da 3,5 mm vera: sul T-Deck un piccolo DAC I2S con jack sulle linee dell'altoparlante; per
una PCB nuova un codec (per esempio TLV320DAC3100) al posto del MAX98357A, sugli stessi pin.

## Parametri radio

| | Messaggi (canale principale) | Vocali (canale voce) |
|---|---|---|
| Frequenza | 869,525 MHz | 869,850 MHz |
| Banda | 62,5 kHz | 125 kHz |
| SF / CR | 7 / 4:5 | 7 / 4:5 |
| Bit rate | circa 2,7 kbit/s | circa 5,5 kbit/s |
| Sensibilità | circa −127,5 dBm | circa −124,5 dBm |
| Sync word | 0x6C (rete privata) | 0x6C |
| Potenza | fino a 22 dBm, **automatica** sul telefono | 22 dBm |

Tempi in aria indicativi: SMS di 160 caratteri circa 0,6 s, conferma di consegna circa 0,2 s,
vocale di 15 secondi circa 7 pacchetti da 0,4 s per ogni salto, chiamata un pacchetto da 0,13–0,18
s ogni 0,4 s di voce (0,14 s ogni 0,48 s con 2 o più ripetitori).

Fino al 9 novembre 2026 la rete usava SF10 (125 kHz per i messaggi, 250 kHz per la voce): i
pacchetti erano circa 3 volte più lunghi e la portata maggiore (4,5 dB in più). Telefoni e
ripetitori con il firmware vecchio **non si sentono** con quelli nuovi: vanno aggiornati tutti
insieme. Per decidere dove mettere i ripetitori c'è il pianificatore in
`strumenti/pianificatore`.

**Nota legale.** Come richiesto, il firmware non impone limiti di duty cycle né di potenza.
In Italia e nell'UE la sottobanda 869,40–869,65 MHz permette 500 mW ERP con un duty cycle del
10%, e 869,85 MHz ha limiti più bassi. Chi usa la rete si assume la responsabilità di
rispettare le norme.

## Test sul PC

Il protocollo si prova senza hardware, con una rete simulata (telefoni, ripetitori, pacchetti
persi, collisioni, nodi nascosti):

```sh
cd Pippaphone_firmware
cmake -S test -B build-test && cmake --build build-test
./build-test/pippanet_test                  # 38 test
PN_SEED=7 ./build-test/pippanet_test        # altro seme casuale
PN_TRACE=1 ./build-test/pippanet_test       # stampa ogni trasmissione
```

Le parti delle cuffie USB-C senza ESP-IDF (descrittori USB Audio, scelta del formato, ricampionatore,
coda, volume) hanno i loro test: `usb_audio_test` (descrittori di cinque dispositivi veri, qualità del
ricampionatore misurata coi toni, coda tra due thread), più `componenti/usb_audio/test_host` (tutti i
casi dei descrittori e un fuzzer) e `componenti/usb_audio/test_host_dsp` (il ricampionatore
ottimizzato e i suoi tempi). Su Windows con MSYS2:

```sh
cmake -S test -B build-test -G Ninja && cmake --build build-test --target usb_audio_test
./build-test/usb_audio_test uac_            # descrittori; dsp_ per il ricampionatore
```

Il GPS del T-Deck ha due gruppi di test. `gps_politica_test` (10 test) prova quando il telefono lo
accende (`telefono/main/app/gps_politica.h`: avvio, ricerche periodiche e loro raddoppi al chiuso,
schermate, richieste, grazia), un giro al secondo per ore simulate. `componenti/scheda/test_host`
(`gnss_test`, 37 test) prova l'analizzatore NMEA (flussi realistici di u-blox M10, L76K e AT6558R,
serie GSV, dati che scadono) e il pilota del ricevitore contro un u-blox finto con un orologio finto
(standby col PMREQ, sveglia, ricevitore che ignora il PMREQ, che tace o che appena sveglio non sente
i comandi, prova al banco). Su Windows con MSYS2 (`C:\msys64\ucrt64\bin` nel PATH):

```sh
cmake -S test -B build-test -G Ninja && cmake --build build-test --target gps_politica_test
./build-test/gps_politica_test
cmake -S componenti/scheda/test_host -B build-gnss-test -G Ninja && cmake --build build-gnss-test
./build-gnss-test/gnss_test                 # nmea_ per l'analizzatore, gps_ per il pilota
```

### Simulatore dell'interfaccia

`strumenti/simulatore` compila sul PC le schermate vere, i dati veri e LVGL, con una scheda, una
rete e un GPS finti, e salva uno screenshot di ogni schermata (65 per la PCB, 94 per il T-Deck,
mappe, chiamata, orologio, giochi, musica e cuffie USB-C comprese; sul T-Deck anche il GPS che cerca
i satelliti, che non risponde e che trova la posizione, nella schermata Posizione, sulla mappa, nelle
richieste e negli SOS). Controlla anche che nessuna scritta esca dal suo riquadro. Su Windows serve
MSYS2 (gcc e ninja), su Linux basta gcc:

```sh
cmake -S strumenti/simulatore -B build-sim -G Ninja && cmake --build build-sim
build-sim/sim_tdeck screenshot-tdeck            # 320x240
build-sim/sim_pcb screenshot-pcb                # 480x480
SIM_NOMI_LUNGHI=1 build-sim/sim_tdeck prova     # tutti i nomi lunghissimi
SIM_NOMI_MAX=1 build-sim/sim_pcb prova          # nomi lunghi quanto il massimo che si può scrivere
SIM_TEMA=scuro build-sim/sim_pcb scuro-pcb      # col tema Scuro (senza: Carta)
```

Per controllare che una modifica non cambi l'interfaccia della PCB si compila il simulatore anche
coi sorgenti di prima (`-DUI_DIR=... -DSCHEDA_DIR=...`) e si confrontano gli screenshot: devono
essere identici pixel per pixel. Col tema Scuro (`SIM_TEMA=scuro`) si controlla che la grafica di
prima non sia cambiata.

### Simulatore nel browser (T-Deck Plus)

Lo stesso simulatore, compilato in WebAssembly con Emscripten, diventa una pagina HTML sola (circa
1,8 MB) col T-Deck disegnato: tasti cliccabili, trackball, interruttore, RST e la guida a tutte le
combinazioni, con un pulsante "Prova" per ognuna. Interfaccia, dati e tastiera sono il codice vero,
compreso `tastiera_tdeck.cpp`: un ESP32-C3 finto risponde in modo raw coi tasti premuti nella pagina.
La rete finta risponde ai messaggi, ai vocali, alle richieste di posizione e all'SOS (Luca Verdi è
fuori portata). Dalla pagina si fanno arrivare messaggi, vocali, SOS, richieste di posizione, inviti
nei gruppi e la batteria scarica. Si usa anche la tastiera del computer: Tab tenuto vale sym, Ctrl
tenuto il microfono (PTT), Invio il clic del trackball. Serve Emscripten (emsdk):

```sh
emcmake cmake -S strumenti/simulatore -B build-web -G Ninja && cmake --build build-web
# -> build-web/pippaphone_tdeck.html: si apre col browser, anche come file, senza server
```

I sorgenti della pagina sono in `strumenti/simulatore/web` (`shell.html` è la pagina, `sim_web.cpp`
il collegamento col browser, `rete_web.cpp` la rete finta).

## Stato del lavoro

**Fatto e verificato:**

- protocollo PippaNet completo: SMS con conferme di consegna e lettura, "non raggiungibile",
  gruppi fino a 16 persone, canale globale firmato, certificati firmati dall'amministratore,
  posizione con conferma, SOS ripetuto ai contatti, vocali sul canale voce con pezzi mancanti
  richiesti di nuovo, beacon ogni 5 minuti, assegnazione del numero via radio. I 21 test
  passano con 120 semi diversi a SF7 e con 40 a SF10 (reti con pacchetti persi e nodi
  nascosti);
- **chiamate in diretta a turni** (9 novembre 2026): protocollo (`CALL_*`, `PROTOCOLLO.md`),
  ripetitori, audio continuo (`scheda::Audio::stream*`, `app::Voce::call*`) e schermata
  (`ui/schermate_chiamata.cpp`). I 7 test delle chiamate (diretta, tre ripetitori, rifiutata,
  occupato, persa, non raggiungibile, caduta, durata massima, SOS, ripetitori in parallelo, strada
  cambiata) passano con 120 semi insieme agli altri. Il simulatore del pianificatore usa lo
  stesso codice: con una rete ben fatta (10 dB di margine in più) in diretta 114 chiamate buone
  su 120, con un ripetitore 98 su 112, con 2 o 3 ripetitori circa 50 buone e 35 "si capisce"
  su 110 (7–8% di voce persa); i numeri sono in `strumenti/pianificatore/LEGGIMI.md`;
- **messaggi rimandati più tardi**: un SMS (o un invito a un gruppo) senza conferma dopo 4
  tentativi diventa "Non raggiungibile: riprovo" e fa un altro giro 3,5 minuti dopo, o subito se
  si sente quel telefono; poi "Non raggiungibile". Due test (messaggio a chi è in chiamata, nuovo
  tentativo appena si sente l'altro); i 30 test passano con 120 semi;
- telefono e ripetitore compilano con ESP-IDF 5.5 senza avvisi nel nostro codice;
- l'interfaccia è stata provata sul PC con una scheda e una rete finte (LVGL con
  AddressSanitizer): tutte le schermate si aprono e si usano con i tasti senza errori di
  memoria.

**Da provare sull'hardware** (scritto dai datasheet e dallo schema, ma non ancora acceso su
una PCB vera):

1. **display**: sequenza di inizializzazione dell'ST7701S e tempi RGB (`scheda/src/display.cpp`,
   pclk 14 MHz). Se l'immagine è spostata o sfarfalla, si regolano porch e pclk;
2. **tastiera**: mappa righe/colonne del TCA8418 (`scheda/src/tastiera.cpp`). Il monitor
   seriale stampa il nome di ogni tasto premuto: se un tasto è sbagliato, basta scambiarlo
   nella tabella;
3. **radio**: TCXO a 1,8 V su DIO3 e commutatore d'antenna su DIO2 (come sui moduli con
   SX1262). All'avvio il log dice se la radio risponde;
4. **audio**: volume dell'altoparlante e sensibilità del microfono (guadagni in
   `scheda/src/audio.cpp`) e qualità di Codec2 700C;
   **chiamate**: altoparlante sempre acceso durante la chiamata (`streamStart`), microfono e
   altoparlante insieme sullo stesso I2S della PCB, tempo di Codec2 per decodificare 10–12 frame,
   margine contro i ritardi (`CALL_JITTER_MS` in `app/voce.cpp`) e ritardo vero dalla bocca
   all'orecchio. Nel simulatore la radio passa dalla ricezione alla trasmissione all'istante: sulla
   radio vera ci vogliono alcuni millisecondi, ed è per questo che con 2 o più ripetitori i
   pacchetti sono da 480 ms;
5. **GPS**: velocità della seriale (9600) e frasi NMEA;
6. **batteria**: indicatore MAX17048. Il segnale di carica ora viene dalla variazione misurata
   (`Batteria::charging()` in `scheda/src/batteria.cpp`: sì sopra 1 %/h, no sotto 0,2 %/h).
   Avviso e spegnimento per batteria scarica in `checkBattery()` (`telefono/main/main.cpp`):
   da controllare che la percentuale del MAX17048 sia giusta vicino allo 0%;
7. **accensione (rev. 1.1)**: autoritenuta con PWR_HOLD, tasto PWR letto attraverso D8,
   spegnimento vero. All'avvio il log scrive `avvio: tasto`, `riavvio` o `risveglio`;
8. **SPI**: SCK e MOSI con la corrente d'uscita più bassa (`scheda::spiLowDrive()`), insieme
   alle resistenze da 47 Ω R56/R57. Se la radio desse errori di comunicazione, si porta a
   `GPIO_DRIVE_CAP_2` (il valore normale).

**Ancora da fare:**

- **risparmio di energia sulla PCB**: sul T-Deck è fatto (vedi "Risparmio di energia", da provare);
  sulla PCB a schermo spento restano accesi la radio in ricezione (ok) ma anche il bus RGB del
  display. Si possono riprendere le stesse cose (frequenza variabile, light sleep coi pin della scheda
  che restano com'erano, DIO1 a livello) fermando il display RGB. Si misura sulla PCB vera;
- il **programma di amministrazione dei ripetitori** (a parte, come deciso): i tipi di
  pacchetto 0x30–0x3F sono già riservati e il ripetitore li passa a una funzione apposita;
- **ripetitori su Heltec WiFi LoRa 32 V3** (in casa) **e LilyGO T3-S3** (solari, fuori): il
  firmware è scritto dagli schemi dei produttori e compila, ma non è ancora stato provato sulle
  schede vere. Da provare: radio (TCXO e commutatore d'antenna), schermo (orientamento, se serve
  `schermo gira`), tasto, console (UART col CP2102 sulla Heltec), tensione della batteria e sonno
  per batteria scarica sulla T3-S3, consumi. Poi, per i ripetitori solari, il light sleep
  dell'ESP32-S3. La scheda con l'E22-900M30S (1 W) resta una proposta (`ripetitore/HARDWARE.md`).

## Licenze

Il firmware Pippaphone **non è open source**. Contiene però librerie di terzi con le loro
licenze (i file di licenza sono nelle rispettive cartelle):

| Libreria | Licenza |
|---|---|
| LVGL, RadioLib | MIT: basta conservare l'avviso di copyright |
| Monocypher | BSD-2 / CC0 |
| Atkinson Hyperlegible, IBM Plex Mono, Font Awesome Free | SIL Open Font License 1.1 |
| **Codec 2** | **GNU LGPL 2.1** |
| minimp3 (lettore musicale del T-Deck) | CC0 (pubblico dominio) |
| esptool-js (solo nella pagina per caricare il firmware, non nel firmware) | Apache 2.0 |
| MapLibre GL JS (solo nella pagina per caricare il firmware, per disegnare le mappe) | BSD-3 |
| firmware della tastiera del T-Deck (`strumenti/tastiera_c3`, deriva dall'esempio LilyGO) | MIT: l'avviso è in cima al file |

Le mappe che la pagina disegna usano i dati di **OpenStreetMap** (© OpenStreetMap contributors,
licenza ODbL) con gli stili di OpenFreeMap: chi distribuisce le tile deve citare OpenStreetMap.

**Attenzione a Codec 2.** Se distribuisci il firmware compilato ad altri, la LGPL chiede di
permettere a chi lo riceve di sostituire Codec 2 con una sua versione modificata: si fa
fornendo, insieme al firmware, i file oggetto del resto del programma (o i sorgenti) per
ricollegarlo, oltre ai sorgenti di Codec 2. Per l'uso privato non serve nulla.
