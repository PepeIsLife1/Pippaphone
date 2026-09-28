# Q-Mesh rev. 1.1: produzione

## PCB 1, scheda principale (`pcb1_principale/`)

| Opzione | Valore |
|---|---|
| Strati | 4 |
| Dimensioni | 70 × 130,3 mm (contorno con intaglio SMA, asola del flat e 2 fori M2) |
| Spessore | 1,0 mm |
| Stackup | **JLC04101H-7628** (L1–L2 prepreg 7628 0,2104 mm, core 0,5 mm) |
| Controllo d'impedenza | **Sì**. Linee a 50 Ω sul **lato B (L4)**, coplanari con massa sotto (CPWG): pista **0,30 mm**, distanza dalla massa **0,20 mm**, riferimento **In2 (L3)**. Calcolo con risolutore di campo: 49 Ω con solder mask, tolleranza JLC ±10% |
| Rame | 1 oz esterni, 0,5 oz interni (standard dello stackup) |
| Via | diametro 0,45 / foro 0,25 mm, coperte dal solder mask (*tented*). Per le regole JLC un foro 0,25 con diametro ≥ 0,45 non costa di più. Foro–pista ≥ 0,22 mm, foro–foro ≥ 0,25 mm |
| Pista / distanza minima | 0,15 / 0,12 mm |
| Finitura | **ENIG** (consigliata: QFN a passo 0,4 mm e LGA 0,7 × 1,1 mm) |
| Solder mask | a scelta (nera per stare in tema) |
| Montaggio | A mano (vedi sotto) oppure PCBA Standard su due lati. Lato B: quasi tutto. Lato F: J7, C69, D3–D5, D8, R13, R14, R41, R42, R52–R57, R60 |
| File | `qmesh-main-gerber.zip`, `qmesh-main-BOM-JLCPCB.csv`, `qmesh-main-CPL-JLCPCB.csv` |

Strati: F.Cu segnali e colata GND · In1 segnali e colata GND · **In2 piano GND pieno** · B.Cu componenti, RF e colata GND.

## PCB 2, tastiera (`pcb2_tastiera/`)

| Opzione | Valore |
|---|---|
| Strati | 2 |
| Dimensioni | 69,5 × 39,5 mm |
| Spessore | 0,6 mm |
| Finitura | ENIG (i KMR2 e il TCA8418 QFN ringraziano) |
| Montaggio | A mano (vedi sotto) oppure PCBA Standard su due lati. Lato F: 39 KMR2, navigatore 5 vie, 8 LED e le loro resistenze. Lato B: TCA8418, passivi, J1 |
| File | `qmesh-kbd-gerber.zip`, `qmesh-kbd-BOM-JLCPCB.csv`, `qmesh-kbd-CPL-JLCPCB.csv` |

## Cosa cambia nella rev. 1.1 (28/9/2026)

Usa i gerber, le BOM e le CPL di questa versione per **tutte e due** le schede: la tastiera cambia anche lei (alimentazione dei LED).

**PCB 1**

| Zona | Modifica |
|---|---|
| Accensione | Nuovo circuito di autoritenuta: il tasto di accensione (BTN_PWR_N) accende il TPS63001 tramite Q2 (AO3401A), D6 e R58; il firmware lo tiene acceso con PWR_HOLD (AW9523B P1_7) tramite D7. R59 tiene spento il TPS63001 a riposo. JP3 "Sempre acceso" (aperto) scavalca tutto. R37 (pull-up del tasto) ora va a VSYS. L'AW9523B legge il tasto attraverso D8 (BAT54WS) con R60 100 kΩ a +3,3 V (BTN_PWR_SENSE su P1_3) |
| LED di stato | Anodi di D3–D5 su **VSYS** invece che su +3V3, con R49 150 Ω (rosso), R50 e R51 100 Ω (verde, blu). L'AW9523B li pilota in modalità LED (corrente costante) |
| LED di carica | CHG_N non va più all'AW9523B: accende direttamente il LED rosso tramite R52 330 Ω (circa 5 mA durante la carica). Il piedino P1_7 liberato diventa PWR_HOLD |
| Batteria | R7 ora è un **NTC 10 kΩ B3380** (Murata NCP15XH103F03RC), sul lato B al bordo della batteria: il BQ24074 sospende la carica se fa troppo caldo o troppo freddo. R9 tolta |
| TPS63001 | Impaginazione rifatta secondo le regole TI: L1 sui piedini L1/L2, condensatori d'ingresso e d'uscita sui loro piedini e su PGND |
| +3V3 | Tronco sul bordo sinistro allargato da 0,25 a 1,0 mm e parte alta ripulita: caduta massima 104 mV (era 169 mV) |
| microSD | J6 ruotata di 180°: la fessura ora guarda il bordo della scheda e l'apertura della scocca, la scheda si infila da fuori. R53–R55 47 Ω in serie a CLK/CMD/DAT0, pull-up R41/R42 sul lato F |
| Bus SPI | R56 (SCK) e R57 (MOSI) 47 Ω in serie lato ESP32, entrambe sul lato F |
| TCXO LoRa | R17 da 220 Ω a 0 Ω (alimentazione da DIO3), C28 da 10 nF a 100 nF accanto a Y2, R48 220 Ω in serie all'uscita del TCXO |
| Quarzo ESP32 | Y1 spostato accanto ai piedini XTAL dell'ESP32, secondo le regole di layout Espressif: a 2 mm dai piedini, piste solo sul lato B e senza via (XTAL_N 3,8 mm, XTAL_P 4,3 mm; prima 12 e 15 mm con 5 via), R15 vicino al chip, C20 e C21 ai capi del quarzo, massa tutto intorno. Di conseguenza: C15/C18 spostati, C16 sul piedino 56, C11 sul tronco +3V3, pull-down R13/R14 dei piedini di strapping sul lato F |
| Display (J5) | I bit bassi di rosso e blu ripetono i bit alti (R0–R2 = R3–R5, B0–B2 = B3–B5): prima erano tutti su R3/B3 e i livelli di colore non erano uniformi |
| Microfono | U12 spostato di 0,71 mm per stare sul foro della scocca |
| Tastiera (J7) | Il piedino libero J7.2 porta VSYS alla PCB 2 per la retroilluminazione |
| Pulizia | Pad di massa dei pezzi RF collegati pieni alla colata; tolte 26 via e 3 spezzoni di pista appesi; serigrafie sistemate; BQ24074 con via termiche sul pad centrale |

**PCB 2**: gli 8 LED della tastiera sono alimentati da **VSYS** (piedino 11 del flat, prima libero) con R2–R9 da **220 Ω** (erano 330 Ω su +3V3); il +3V3 resta solo al TCA8418. Navigatore SW40: impronta controllata sul disegno Alps (catalogo SKRH, p. 481); le due linguette di fissaggio ora sono pad SMD senza foro, come nel disegno (prima avevano un foro metallizzato da 0,6 mm che risucchiava lo stagno), con una via di massa accanto a ciascuna.

## Montaggio a mano

1. **Ordine PCB**: carica i due gerber con le opzioni qui sopra, **senza** *PCB Assembly*, e aggiungi uno stencil per lato di ogni scheda.
2. **Componenti**: BOM LCSC e i due pezzi da Mouser/DigiKey, come in `ACQUISTI.md`.
3. **Serve**: pasta saldante di tipo 4 o più fine (i QFN a passo 0,4 mm la vogliono fine), piastra riscaldante o fornetto, aria calda, flussante, lente o microscopio.
4. **PCB 1**: stendi la pasta sul **lato B** con lo stencil, posa i componenti seguendo le bustine (`Customer Part Number`) e rifondi sulla piastra. Poi il **lato F**, che ha pochi pezzi: J7, C69, D3–D5, D8, R13, R14, R41, R42, R52–R57, R60. Saldatore o aria calda.
5. **PCB 2**: prima il **lato B** (TCA8418, passivi, J1) sulla piastra, poi il **lato F** (tasti, navigatore, LED, resistenze) con pasta e aria calda, senza insistere sui tasti.
6. **Pezzi delicati**: ESP32-S3 (U5) e AT6558R (U10) hanno i piedini a 0,4 mm, lo switch U9 è 0,7 × 1,1 mm, l'LNA U15 (AT2659) è un DFN-6 da 1,5 × 1,0 mm e i due SAW FL1/FL2 sono 1,4 × 1,1 mm. Nella zona GNSS i componenti sono vicini (0,2 mm tra i corpi): posali con pinzette sottili. Vanno solo con pasta da stencil e rifusione, non a saldatore. Dopo, controlla i ponti con la lente.
7. **Non montare**: i posti segnati DNP (accordo RF), TP, H. JP1 e JP2 sono già chiusi in rame. **JP3** si chiude con una goccia di stagno solo per la prima programmazione e poi si riapre (vedi "Accensione" più sotto). Altoparlante (LS1) e motorino (M1) si saldano con i fili.
8. **Prima accensione**: vedi "Dopo il montaggio" più sotto.

## Montaggio da JLCPCB (alternativa)

1. Carica il gerber, imposta le opzioni qui sopra e attiva *PCB Assembly*.
2. Carica BOM e CPL. Tutte le righe hanno il codice LCSC e si abbinano da sole, tranne l'antenna GPS AE2 in Global Sourcing. Le righe segnate *shortfall* (U8, a volte U9) non sono nel magazzino di montaggio JLCPCB ma sono su LCSC: si comprano con *Pre-order* (vedi `ACQUISTI.md`).
3. Le parti segnate "Global Sourcing" nella colonna Note vanno aggiunte da DigiKey/Mouser nel pannello *Global Sourcing Parts* prima di confermare. L'elenco è in `ACQUISTI.md`.
4. Nell'anteprima di posizionamento controlla il **pin 1** degli integrati (U2–U15, U8, U9, FL1, FL2) e di Q2: JLC a volte ruota di 90° o 180° le parti con impronte diverse dalle sue. Si corregge lì, trascinando.
5. Non montare: R/C/L segnati DNP (posti di accordo RF), TP, H, JP. I jumper JP1/JP2 sono già chiusi in rame (1-2), JP3 resta aperto (si chiude a mano solo per la prima programmazione).
6. Speaker (LS1) e motorino (M1) sono pad per fili: si saldano a mano.

## Cosa è stato verificato

L'esito completo è in `VERIFICHE.txt` e nei report DRC (`*-drc.rpt`).

| Controllo | PCB 1 | PCB 2 |
|---|---|---|
| Reti della PCB uguali allo schema, pad per pad | 205 componenti, 0 differenze | 65 componenti, 0 differenze |
| DRC KiCad: connessioni mancanti | 0 | 0 |
| DRC KiCad: errori | 0 | 0 |
| Linee a 50 Ω (CPWG 0,30 / 0,20 su In2) | 49–52 Ω, tutte sul lato B, gap ≥ 0,20 mm, piano In2 intero sotto tutte | – |
| Caduta su +3V3 nel caso peggiore (668 mA) | 104 mV | – |
| Quarzo Y1 dell'ESP32 (regole Espressif) | a 2,05 mm dai piedini, piste solo sul lato B e senza via, nessun segnale sotto il quarzo sugli strati vicini | – |

Avvisi DRC rimasti sulla PCB 1, tutti valutati e innocui:

- **53 cortili sovrapposti**: 8 nella catena RF LoRa/Wi-Fi e 27 nella sezione GNSS, come nella rev. 1, più 18 nuovi (quarzo Y1, TCXO Y2, TPS63001, circuito di accensione). Nei 18 nuovi tra i corpi restano almeno 0,26 mm e tra pad di reti diverse almeno 0,22 mm: la pick-and-place li monta, a mano servono pinzette sottili e lente. L'elenco è in `VERIFICHE.txt`.
- **3 serigrafie** di libreria tagliate dalla maschera: al massimo JLC le taglia.
- Nessuna via o pista appesa (nella rev. 1 erano 29).

I file sono stati verificati con KiCad 7. Se apri i progetti con una versione più recente, KiCad li converte da solo; lancia una volta il DRC anche lì.

**Antenna GPS AE2** (Antenova SR4G013, antenna "on-ground"): i pad di massa sono collegati a GND, compreso il ponticello del piedino 1, e sotto c'è il piano di massa pieno, come chiede il datasheet.

**Sezione GNSS**: antenna AE2 → π-greco (C48, L9, C49) → SAW FL2 → LNA AT2659 (U15, 470 pF + 6,8 nH in ingresso, 10 Ω + 33 nF + 1 µF sull'alimentazione) → SAW FL1 → π (C51, L10) → RF_IN dell'AT6558R. DC-DC interno dell'AT6558R con L8 2,2 µH; TCXO Y3 alimentato dal suo VX_OUT; quarzo RTC Y4 da 9 pF senza condensatori esterni. Ogni pad di massa della sezione ha la sua via verso i piani, il pad centrale di U10 ne ha 9. Nessuna pista digitale sul lato B nella zona GNSS.

Piedinature controllate sui datasheet: AT6558R, LNA AT2659 (1-2 GND, 3 RFIN, 4 VDD, 5 SHDN attivo alto, 6 RFOUT), IPD Johanson (impronta ufficiale KiCad), switch BGS12WN6 (tabella di verità), SAW B39162B4327P810 (1 e 4 segnale, 2/3/5 massa), antenna SR4G013 (piedino 1 a massa col ponticello), display (tabella pin del TL034WVS05), BQ24074 (ITERM/TMR/TS), AO3401A (1 G, 2 S, 3 D), AW9523B (piedini fino a 6 V anche con VCC più bassa: il pull-up del tasto a VSYS va bene). Navigatore Alps SKRHABE010 della PCB 2: pad, fori guida (ø0,75 in alto, ø1,05 in basso, a 3,8 mm), linguette e numerazione dei piedini uguali al disegno Alps.

## Dopo il montaggio

- **Reti di adattamento RF** (LoRa, Wi-Fi, GNSS): i valori sono quelli di partenza. Con un VNA si accordano i π-greco verso le antenne; i posti DNP sono lì per questo. Senza VNA funziona comunque, con qualche dB di margine in meno.
- **Prima accensione**: alimenta da USB senza batteria e, **senza toccare il tasto**, controlla che VSYS su TP2 ci sia e che TP3 stia a 0 V con i LED spenti (GND su TP5). Poi **premi il tasto di accensione**: TP3 va a 3,3 V finché lo tieni premuto; con l'ESP32 ancora vuoto, quando lo lasci torna a 0 V (vedi "Accensione"). Controlla la corrente a riposo, poi collega la batteria (VBAT su TP4).

## Accensione (hardware)

- **Accendere**: il tasto porta a massa BTN_PWR_N, D6 porta giù il gate di Q2, Q2 dà VSYS all'EN del TPS63001 e parte il 3,3 V.
- **Restare acceso**: mentre il tasto è premuto il firmware porta a 1 PWR_HOLD (P1_7), che tramite D7 tiene alto l'EN; da lì il tasto si può lasciare. L'AW9523B legge il tasto su P1_3 attraverso D8: dopo il reset tutte le sue porte sono uscite a 0 (AD0 = AD1 = GND, tabella 1 del datasheet), ma D8 impedisce che P1_3 a 0 faccia da "tasto premuto" e tenga acceso il telefono da solo.
- **Prima programmazione**: con l'ESP32 vuoto nessuno alza PWR_HOLD e il 3,3 V c'è solo col tasto premuto. Chiudi **JP3** con una goccia di stagno, programma via USB, poi riaprilo con la treccia dissaldante (oppure tieni premuto il tasto per tutta la programmazione).
- **Spegnere**: il firmware porta PWR_HOLD a 0 con P1_3 in ingresso. Se il tasto è ancora premuto il telefono resta acceso finché non lo lasci.
- **JP3 "Sempre acceso"**: chiuso con una goccia di stagno, tiene l'EN del TPS63001 sempre a VSYS e scavalca il circuito. Con JP3 chiuso il telefono non si spegne più dal tasto.
- A telefono spento il circuito di accensione non consuma: D8 separa il tasto (a VSYS tramite R37) dall'AW9523B spento. I LED invece sono collegati tra VSYS e i piedini P0 dell'AW9523B: il datasheet ammette 6 V sui piedini qualunque sia VCC e nello schema tipico collega i LED alla batteria, quindi da spento non devono accendersi. Il controllo "TP3 a 0 V e LED spenti" della prima accensione lo verifica.

## Note per il firmware

- **AW9523B all'avvio, per prima cosa, mentre il tasto è ancora premuto** (dopo il reset tutte le porte sono uscite a 0):
  1. `Output_Port1` (0x03): P1_7 = 1 (PWR_HOLD alto: da qui il telefono resta acceso anche a tasto lasciato), P1_2 = 1 (SD_CS_N alto: la microSD è selezionata finché resta a 0), P1_1 = 1 (LCD_CS_N), P1_0 come serve al reset del display.
  2. `Config_Port1` (0x05): P1_3–P1_6 in **ingresso** (tasti accensione, PTT, volume). P1_3 legge BTN_PWR_SENSE: 0 = tasto premuto (R60 fa da pull-up, D8 porta giù il livello a circa 0,2 V). Non mettere mai P1_4–P1_6 in uscita alta: con il tasto premuto sarebbe un corto (P1_3 è protetto da D8).
  3. `LED mode switch` (0x12): P0_0–P0_3 in modalità LED (bit a 0). Fino a quel momento i LED di stato e la retroilluminazione della tastiera restano accesi a piena corrente (normale, dura quanto l'avvio del firmware).
  4. `GCR` (0x11): ISEL = 11 (Imax/4, circa 9 mA per piedino) come punto di partenza; la luminosità si regola a 256 passi con i registri 0x20–0x2F. KB_BL (P0_3) alimenta tutti gli 8 LED della tastiera: la corrente del piedino si divide tra loro.
- **LED di carica**: il rosso si accende da solo durante la carica (CHG_N dal BQ24074, circa 5 mA). Il firmware lo può accendere anche dall'AW9523B (P0_0).
- **SPI condiviso** (LoRa, microSD): tieni SD_CS_N alto prima di parlare con l'SX1262. Forza d'uscita dei GPIO SCK e MOSI dell'ESP32 a 1–2 (su 0–3): con R56/R57 da 47 Ω i fronti sono puliti.
- **Switch RF LoRa**: `SetDio2AsRfSwitchCtrl(true)` (in RadioLib `setDio2AsRfSwitch(true)`). DIO2 alto in TX → RF2 = TX.
- **TCXO LoRa** alimentato da DIO3 a 1,8 V (`setTCXO(1.8)`), ora senza resistenza in serie. Il ritardo di avvio del TCXO si imposta con lo stesso comando (in RadioLib è il secondo parametro di `setTCXO`); 5 ms bastano.
- **Display**: RGB a 12 bit (R3-R5, G0-G5, B3-B5; i bit bassi di rosso e blu sono ricopiati da quelli alti), HSYNC su GPIO1, VSYNC su GPIO0. Init SPI a 3 fili sul pannello: SCK = LCD_G4, SDA = LCD_G5. CS e RESET del display dall'AW9523B (P1_1, P1_0). Codice di init del pannello: file `C19625-001+1477_code.txt` di Adafruit.
- **Retroilluminazione display**: 40 mA a piena luminosità, PWM su LCD_BL_PWM.
- **I2C**: AW9523B 0x58, MAX17048 0x36, TCA8418 0x34 (sulla PCB 2).
- **GNSS**: uscita NMEA su GNSS_TXD (UART0 TX dell'AT6558R; il suo RX è tenuto alto da R20, quindi il modulo non si configura dall'MCU). GNSS_EN (AW9523B P0_6) accende insieme AT6558R (ON_OFF) e LNA (SHDN): P0 è open-drain, scrivere 1 lo rilascia e R21 lo porta a 3,3 V, scrivere 0 spegne tutto il GNSS.
