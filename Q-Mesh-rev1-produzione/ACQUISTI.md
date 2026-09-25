# Q-Mesh rev. 1: cosa comprare

I prezzi e le disponibilità sono quelli visti il 24 settembre 2026: ricontrollali prima di ordinare.

## 1. PCB montate da JLCPCB

| Scheda | File da caricare | Impostazioni |
|---|---|---|
| **PCB 1 (principale)** | `qmesh-main-gerber.zip`, `qmesh-main-BOM-JLCPCB.csv`, `qmesh-main-CPL-JLCPCB.csv` | Vedi `PRODUZIONE.md`: 4 strati, 1,0 mm, stackup **JLC04101H-7628**, controllo d'impedenza, montaggio su **due lati** |
| **PCB 2 (tastiera)** | `qmesh-kbd-gerber.zip`, `qmesh-kbd-BOM-JLCPCB.csv`, `qmesh-kbd-CPL-JLCPCB.csv` | 2 strati, 0,6 mm, montaggio su due lati (tasti sul lato F, TCA8418 sul lato B) |

### Parti che LCSC non ha a magazzino

JLCPCB le compra per te con il **Global Sourcing**: nell'ordine le aggiungi da DigiKey o Mouser e le fai arrivare prima del montaggio. Calcola 1–2 settimane in più.

| Rif. | Parte | Perché serve quella |
|---|---|---|
| U8 | Johanson **0900FM15K0039001E** | Balun, adattamento e filtro dell'SX1262 in un solo pezzo |
| U9 | Infineon **BGS12WN6E6327XTSA1** | Switch TX/RX |
| FL1 | Qualcomm/RF360 **B39162B9416K610** | SAW GPS 1575 MHz. Se non si trova, qualunque SAW 1575 MHz in case 1,4 × 1,1 mm con la stessa piedinatura (1 uscita, 4 ingresso, 2/3/5 massa) |
| AE2 | Antenova **SR4G013** | Antenna chip GNSS |
| Y3 | Epson **TG2520SMN 26.0000M** (TCXO 26 MHz ±0,5 ppm, 2,5 × 2,0 mm) | Riferimento del GNSS. Va bene qualunque TCXO 26 MHz 2520 clipped-sine a 3,3 V con piedinatura standard (1 NC, 2 GND, 3 uscita, 4 VCC) |
| J6 | Molex **104031-0811** | Slot microSD |
| J7 (PCB 1), J1 (PCB 2) | Hirose **FH12-12S-0.5SH(55)** | Connettore del cavo piatto della tastiera |

I codici LCSC già verificati sono nella colonna `LCSC Part #` delle BOM. Per resistenze e condensatori il codice è vuoto: lo strumento BOM di JLCPCB li abbina da solo per valore e contenitore (0402/0603/0805). Controlla gli abbinamenti proposti prima di confermare.

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

1. **Contatti del flat del display.** J5 è un Hirose FH12**A** (contatti in alto): va bene se, tenendo il display con lo schermo verso di te, i contatti dorati del flat sono sul **retro**. Il flat fa una piega a Z nell'asola e arriva a J5 dal lato B. Se invece i contatti sono davanti, serve l'FH12 normale (contatti in basso, stessa impronta, LCSC **C202114**). Per non restare fermi, aggiungi 2 FH12-40S-0.5SH(55) sfusi all'ordine LCSC: costano poco e, se serve, si cambia J5 con l'aria calda.
2. **Polarità della batteria** sul cavetto JST SH (pin 1 = +).
3. **Esagono dell'SMA** del pigtail: la sede nella scocca è per 8 mm.
