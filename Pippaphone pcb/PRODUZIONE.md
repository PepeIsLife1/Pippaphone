# Q-Mesh rev. 1: ordine a JLCPCB

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
| Montaggio | **PCBA Standard, due lati**. Lato B: quasi tutto. Lato F: J7, C69, D3–D5 |
| File | `qmesh-main-gerber.zip`, `qmesh-main-BOM-JLCPCB.csv`, `qmesh-main-CPL-JLCPCB.csv` |

Strati: F.Cu segnali e colata GND · In1 segnali e colata GND · **In2 piano GND pieno** · B.Cu componenti, RF e colata GND.

## PCB 2, tastiera (`pcb2_tastiera/`)

| Opzione | Valore |
|---|---|
| Strati | 2 |
| Dimensioni | 69,5 × 39,5 mm |
| Spessore | 0,6 mm |
| Finitura | ENIG (i KMR2 e il TCA8418 QFN ringraziano) |
| Montaggio | PCBA Standard, due lati. Lato F: 39 KMR2, navigatore 5 vie, 8 LED. Lato B: TCA8418, passivi, J1 |
| File | `qmesh-kbd-gerber.zip`, `qmesh-kbd-BOM-JLCPCB.csv`, `qmesh-kbd-CPL-JLCPCB.csv` |

## Passi dell'ordine

1. Carica il gerber, imposta le opzioni qui sopra e attiva *PCB Assembly*.
2. Carica BOM e CPL. Le parti con codice LCSC si abbinano da sole; per i passivi controlla l'abbinamento proposto (valore e contenitore).
3. Le parti segnate "Global Sourcing" nella colonna Note vanno aggiunte da DigiKey/Mouser nel pannello *Global Sourcing Parts* prima di confermare. L'elenco è in `ACQUISTI.md`.
4. Nell'anteprima di posizionamento controlla il **pin 1** degli integrati (U2–U14, U8, U9, FL1): JLC a volte ruota di 90° o 180° le parti con impronte diverse dalle sue. Si corregge lì, trascinando.
5. Non montare: R/C/L segnati DNP (posti di accordo RF), TP, H, JP. I jumper JP1/JP2 sono già chiusi in rame (1-2).
6. Speaker (LS1) e motorino (M1) sono pad per fili: si saldano a mano.

## Cosa è stato verificato

L'esito completo è in `VERIFICHE.txt` e nei report DRC (`*-drc.rpt`).

| Controllo | PCB 1 | PCB 2 |
|---|---|---|
| Reti della PCB uguali allo schema, pad per pad | 181 componenti, 0 differenze | 65 componenti, 0 differenze |
| DRC KiCad: connessioni mancanti | 0 | 0 |
| DRC KiCad: errori | 0 | 0 |
| Linee a 50 Ω (CPWG 0,30 / 0,20 su In2) | 49,0 Ω, tutte sul lato B, gap 0,20 mm | – |

Avvisi DRC rimasti sulla PCB 1, tutti valutati e innocui:

- **27 via "appese"**: via di fanout dove la pista prosegue sullo stesso strato. Collegano comunque, fanno solo un piccolo foro nei piani interni.
- **3 spezzoni** di pista più corti di 0,1 mm, attaccati alla loro rete.
- **11 cortili sovrapposti** nella catena RF (componenti 0402 stretti apposta): i corpi restano ad almeno 0,35 mm, la pick-and-place li monta senza problemi.
- **6 serigrafie** su rame o vicino al bordo: al massimo JLC le taglia.

Sotto le linee a 50 Ω il piano In2 è intero; unica eccezione, all'ingresso del GNSS (GN_LNA_IN), il foro di una via +3V3 sfiora il gap per 0,08 mm² proprio al pin del chip: trascurabile a 1575 MHz.

Piedinature controllate sui datasheet: IPD Johanson (impronta ufficiale KiCad), switch BGS12WN6 (tabella di verità), SAW, display (tabella pin del TL034WVS05), BQ24074 (ITERM/TMR).

## Dopo il montaggio

- **Reti di adattamento RF** (LoRa, Wi-Fi, GNSS): i valori sono quelli di partenza. Con un VNA si accordano i π-greco verso le antenne; i posti DNP sono lì per questo. Senza VNA funziona comunque, con qualche dB di margine in meno.
- **Prima accensione**: alimenta da USB senza batteria e controlla VSYS su TP2 e 3,3 V su TP3 (GND su TP5) e la corrente a riposo, poi collega la batteria (VBAT su TP4).

## Note per il firmware

- **Switch RF LoRa**: `SetDio2AsRfSwitchCtrl(true)` (in RadioLib `setDio2AsRfSwitch(true)`). DIO2 alto in TX → RF2 = TX.
- **TCXO LoRa** alimentato da DIO3 a 1,8 V (`setTCXO(1.8)`).
- **Display**: RGB a 12 bit (R3-R5, G0-G5, B3-B5), HSYNC su GPIO1, VSYNC su GPIO0. Init SPI a 3 fili sul pannello: SCK = LCD_G4, SDA = LCD_G5. CS e RESET del display dall'AW9523B (P1_1, P1_0). Codice di init del pannello: file `C19625-001+1477_code.txt` di Adafruit.
- **Retroilluminazione**: 40 mA a piena luminosità, PWM su LCD_BL_PWM.
- **I2C**: AW9523B 0x58, MAX17048 0x36, TCA8418 0x34 (sulla PCB 2).
- **GNSS**: uscita NMEA su GNSS_TXD, abilitazione da AW9523B P0_6.
