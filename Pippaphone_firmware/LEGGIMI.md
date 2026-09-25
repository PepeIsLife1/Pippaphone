# Pippaphone: firmware

Firmware del telefono LoRa **Pippaphone** (PCB in `Pippaphone pcb/`) e dei **ripetitori** della
rete PippaNet: LoRa a 868 MHz con SF10, messaggi cifrati end-to-end, gruppi, canale globale,
walkie-talkie, posizione su richiesta e SOS.

> La cartella si chiama `Pippaphone_firmware` (senza spazio) perché ESP-IDF non compila in
> percorsi che contengono spazi.

## Cartelle

```
Pippaphone_firmware/
├── telefono/            firmware del telefono (ESP32-S3R8, display 480×480, tastiera)
│   ├── main/app/        rete, dati, vocali, aggiornamento via Wi-Fi
│   └── main/ui/         interfaccia grafica (LVGL): schermate, widget, font
├── ripetitore/          firmware del ripetitore (+ HARDWARE.md: scheda proposta)
├── componenti/
│   ├── pippanet/        protocollo di rete (indipendente dall'hardware, testato sul PC)
│   ├── pippa_radio/     SX1262 con RadioLib, CAD prima di trasmettere, cambio canale
│   ├── scheda/          driver della PCB: expander AW9523B, tastiera TCA8418, MAX17048,
│   │                    display ST7701S, audio I2S, GPS AT6558R
│   ├── lvgl/            LVGL 9.2.2 (MIT)
│   ├── RadioLib/        RadioLib 7.7.1 (MIT)
│   ├── monocypher/      Monocypher 4.0.2 (BSD-2 / CC0)
│   └── codec2/          Codec 2, modo 700C (LGPL 2.1)
├── test/                test del protocollo sul PC (rete simulata)
├── strumenti/           generazione dei font (Atkinson Hyperlegible, IBM Plex Mono, Font Awesome)
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
idf.py menuconfig                  # "Pippaphone ripetitore": PCB del telefono oppure scheda E22
idf.py build flash monitor
```

La porta USB-C della PCB è collegata alla USB interna dell'ESP32-S3: non servono adattatori.
Se il chip non entra in programmazione, tieni GPIO0 a massa (punto di test **TP6**) mentre
colleghi il cavo.

### Partizioni del telefono (flash da 16 MB)

| Partizione | Dimensione | Uso |
|---|---|---|
| `nvs` | 24 kB | chiavi, numero, certificato, impostazioni |
| `ota_0`, `ota_1` | 4 MB + 4 MB | firmware (due copie per l'aggiornamento via Wi-Fi) |
| `dati` | 7,9 MB (FAT) | rubrica, conversazioni, gruppi, certificati, vocali |

Il firmware occupa circa 2,2 MB. Se un aggiornamento non parte, al riavvio si torna da soli
alla versione precedente.

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
   riavvia
   ```

   I ripetitori non hanno bisogno di chiavi: ripetono pacchetti che non possono leggere.

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
| **PWR** | schermo acceso/spento; tenuto premuto: spegni |
| **SYM** | lettere accentate e simboli |
| **ALT** | numeri e punteggiatura (due volte: blocca) |
| **SHIFT** | maiuscola (due volte: tutto maiuscolo) |

Da una chat: su/giù scorre i messaggi, OK apre un vocale o una posizione, PTT registra un
vocale, MENU mostra le opzioni (chiedi la posizione, walkie-talkie, scheda contatto...).

## Parametri radio

| | Messaggi (canale principale) | Vocali (canale voce) |
|---|---|---|
| Frequenza | 869,525 MHz | 869,850 MHz |
| Banda | 125 kHz | 250 kHz |
| SF / CR | 10 / 4:5 | 10 / 4:5 |
| Sync word | 0x6C (rete privata) | 0x6C |
| Potenza | fino a 22 dBm, **automatica** sul telefono | 22 dBm |

Tempi in aria indicativi: SMS di 160 caratteri circa 1,7 s, conferma di consegna circa 0,6 s,
vocale di 15 secondi circa 7 pacchetti da 1,2 s per ogni salto.

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
./build-test/pippanet_test                  # 20 test
PN_SEED=7 ./build-test/pippanet_test        # altro seme casuale
PN_TRACE=1 ./build-test/pippanet_test       # stampa ogni trasmissione
```

## Stato del lavoro

**Fatto e verificato:**

- protocollo PippaNet completo: SMS con conferme di consegna e lettura, "non raggiungibile",
  gruppi fino a 16 persone, canale globale firmato, certificati firmati dall'amministratore,
  posizione con conferma, SOS ripetuto ai contatti, vocali sul canale voce con pezzi mancanti
  richiesti di nuovo, beacon ogni 5 minuti, assegnazione del numero via radio. I 20 test
  passano con 40 semi diversi (reti con pacchetti persi e nodi nascosti);
- telefono e ripetitore compilano con ESP-IDF 5.5 senza avvisi nel nostro codice.

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
5. **GPS**: velocità della seriale (9600) e frasi NMEA;
6. **batteria**: indicatore MAX17048 e segnale di carica.

**Ancora da fare:**

- **risparmio di energia**: ora a schermo spento resta accesa la radio in ricezione (ok) ma
  anche il bus RGB del display. Il prossimo passo è fermare il display e usare il light sleep
  dell'ESP32-S3, svegliandosi con l'interruzione della radio o dei tasti. Si misura sulla PCB
  vera;
- il **programma di amministrazione dei ripetitori** (a parte, come deciso): i tipi di
  pacchetto 0x30–0x3F sono già riservati e il ripetitore li passa a una funzione apposita;
- la **scheda del ripetitore** (proposta in `ripetitore/HARDWARE.md`).

## Licenze

Il firmware Pippaphone **non è open source**. Contiene però librerie di terzi con le loro
licenze (i file di licenza sono nelle rispettive cartelle):

| Libreria | Licenza |
|---|---|
| LVGL, RadioLib | MIT: basta conservare l'avviso di copyright |
| Monocypher | BSD-2 / CC0 |
| Atkinson Hyperlegible, IBM Plex Mono, Font Awesome Free | SIL Open Font License 1.1 |
| **Codec 2** | **GNU LGPL 2.1** |

**Attenzione a Codec 2.** Se distribuisci il firmware compilato ad altri, la LGPL chiede di
permettere a chi lo riceve di sostituire Codec 2 con una sua versione modificata: si fa
fornendo, insieme al firmware, i file oggetto del resto del programma (o i sorgenti) per
ricollegarlo, oltre ai sorgenti di Codec 2. Per l'uso privato non serve nulla.
