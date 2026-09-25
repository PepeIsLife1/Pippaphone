# Q-Mesh: scocca apribile (rev. 1)

Scocca in stile BlackBerry Classic per le PCB del Q-Mesh. Si apre dal retro: due ganci in alto e due viti M2 in basso.
È pensata per la stampa in resina SLA (per esempio da JLC3DP).

> Questa scocca è disegnata per la **PCB 1 rev. 1**, non per la rev. 0. Il display vero è 70,45 × 70,32 mm, più grande dello spazio che la rev. 0 gli lasciava: la PCB 1 si allunga a 130,3 mm e l'antenna LoRa diventa esterna (SMA). L'elenco delle modifiche è più sotto.

## File

| File | A cosa serve |
|---|---|
| `QMesh_scocca_assieme.step` | Tutte le parti montate, con nomi e colori. Si apre in Fusion (File > Apri). Qui il retro è liscio, per non appesantire Fusion; la goffratura è in `Retro.step` e `Retro.stl`. |
| `QMesh_riferimento_PCB_rev1.step` | Ingombri di PCB, display, batteria, SMA e componenti: importalo insieme alla scocca per controllare gli spazi. |
| `Frontale.stl/.step`, `Retro.stl/.step`, `Supporto_tastiera.stl/.step` | Pezzi da stampare. |
| `Tasti_albero.stl/.step` | I 44 tasti (39 della tastiera, cappuccio del 5 vie, 4 laterali) uniti da canali sottili, così diventano un unico pezzo da ordinare. I canali si attaccano alle alette, che restano nascoste sotto il frontale. |
| `Lente_66x66_acrilico_1mm.dxf` | Taglio laser della lente (PMMA trasparente 1 mm). |
| `PCB1_rev1_contorno.dxf` | Nuovo contorno della PCB 1, con intaglio SMA, fori M2 e asola del display. Si importa in KiCad su Edge.Cuts. |
| `fusion/QMeshScocca/` | Script per Fusion che ricostruisce tutto con schizzi ed estrusioni nella timeline. |
| `sorgenti/` | `spec.py` (tutte le misure), `build_cq.py`, `export.py`, `render.py`: la stessa specifica genera STEP/STL con CadQuery e il modello in Fusion. |
| `render/` | Viste renderizzate: fronte, retro, esploso, sezioni. |

## Aprirla in Fusion

**STEP**: File > Apri > Apri dal mio computer > `QMesh_scocca_assieme.step`. Ogni parte arriva come componente separato, modificabile in modellazione diretta.

**Script parametrico**:
1. Copia la cartella `fusion/QMeshScocca` dove preferisci.
2. In Fusion apri Utilità > Script e componenti aggiuntivi, premi "+" accanto a "I miei script" e scegli quella cartella.
3. Seleziona `QMeshScocca` e premi Esegui. Si apre un nuovo progetto con i componenti Frontale, Retro, Supporto_tastiera, Lente e Tasti (circa 290 operazioni, qualche minuto).
4. Per cambiare le misure modifica il dizionario `P` in `qmesh_spec.py` (spessori, giochi, lente, batteria...) e rilancia lo script.

In cima allo script ci sono le opzioni: `TASTI`, `SCRITTE` e `TEXTURE` (la goffratura del retro è spenta di default perché rallenta molto; negli STEP e STL c'è già).

Lo script non l'ho potuto eseguire in Fusion: l'ho provato contro una simulazione delle API che controlla nomi, argomenti e schizzi chiusi. Se in Fusion qualche operazione fallisce, lo script continua e alla fine elenca quelle non riuscite: mandami quel messaggio.

## Misure

- Corpo 74 × 134 × 14,8 mm (tasti +0,4–0,9 mm, cappuccio 5 vie +1,5 mm). Il Classic vero è 72,4 × 131 × 10,2.
- Pareti 1,6 mm, frontale 1,2 mm (1,6 sotto la lente), retro 1,2 mm.
- Giochi: PCB 0,4 mm sui lati e 0,25 mm in alto e in basso, display 0,175 mm per lato, tasti 0,15 mm, bordino del retro 0,15 mm.

Pila dal fronte (mm): lente 0–1 · display 1,7–3,6 · alette dei tasti 1,2–1,7 · KMR2 2,0–3,9 · PCB 2 3,9–4,5 · supporto tastiera 4,5–7,0 · PCB 1 7,0–8,0 · batteria 8,3–13,3 · retro 13,6–14,8.

## Minuteria

| Pezzo | Q.tà | Note |
|---|---|---|
| Inserti filettati M2 in ottone, Ø esterno 3,2 × 3 mm | 4 | Da **incollare** con epossidica nei fori da 3,3 mm: la resina SLA non si fonde, quindi gli inserti a caldo non tengono. |
| Vite M2 × 10 a testa cilindrica (ISO 7045 / DIN 7985) | 2 | Retro → PCB 1 → frontale. |
| Vite M2 × 3 a testa cilindrica | 2 | PCB 2 → frontale. |
| SMA femmina da pannello con pigtail U.FL 1,13 mm, ~5 cm, esagono 8 mm | 1 | Si inserisce da dentro; dado e rondella fuori. Misura il tuo: `sma_hex` e `sma_hex_len` in `spec.py` / `qmesh_spec.py`. |
| Antenna 868 MHz con SMA maschio | 1 | Stilo corto o a gomma. |
| Lente PMMA 66 × 66 × 1 mm | 1 | Incollata a filo (colla trasparente o UV, non biadesivo spesso). |
| Biadesivo 0,1 mm | – | Display sotto la cornice della lente, batteria sulla PCB. |
| Cavo piatto FFC 12 poli passo 0,5, ~22 mm, contatti **sullo stesso lato** | 1 | Tra J1 (PCB 2) e J7 (PCB 1), piega a U sul bordo alto della tastiera. |
| LiPo 505060 (5 × 50 × 60 mm) con JST SH 2 poli | 1 | Sede 51 × 60,4 mm. |
| Speaker 1511 (15 × 11 × 3,5) e motore a moneta 0820 | 1 + 1 | Incollati nelle sedi del retro, fili saldati ai pad. |

## Ordine di montaggio

1. Incolla i 4 inserti nel frontale.
2. Inserisci da dentro i 4 tasti laterali nelle loro tasche.
3. Incolla la lente da fuori; poi il display da dentro, con biadesivo 0,1 mm sulla cornice nera.
4. Stacca i tasti dall'albero, carteggia i punti d'attacco sulle alette e mettili nelle aperture da dentro, faccia in giù.
5. Appoggia la PCB 2 sui tasti e fissala con le due M2 × 3. Incolla il cappuccio del 5 vie sullo stelo, da fuori.
6. Collega il cavo piatto a J1, appoggia il supporto tastiera e collega l'altro capo a J7 con la PCB 1 tenuta aperta "a libro". Poi chiudi la PCB 1 sul supporto.
7. Fai passare il flat del display nell'asola e collegalo a J5. Monta l'SMA nel foro in alto (esagono nella sede, dado fuori) e collega il pigtail a J4.
8. Batteria sul retro della PCB 1, dentro la cornice; speaker e motore nel retro.
9. Chiudi: infila i due ganci del retro nelle sedi in alto, abbassa il retro e avvita le due M2 × 10.

## Modifiche per la PCB 1 rev. 1

1. **Contorno 70 × 130,3 mm** (era 124). Tutto ciò che nella rev. 0 sta sotto y = 70 mm scende di 6,3 mm: display FPC, tastiera, alimentazione, audio, USB-C, microSD.
2. **Intaglio 16,0 × 7,2 mm nell'angolo in alto a sinistra** per il corpo dell'SMA.
3. **Antenna LoRa esterna**: rimuovere AE2 (elicoidale 868) e la sua zona libera; J4 (U.FL) diventa montato ed è l'uscita verso l'SMA. La rete di adattamento verso J4 va messa a 50 Ω diretti (serie 0 Ω, shunt non montati) finché non si misura col VNA.
4. **LED D3** in (44,5; 3,5) sul lato F: il vecchio posto finisce sotto il display. Nel frontale c'è il canale di luce.
5. **J7** in (14,5; 95,5) sul lato F, con l'ingresso del cavo verso l'alto. **C70** in (5,5; 95,5). Così J7 non è più sotto J1 e bastano 2,5 mm tra le schede.
6. **Fori M2** Ø 2,2 mm in (4,5; 127,0) e (65,5; 127,0), senza rame per Ø 5,2 mm su tutti gli strati.
7. **Asola del flat del display** in (22; 79,3)–(48; 80,9).
8. Zone da lasciare libere:
   - lato F, appoggi Ø 2,4 in (18; 4) e (48,6; 4);
   - lato B, appoggi Ø 2,8 in (18; 4) e (44,5; 4);
   - lato B, batteria 9,5–60,5 × 29,1–89,5, nessun componente;
   - lato B, sotto speaker e motore, componenti alti al massimo 1,5 mm.

Coordinate KiCad della PCB 1, origine nell'angolo in alto a sinistra, y verso il basso. Nessuna modifica obbligatoria alla PCB 2.

## Da verificare

- **Stelo del 5 vie**: il cappuccio assume stelo Ø 2,6 mm e altezza totale 5,0 mm (dato Alps). Misuralo e correggi `nav_stem_d` / `nav_total_h`.
- **Esagono dell'SMA**: i pigtail economici cambiano molto; la sede è per esagono 8 mm lungo 2,5 mm.
- **Flat del display**: lunghezza sufficiente per piegare dal bordo del display, passare nell'asola e arrivare a J5 (~20 mm).
- **Tasti laterali**: si appoggiano all'attuatore EVQ-P7C con 0,15 mm di gioco; se risultano duri o molli si sposta la faccia interna del tasto (70,75 mm, funzione `keycaps` in `spec.py`).
- **Resina**: per tasti e ganci meglio una resina tenace (tipo 8001 o "tough") rispetto alla standard, più fragile.
- **Retroilluminazione**: con resina nera i LED della tastiera non si vedono. Per la luce sotto i tasti servono tasti in resina trasparente, verniciati e con le lettere scoperte.
