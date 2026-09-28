# Q-Mesh rev. 1.1: cosa comprare

I prezzi sono quelli visti il 24 settembre 2026, le disponibilità dei componenti JLCPCB/LCSC quelle del 27 settembre 2026 (quelle delle tre parti nuove della rev. 1.1 il 28 settembre): ricontrollale prima di ordinare.

## 1. Montaggio a mano: PCB da JLCPCB, componenti da LCSC (2 telefoni)

**PCB nude e stencil da JLCPCB.** Carica `qmesh-main-gerber.zip` e `qmesh-kbd-gerber.zip` con le impostazioni di `PRODUZIONE.md`, **senza** *PCB Assembly*, e aggiungi uno **stencil per lato** di ogni scheda (4 in tutto). Usa i gerber di questa versione (rev. 1.1) per **tutte e due** le schede: le modifiche sono elencate in `PRODUZIONE.md`.

**Componenti da LCSC.** Nella cartella `ordine_LCSC_montaggio_a_mano/`:

- `Q-Mesh-LCSC-BOM-2telefoni.xlsx` (o `.csv`): caricala nel **BOM Tool** di LCSC. La colonna `LCSC Part Number` fa prendere esattamente il pezzo giusto, senza abbinamenti automatici. Le quantità sono per 2 telefoni più le scorte (20% o almeno 5 pezzi per i passivi, un pezzo in più per integrati e connettori). LCSC le arrotonda al minimo d'ordine.
- `Customer Part Number` elenca dove va ogni pezzo (`PCB1:` scheda principale, `PCB2:` tastiera): LCSC lo stampa sulle bustine.
- 81 righe, con stock LCSC controllato il 27 settembre 2026 (le tre righe nuove della rev. 1.1 il 28). Unica riga senza stock: **C13594** (33 nF 0402, C70), esaurito su LCSC; va bene qualsiasi 33 nF 0402 X7R da 10 V in su, lo dice la colonna `Note`. In 5 righe il codice è diverso da quello della BOM JLCPCB perché su LCSC l'altro era esaurito (l'ultima è R49: 150 Ω YAGEO C137918 al 5% al posto di C25082); anche questo è nella colonna `Note`.

**Due pezzi fuori da LCSC** (`Q-Mesh-fuori-LCSC.csv`), da [Mouser](https://www.mouser.it) o [DigiKey](https://www.digikey.it):

| Rif. | Parte | Quantità | Perché |
|---|---|---|---|
| AE2 | Antenova **SR4G013** | 3 | LCSC non la vende, e nessuna antenna LCSC entra nella stessa impronta |
| U5 | Espressif **ESP32-S3R8** | 3 | Esaurito su LCSC. Se torna disponibile (C2913194), prendilo lì |

### Alternativa: montaggio da JLCPCB

| Scheda | File da caricare | Impostazioni |
|---|---|---|
| **PCB 1 (principale)** | `qmesh-main-gerber.zip`, `qmesh-main-BOM-JLCPCB.csv`, `qmesh-main-CPL-JLCPCB.csv` | 4 strati, 1,0 mm, stackup **JLC04101H-7628**, controllo d'impedenza, montaggio su **due lati** |
| **PCB 2 (tastiera)** | `qmesh-kbd-gerber.zip`, `qmesh-kbd-BOM-JLCPCB.csv`, `qmesh-kbd-CPL-JLCPCB.csv` | 2 strati, 0,6 mm, montaggio su due lati |

Nelle BOM JLCPCB solo l'antenna AE2 resta in Global Sourcing. Righe da tenere d'occhio: U8 **C7434807** e U9 **C534203** vanno spesso in *shortfall* (si comprano con *Pre-order* da LCSC; per U9 non usare C1854968, che JLCPCB propone da solo); U5 ESP32-S3R8 **C2913194** è esaurito su LCSC ma JLCPCB ne ha nel suo magazzino. C70 **C13594** (33 nF) è una parte *extended*: se JLCPCB non ne ha, sostituiscila nel pannello BOM con un altro 33 nF 0402 X7R.

### Parti cambiate per trovarle a magazzino

Stessa impronta, prestazioni uguali o migliori:

| Rif. | Prima | Ora | Perché va bene |
|---|---|---|---|
| FL1 | RF360 B39162B9416K610 (introvabile) | RF360 **B39162B4327P810** (C2650956) | Stessa custodia 1,4 × 1,1 e stessi piedini (1 e 4 segnale, 2/3/5 massa). Passa anche GLONASS e BeiDou, che l'AT6558R riceve |
| L1 | FNR252012S2R2MT | cjiang **FTC252012S2R2MBCA** (C5832372) | 2,2 µH, 2520, Isat 3,8 A, 55 mΩ (l'originale aveva 216 mΩ) |
| L12 | FNR252012S100MT | cjiang **FTC252012S100MBCA** (C5832376) | 10 µH, 2520, Isat 1,6 A |
| FB2, FB3 | ferrite 1 kΩ | Murata **BLM15PX601SN1D** (C160977) | 600 Ω @ 100 MHz ma **900 mA**: le ferrite da 1 kΩ in 0402 reggono al massimo 350 mA, poche per l'altoparlante |
| R5 | 46,4 kΩ | **47 kΩ** (C25792) | Timer di sicurezza della carica 6,3 h invece di 6,2 h |
| D3–D5 | KT-0402 | XINGLIGHT **XL-1005SURC / UGC / UBC** | LED 0402 rosso, verde, blu |
| D1 | BAT54WS | BAT54WS Yangjie **C699104** (LCSC) / Diodes BAT54WS-7-F C124205 (JLCPCB) | Stesso diodo |
| D2 | B0540WS (Shikues, esaurito) | JSCJ **B0540WS** (C22635) | 40 V, 500 mA |
| U2 | BQ24074RGTR | BQ24074RGTT **C2861252** (LCSC) / RGTR C54313 (JLCPCB) | Stesso chip, bobina diversa |
| U10 | AT6558R | **AT6558R-5N32** (C500608) | Stesso chip nella sigla con cui si vende oggi |
| J4 | U.FL-R-SMT-1 | (10) **C88373** (LCSC) / (80) C88374 (JLCPCB) | Stesso connettore, bobina diversa |
| C1, C59 | 1 µF 0603 | Samsung CL10A105KA8NNNC **C5673** (LCSC) / C15849 (JLCPCB) | 1 µF 25 V X5R |
| Y1 | quarzo 40 MHz generico | SCTF **SX3B40.000F1010F30** (C7302016) | 40 MHz, 10 pF, ±10 ppm, ESR 30 Ω |
| Y2 | TG2520SMN 32M ECGNNM3 | Epson **X1G0054210205** (C7527388) | È ancora un TG2520SMN 32 MHz ±0,5 ppm, alimentabile da 1,7 a 3,63 V |
| Y3 | Epson TG2520SMN 26M | Huaxin **2T026000VY** (C19725361) | TCXO 26 MHz 2520, piedinatura standard (1 NC, 2 GND, 3 uscita, 4 VCC). Il datasheet dà ±1,5 ppm (LCSC ±0,5): il GPS funziona, il primo fix a freddo può essere un po' più lento |
| Y4 | quarzo 32 kHz generico | Seiko **SC-20S 9 pF** (C97603) | 32,768 kHz, 2012. Da 9 pF perché sull'AT6558R il quarzo va senza condensatori esterni (prima era un FC-12M da 12,5 pF) |

### Parti nuove o cambiate nella rev. 1.1 (28/9/2026)

| Rif. | Parte | Codice LCSC | Note |
|---|---|---|---|
| Q2 | Alpha & Omega **AO3401A** (P-MOSFET, SOT-23) | C15127 | Nuovo, circuito di accensione |
| D6, D7, D8 | BAT54WS | come D1 | Nuovi, circuito di accensione (D8: lettura del tasto) |
| R58, R59, R60 | 100 kΩ 0402 | C25741 | Nuovi, circuito di accensione |
| R7 | Murata **NCP15XH103F03RC** (NTC 10 kΩ B3380, 0402) | C77131 | Era una 10 kΩ fissa: ora misura la temperatura vicino alla batteria |
| R9 | – | – | Tolta |
| R49 | 150 Ω 0402 | C25082 (JLCPCB) / C137918 (LCSC) | Nuova, LED rosso. C25082 esaurito su LCSC il 28/9 |
| R50, R51 | 100 Ω 0402 | C25076 | Nuove, LED verde e blu |
| R52 | 330 Ω 0402 | C25104 | Nuova, LED di carica |
| R53–R57 | 47 Ω 0402 | C25118 | Nuove, in serie al bus SPI |
| R48 | 220 Ω 0402 | C25091 | Nuova, uscita del TCXO LoRa |
| R17 | 0 Ω 0402 | C17168 | Era 220 Ω |
| C28 | 100 nF 0402 | C1525 | Era 10 nF |
| JP3 | ponticello a stagno | – | Solo rame, non si compra |
| PCB 2: R2–R9 | 220 Ω 0402 | C25091 | Erano 330 Ω |

### Sezione GNSS rifatta (27/9/2026)

| Rif. | Parte | Codice LCSC | Note |
|---|---|---|---|
| U15 | ZHONGKEWEI **AT2659** (LNA GNSS, DFN-6 1,5 × 1,0) | C92450 | Nuovo |
| FL2 | RF360 **B39162B4327P810** | C2650956 | Secondo SAW, prima dell'LNA: lo protegge da LoRa e Wi-Fi |
| L11 | Murata **LQG15HS6N8J02D** 6,8 nH | C77110 | Nuovo, adattamento d'ingresso dell'LNA |
| C70 | Samsung **CL05B333KO5NNNC** 33 nF | C13594 | Nuovo. Esaurito su LCSC al 27/9: va bene qualsiasi 33 nF 0402 X7R |
| C71 | 1 µF 0402 | C52923 | Nuovo |
| R44 | 10 Ω 0402 | C25077 | Nuovo, filtro sull'alimentazione dell'LNA |
| R45, R46 | 0 Ω 0402 | C17168 | Nuovi (R46 alimenta il DC-DC interno; R47 resta DNP) |
| L8 | Murata **LQM18PN2R2MFRL** 2,2 µH | C337910 | Era 4,7 µH |
| C36, C37 | 4,7 µF 0603 | C19666 | Erano 10 µF e 1 µF |
| C46 | 100 nF 0402 | C1525 | Era 10 nF |
| R19, C47 | – | – | Tolti |

## 2. Da comprare a parte e montare a mano

| Cosa | Specifica | Dove |
|---|---|---|
| **Display** | Adafruit **5825**, TL034WVS05-B1477A, 3,4" 480 × 480 senza touch (70,45 × 70,32 × 1,89 mm, FPC 40 poli) | [Adafruit](https://www.adafruit.com/product/5825) ($29,95), [The Pi Hut](https://thepihut.com/products/square-rgb-ttl-tft-display-3-4-480x480-no-touchscreen-tl034wvs05-b1477a), [Jameco](https://www.jameco.com/z/5825-Adafruit-Industries-Square-RGB-TTL-TFT-Display-3-4-480X480-No-Touchscreen-Tl034Wvs05-B1477A_2527143.html) |
| **Batteria** | LiPo **505060**, 3,7 V, ~2000 mAh, con circuito di protezione. Al massimo 51 × 60,4 × 5,0 mm | Per esempio [questa su Amazon](https://www.amazon.com/505060-2000mAh-Lithium-Rechargeable-Connector/dp/B0C2KNTVS9) (arriva con JST PH 2,0) |
| **Connettore batteria** | Cavetto **JST SH 1,0 mm 2 poli** già crimpato, da saldare al posto del PH. Su J2 il **pin 1 è il +** (VBAT), il pin 2 è GND: controlla la polarità prima di collegare | [Amazon](https://www.amazon.com/SH1-0mm-Connector-Female-200mm-Extension/dp/B0FH4QRHH4) |
| **Altoparlante** | **1511**, 15 × 11 × 3,5 mm, 8 Ω, 1 W, con fili | [Amazon](https://www.amazon.com/Speaker-Loudspeaker-Rectangle-Tablet-15x11x3-5MM/dp/B07JJL9H2N) |
| **Motorino vibrazione** | A moneta **0820** (Ø 8 × 2 mm), 3 V, con fili | [Precision Microdrives C0820](https://precisionminidrives.com/product/8mm-coin-vibration-motor-2mm-type-model-nfp-c0820), [Vybronics VC0820B006F](https://www.vybronics.com/coin-vibration-motors/with-brushes/v-c0820b006f) |
| **Pigtail antenna** | **SMA femmina da pannello** → **U.FL**, cavo 1,13 mm, 5 cm, esagono 8 mm. Deve essere **SMA** e non RP-SMA (le antenne LoRa sono quasi tutte SMA maschio) | [Amazon, 2 pezzi](https://www.amazon.com/Connector-Pigtail-Female-Bulkhead-Extension/dp/B01MV02FLK) |
| **Antenna LoRa** | **868 MHz**, SMA maschio. Stilo corto o a gomma | [Passion Radio 868 stubby](https://www.passion-radio.com/ism/868-stubby-882.html), [Kiwi Electronics 2,8 dBi](https://www.kiwi-electronics.com/en/2-8dbi-lora-antenna-sma-male-868mhz-195mm-20047) |
| **Cavo tastiera** | FFC **12 poli, passo 0,5 mm, tipo A** (contatti sullo stesso lato), lunghezza ~22 mm. Misura sulla scocca stampata prima di ordinare | Qualunque venditore di FFC 0,5 mm; le misure standard partono spesso da 30 mm |
| **Inserti filettati** | M2, Ø esterno 3,2 mm, lunghi 3 mm, in ottone (4 pezzi) | [Amazon](https://www.amazon.com/M2x3mmx3-2mm-Thread-Insert-Embedment-Knurled/dp/B0949M68PQ) |
| **Viti** | M2 × 10 a testa cilindrica (2) e M2 × 3 (2) | Ferramenta o Amazon |
| **Lente** | PMMA trasparente 66 × 66 × 1 mm, taglio laser da `Lente_66x66_acrilico_1mm.dxf` | Qualunque servizio di taglio laser |
| **Scocca** | Resina SLA (meglio una resina tenace), file della cartella `scocca` | JLC3DP |
| **microSD** | Qualunque, da 4 GB in su | – |

## 3. Da controllare quando arrivano i pezzi

1. **Contatti del flat del display.** J5 è un Hirose FH12**A** (contatti in alto): va bene se, tenendo il display con lo schermo verso di te, i contatti dorati del flat sono sul **retro**. Il flat fa una piega a Z nell'asola e arriva a J5 dal lato B. Se invece i contatti sono davanti, serve l'FH12 normale (contatti in basso, stessa impronta, LCSC **C202114**). Nella BOM LCSC ci sono già 2 FH12-40S-0.5SH(55) di scorta: se serve, monti quello al posto dell'FH12A.
2. **Polarità della batteria** sul cavetto JST SH (pin 1 = +).
3. **Esagono dell'SMA** del pigtail: la sede nella scocca è per 8 mm.
