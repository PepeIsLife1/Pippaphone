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
│   └── codec2/          Codec 2, modo 700C (LGPL 2.1)
├── test/                test del protocollo sul PC (rete simulata)
├── strumenti/           generazione dei font (Atkinson Hyperlegible, IBM Plex Mono, Font Awesome),
│   ├── flasher/         pagina per caricare il firmware dal browser (T-Deck e ripetitori)
│   └── simulatore/      l'interfaccia del telefono sul PC (screenshot di tutte le schermate)
│                        e nel browser col T-Deck disegnato (web/)
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

La barra in basso mostra sempre i tasti giusti. Il modo "raw" (tasti premuti e rilasciati) c'è nel
firmware della tastiera (ESP32-C3) dal giugno 2025. Il telefono capisce il modo al primo tasto
premuto e fino ad allora suppone il raw. Con un firmware più vecchio se ne accorge da solo (la barra
in basso si corregge in un paio di secondi) e passa a un **ripiego**: i caratteri arrivano già
composti dal C3 (i numeri con SYM tenuto), il PTT è SHIFT+🎤 premuto una volta per parlare e una per
inviare, MENU è SHIFT+$, CALL è ALT+C, END è SYM+SHIFT+H, l'SOS si annulla tenendo premuto il
trackball, e non ci sono il riquadro degli accenti, il volume dai tasti e la scorciatoia dell'SOS.
Per avere tutto basta aggiornare il firmware del C3 (esempio Keyboard_ESP32C3 di LilyGO, con un
adattatore USB-seriale sui pin accanto al tasto RST). Il monitor seriale e `info.txt` scrivono quale
modo è in uso.

### Differenze dalla PCB

- **Accensione**: con l'interruttore laterale. "Spegni" (e la batteria scarica) spegne le
  periferiche e mette l'ESP32-S3 in sonno profondo; si riaccende con l'interruttore (o RST).
- **Vibrazione**: non c'è il motore; la voce "Vibrazione" sparisce dalle impostazioni.
- **LED di notifica**: lampeggia la luce della tastiera, solo a schermo spento.
- **Batteria**: c'è solo la tensione della cella. La percentuale è stimata dalla curva di una
  LiPo; "in carica" si capisce dal salto di tensione quando si collega il caricabatterie, dalla
  percentuale che sale o dalla USB collegata a un computer.
- **GPS**: u-blox M10Q (38400 baud) oppure Quectel L76K (9600), riconosciuto da solo. Quando non
  serve, l'u-blox va in standby; l'L76K resta acceso (non ha un comando per spegnersi).
- **Display, LoRa e microSD** sono sullo stesso bus SPI.

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

### Da provare sul T-Deck vero

Scritto dagli schemi e dagli esempi di LilyGO, provato nel simulatore ma non ancora su un T-Deck:

1. **display**: orientamento e colori (`SWAP_XY`, `MIRROR_X`, `INVERT` e ordine RGB in
   `scheda/src/tdeck/display_tdeck.cpp`) e i 16 livelli della retroilluminazione;
2. **tastiera**: riconoscimento del modo raw e del ripiego; il monitor seriale stampa ogni tasto
   (colonna, riga, nome) e il modo scelto;
3. **trackball**: sensibilità (`TB_EDGES_PER_STEP` in `tastiera_tdeck.cpp`);
4. **audio**: canale e guadagni del microfono (ES7210, 24 dB + `MIC_DIGITAL_GAIN` in
   `audio.cpp`), volume dell'altoparlante;
5. **GPS**: riconoscimento della velocità e standby dell'u-blox;
6. **batteria**: curva tensione → percentuale e riconoscimento della carica
   (`batteria_tdeck.cpp`);
7. **microSD**: montaggio, copia dei dati dalla memoria interna, mappe;
8. **radio** sul bus SPI condiviso col display;
9. **sonno profondo**: consumo da "spento" e riaccensione con l'interruttore.

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
./build-test/pippanet_test                  # 30 test
PN_SEED=7 ./build-test/pippanet_test        # altro seme casuale
PN_TRACE=1 ./build-test/pippanet_test       # stampa ogni trasmissione
```

### Simulatore dell'interfaccia

`strumenti/simulatore` compila sul PC le schermate vere, i dati veri e LVGL, con una scheda, una
rete e un GPS finti, e salva uno screenshot di ogni schermata (41 per la PCB, 46 per il T-Deck,
mappe e chiamata comprese). Controlla anche che nessuna scritta esca dal suo riquadro. Su Windows serve
MSYS2 (gcc e ninja), su Linux basta gcc:

```sh
cmake -S strumenti/simulatore -B build-sim -G Ninja && cmake --build build-sim
build-sim/sim_tdeck screenshot-tdeck            # 320x240
build-sim/sim_pcb screenshot-pcb                # 480x480
SIM_NOMI_LUNGHI=1 build-sim/sim_tdeck prova     # tutti i nomi lunghissimi
SIM_NOMI_MAX=1 build-sim/sim_pcb prova          # nomi lunghi quanto il massimo che si può scrivere
```

Per controllare che una modifica non cambi l'interfaccia della PCB si compila il simulatore anche
coi sorgenti di prima (`-DUI_DIR=... -DSCHEDA_DIR=...`) e si confrontano gli screenshot: devono
essere identici pixel per pixel.

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

- **risparmio di energia**: ora a schermo spento resta accesa la radio in ricezione (ok) ma
  anche il bus RGB del display. Il prossimo passo è fermare il display e usare il light sleep
  dell'ESP32-S3, svegliandosi con l'interruzione della radio o dei tasti. Si misura sulla PCB
  vera;
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
| esptool-js (solo nella pagina per caricare il firmware, non nel firmware) | Apache 2.0 |

**Attenzione a Codec 2.** Se distribuisci il firmware compilato ad altri, la LGPL chiede di
permettere a chi lo riceve di sostituire Codec 2 con una sua versione modificata: si fa
fornendo, insieme al firmware, i file oggetto del resto del programma (o i sorgenti) per
ricollegarlo, oltre ai sorgenti di Codec 2. Per l'uso privato non serve nulla.
