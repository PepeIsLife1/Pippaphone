# Tastiera del T-Deck a basso consumo (ESP32-C3)

La tastiera del T-Deck e del T-Deck Plus ha un suo microcontrollore, un ESP32-C3, alimentato insieme a radio,
display e GPS: il telefono non lo puo' spegnere. Col firmware di LilyGO resta sempre sveglio a 80 MHz e legge
la matrice dei tasti di continuo, anche a schermo spento: si stimano 8–13 mA, piu' di tutto il resto del
telefono quando dorme.

Questo firmware (`Keyboard_ESP32C3_Pippaphone/`) parla lo stesso protocollo di quello di LilyGO (I2C 0x55,
modo raw e modo a caratteri, luce della tastiera, ALT+B, ALT+C), quindi va anche con altri firmware
dell'ESP32-S3. In piu':

- gira a 40 MHz e legge la matrice in circa 1 ms ogni 8 ms (prima 35 ms di fila), con un filtro dei rimbalzi;
- col firmware del Pippaphone sull'S3 (dalla versione con questo file) passa al **modo a basso consumo**:
  quando non si tocca niente va in light sleep, lo sveglia un tasto, e alza la linea `C3_INT` (GPIO 8 del C3,
  GPIO 46 dell'S3 sullo schema LilyGO) per dire all'S3 di leggere. Cosi' anche l'S3, a schermo spento, smette
  di interrogarlo 25 volte al secondo e dorme.

Il telefono lo riconosce da solo all'accensione: nel monitor seriale compare
`tastiera pronta: firmware del C3 del Pippaphone (versione 1), modo raw e C3_INT`, e `info.txt` sulla
microSD dice `tastiera: firmware del C3 del Pippaphone`. Col firmware di LilyGO il telefono funziona lo
stesso: a schermo spento legge la tastiera ogni 40 ms, e il C3 resta sempre sveglio.

## Cosa serve

- un adattatore USB-seriale a **3,3 V** (CP2102, CH340, FT232...): il C3 non e' collegato alla USB-C del
  T-Deck;
- per caricarlo **dal browser** (il modo piu' semplice): la pagina del flasher (`strumenti/flasher`),
  preparata su un computer con PlatformIO, e Chrome o Edge;
- oppure PlatformIO (stessa piattaforma e scheda di LilyGO, `platformio.ini` qui accanto) o l'IDE di
  Arduino con le schede ESP32 di Espressif (2.0.17 come LilyGO, oppure 3.x).

PlatformIO si installa con l'estensione PlatformIO IDE di VS Code oppure con `pipx install platformio`,
meglio non dentro l'ambiente Python di ESP-IDF. La prima compilazione scarica da internet la piattaforma
per l'ESP32-C3.

## Collegare la tastiera

Il connettore a 6 piedini accanto al tasto RST, dall'alto (dal lato del tasto RST), e':
**TX, RX, BOOT, RST, GND, VCC** (README di LilyGO). Collegamenti:

| Adattatore | T-Deck |
|---|---|
| RX | TX |
| TX | RX |
| GND | GND |

VCC no: la tastiera prende la corrente dal T-Deck acceso (gliela da' l'ESP32-S3), quindi il T-Deck va
acceso normalmente, a batteria o col cavo USB-C, senza premere il trackball.

1. T-Deck **spento** (interruttore laterale) e adattatore collegato come sopra.
2. Metti a massa **BOOT** (un filo tra BOOT e GND), poi accendi il T-Deck: il C3 parte nel modo di
   programmazione. Se il T-Deck era gia' acceso: con BOOT a massa, tocca un attimo RST con un altro filo
   collegato a GND.
3. Carica: dal browser o con PlatformIO, qui sotto.
4. Togli il filo tra BOOT e GND e spegni e riaccendi il T-Deck: cosi' riparte anche l'S3, che riconosce
   il firmware della tastiera all'accensione.

## Caricare dal browser

1. Prepara la pagina sul computer con PlatformIO: `python strumenti/flasher/prepara.py` (vedi "Caricare
   dal browser" nel `LEGGIMI.md` del firmware) compila anche la tastiera. Nella pagina, sotto
   "Tastiera del T-Deck", deve esserci "firmware versione 1".
2. Apri `build-flasher/pippaphone_flasher.html` con Chrome o Edge, scegli **Tastiera del T-Deck** e
   **Firmware del Pippaphone**, e conferma l'antenna (il T-Deck acceso trasmette).
3. Collega la tastiera come sopra, premi **Collega e carica** e scegli la porta dell'adattatore. La
   pagina controlla che dall'altra parte ci sia un ESP32-C3 (e non, per esempio, la USB-C del T-Deck).
4. Alla fine togli il filo da BOOT e spegni e riaccendi il T-Deck: la console della pagina, che si apre
   da sola sull'adattatore, deve scrivere `Tastiera Pippaphone pronta`. Se invece la ROM scrive
   `waiting for download`, il C3 e' ancora nel modo di programmazione e la pagina lo dice.

## Caricare con PlatformIO o l'IDE di Arduino

```sh
cd Pippaphone_firmware/strumenti/tastiera_c3
pio run -t upload --upload-port /dev/ttyUSB0      # su Windows: --upload-port COM7
```

Con l'IDE di Arduino: apri `Keyboard_ESP32C3_Pippaphone/Keyboard_ESP32C3_Pippaphone.ino`, scheda
**ESP32C3 Dev Module**, *USB CDC On Boot: Disabled* (i piedini USB del C3 sono righe della tastiera),
*Flash Mode: DIO*, poi Carica.

`pio run` scrive anche `.pio/build/tastiera/flasher_args.json` (lo script `scrivi_flasher_args.py`): i
file e gli indirizzi che servono alla pagina del flasher, gli stessi di `pio run -t upload`.

## Tornare al firmware di LilyGO

Il file e' `T-Keyboard_Keyboard_ESP32C3_250620.bin`, nella cartella `firmware` del repository
Xinyuan-LilyGO/T-Deck. Dalla pagina del flasher: **Tastiera del T-Deck**, **Da un file .bin**, scegli il
file e carica come sopra (la pagina controlla che sia un firmware completo per l'ESP32-C3). Oppure con
esptool: `esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0
T-Keyboard_Keyboard_ESP32C3_250620.bin`.

## Da provare (non e' ancora stato acceso su un T-Deck vero)

1. Tutti i tasti, SYM, ALT, i due SHIFT, la luce della tastiera (anche ALT+B) e il lampeggio degli avvisi a
   schermo spento.
2. A schermo spento: un tasto riaccende lo schermo come prima. Se un tasto breve a volte non lo riaccende,
   `RESTA_SVEGLIO_MS` e `SCANSIONE_MS` sono in cima al file.
3. Se la tastiera desse errori o tasti sbagliati, prima prova `CPU_MHZ = 80` (come LilyGO).
4. Il consumo a schermo spento, prima e dopo (vedi "Risparmio di energia" nel `LEGGIMI.md` del firmware).

**Una cosa da sapere.** Il GPIO 46 dell'S3 e' anche uno strapping: se fosse alto mentre l'S3 parte col
trackball premuto (programmazione), l'S3 non si avvierebbe. Per questo il C3 tiene `C3_INT` alta solo dopo un
tasto e al massimo 50 ms; il modo a basso consumo lo accende il telefono, e dopo ogni riavvio del C3 la linea
resta libera come col firmware di LilyGO.

## Licenza

Il file deriva da `examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino` del repository Xinyuan-LilyGO/T-Deck
(Copyright (c) 2023 Shenzhen Xin Yuan Electronic Technology Co., Ltd), con licenza MIT: l'avviso e il testo
della licenza sono in cima al file e vanno conservati.
