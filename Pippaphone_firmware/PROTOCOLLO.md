# Protocollo PippaNet

Rete LoRa privata per i telefoni Pippaphone. Il codice è in `componenti/pippanet/`, è
indipendente dall'hardware e si prova sul PC con `test/`.

## Com'è fatta la rete

- **Ripetitori fissi** (indirizzi `0xF001`–`0xFFFE`, chiamati R1, R2...): ascoltano sempre e
  ripetono i pacchetti. Non hanno chiavi: non possono leggere i messaggi.
- **Telefoni** (numeri 1–59999): non ripetono i pacchetti degli altri. Parlano con i
  ripetitori a portata, o direttamente con un telefono vicino.
- Al massimo **10 salti**. Niente collegamento a internet.
- `0xFFFF` = tutti (canale globale, beacon); `0x0000` = telefono senza numero.

## Radio

| | Canale principale | Canale voce |
|---|---|---|
| Frequenza | 869,525 MHz | 869,850 MHz |
| Banda / SF / CR | 62,5 kHz / SF7 / 4:5 | 125 kHz / SF7 / 4:5 |
| Preambolo | 16 simboli | 12 simboli |
| Sync word | 0x6C | 0x6C |

Tutti ascoltano sempre il canale principale. Il canale voce si usa solo durante un vocale o
una chiamata, e ci passano solo i due telefoni e i ripetitori sul percorso.

Prima di trasmettere si fa **CAD** (channel activity detection): se qualcuno sta già
trasmettendo si aspetta un tempo casuale (100–700 ms; 10–30 ms durante una chiamata). Non c'è
limite di duty cycle.

## Intestazione (14 byte, little endian)

| Byte | Campo |
|---|---|
| 0 | bit 7: FORZA_FLOOD, bit 6: VUOLE_ACK, bit 0–5: tipo |
| 1 | limite di salti (4 bit alti) · salti fatti (4 bit bassi) |
| 2–3 | mittente |
| 4–5 | destinatario (id del gruppo per `GROUP_TEXT`) |
| 6–9 | seq: contatore del mittente, diverso per ogni pacchetto che crea |
| 10–11 | via: ripetitore che deve ripetere (0 = chiunque) |
| 12–13 | ultimo: chi ha trasmesso questa copia |

Il pacchetto si riconosce da (mittente, seq): ogni nodo scarta i doppioni che ha già
visto. "Salti fatti", "via" e "ultimo" cambiano a ogni salto e non sono autenticati; tipo,
mittente, destinatario e seq invece sì (dati associati della cifratura o della firma).

Il seq si salva in memoria ogni tanto con un margine, così dopo un riavvio non si
riusano valori già usati (servono anche come nonce).

## Tipi di pacchetto

| Tipo | Nome | Cifratura | Priorità |
|---|---|---|---|
| 0x01 | `DM_TEXT`: SMS 1:1 | coppia | normale |
| 0x02 | `DM_ACK`: consegnato / letto | coppia | alta |
| 0x03 | `GROUP_TEXT` | chiave del gruppo + firma | normale |
| 0x04 | `GLOBAL_TEXT`: canale globale | in chiaro + firma | normale |
| 0x05 | `CERT_REQ`: "mandami il tuo certificato" (con il mio) | — | normale |
| 0x06 | `CERT_RESP`: certificato | firmato dall'amministratore | normale |
| 0x07 | `POS_REQ`: richiesta di posizione | coppia | normale |
| 0x08 | `POS_RESP`: posizione o rifiuto | coppia | normale |
| 0x09 | `SOS` | coppia | **massima** |
| 0x0A | `GROUP_INVITE`: chiave e membri del gruppo | coppia | normale |
| 0x10 | `BEACON`: "ci sono", ora, batteria | — | bassa, non ripetuto |
| 0x11 | `PROV_HELLO`: telefono nuovo chiede un numero | — | bassa, non ripetuto |
| 0x12 | `PROV_CERT`: l'amministratore assegna il numero | firmato | non ripetuto |
| 0x20 | `VOICE_REQ`: posso mandarti un vocale? | coppia | normale |
| 0x21 | `VOICE_ACCEPT`: sì, passiamo al canale voce | coppia | alta |
| 0x22 | `VOICE_DATA`: pezzo di vocale | coppia | normale (canale voce) |
| 0x23 | `VOICE_END`: fine / pezzi mancanti | coppia | normale (canale voce) |
| 0x24 | `CALL_REQ`: ti chiamo | coppia | normale |
| 0x25 | `CALL_RING`: squilla / occupato | coppia | alta |
| 0x26 | `CALL_ANSWER`: ho risposto, passiamo al canale voce | coppia | alta |
| 0x27 | `CALL_VOICE`: voce della chiamata | coppia | alta (canale voce) |
| 0x28 | `CALL_END`: fine o rifiuto della chiamata | coppia | normale (tutti e due i canali) |
| 0x30–0x3F | riservati al programma di amministrazione dei ripetitori | | |

Coda di trasmissione: prima l'SOS, poi conferme e risposte (e la voce delle chiamate), poi i
messaggi, infine beacon.

## Identità e sicurezza

- Ogni telefono crea al primo avvio due coppie di chiavi: **Ed25519** (firma) e **X25519**
  (cifratura). In memoria si salvano solo i due semi da 32 byte.
- **Codice** di un telefono: 16 cifre ricavate dalla chiave Ed25519 (BLAKE2b). Si confronta a
  voce o a vista durante l'assegnazione del numero.
- **Certificato** (134 byte): numero, chiave Ed25519, chiave X25519, data, firma
  dell'amministratore sul tutto (con prefisso "PNCERT1"). Un telefono accetta il certificato
  di un altro solo se la firma è dell'amministratore che conosce.
- **Messaggi 1:1**: chiave della coppia = BLAKE2b(X25519(mia, sua) ‖ chiavi pubbliche in
  ordine). Cifratura **XChaCha20-Poly1305**, tag di 16 byte. Il nonce non viaggia: si ricava
  da mittente, seq e tipo. Dati associati: tipo, mittente, destinatario, seq.
- **Gruppi**: chiave casuale da 32 byte scelta da chi crea il gruppo, mandata a ogni membro
  con `GROUP_INVITE` (cifrato con la chiave della coppia). Ogni messaggio è cifrato con la
  chiave del gruppo e firmato Ed25519 dal mittente, così un membro non può fingersi un altro.
- **Canale globale**: in chiaro (lo legge chiunque abbia il firmware) ma firmato: chi riceve
  controlla la firma con il certificato del mittente.
- **Primo contatto**: se non ho il certificato del destinatario, mando `CERT_REQ` (con dentro
  il mio) e il messaggio parte quando arriva `CERT_RESP`. I certificati visti passare si
  imparano e si salvano.

## Assegnazione del numero

1. Il telefono nuovo trasmette `PROV_HELLO` (le sue chiavi pubbliche) ogni 5–7 secondi, solo ai
   vicini diretti.
2. L'amministratore vede il codice a 16 cifre, lo confronta con lo schermo del telefono
   nuovo, sceglie il numero e manda `PROV_CERT` (certificato firmato + chiave pubblica
   dell'amministratore).
3. Il telefono nuovo mostra il numero e il codice dell'amministratore: l'utente accetta con OK.

## Instradamento

- Ogni ripetitore **impara le strade** guardando i pacchetti che passano: il campo "ultimo"
  dice da quale vicino arriva il mittente, "salti fatti" quanto è lontano.
- **Strada nota**: il ripetitore mette nel campo "via" il prossimo ripetitore e solo quello
  ripete. Se non lo sente ripartire dal vicino ("ack passivo"), lo trasmette un'altra volta.
- **Strada sconosciuta, pacchetti a tutti, nuovi tentativi**: flood. Ogni ripetitore aspetta
  un tempo che dipende dal segnale (chi sente peggio, quindi è più lontano, parte prima;
  l'SNR si conta dal limite di ricezione dello SF) e
  **rinuncia se sente altri due ripetitori** ripetere lo stesso pacchetto prima di lui.
  Se dopo averlo ripetuto non sente i ripetitori vicini ripeterlo a loro volta, lo trasmette
  un'altra volta. I messaggi senza conferma (`GROUP_TEXT`, `GLOBAL_TEXT`), che il telefono
  manda una volta sola, hanno un terzo tentativo. Scatta solo se dopo la prima trasmissione
  non si è sentito nessun ripetitore: vuol dire che è andato perso davvero, per esempio per
  due collisioni di fila con un nodo nascosto. Nelle reti fitte, dove qualcuno tace perché
  l'ha già sentito due volte, non scatta.
- **Ultimo salto**: il ripetitore che consegna a un telefono si aspetta di sentirlo
  rispondere subito dopo; se non lo sente, trasmette il pacchetto un'altra volta.
- Le strade scadono dopo 30 minuti senza traffico. I beacon ogni 5 minuti le tengono
  aggiornate.

## Affidabilità

- `DM_TEXT`, `POS_REQ`, `POS_RESP`, `SOS` e `GROUP_INVITE` chiedono conferma (`DM_ACK`). Senza conferma si ritrasmette fino a **4 volte**, dalla seconda in flood
  forzato; il tempo di attesa dipende dal tempo in aria del pacchetto. Dopo 4 tentativi il
  messaggio diventa **"Non raggiungibile"**.
- `DM_TEXT` e `GROUP_INVITE`, prima di arrendersi, fanno **un altro giro di 4 tentativi 3,5
  minuti dopo** (stato "riprovo più tardi"): chi è in chiamata, o dietro un ripetitore in
  chiamata, non sente i messaggi per al massimo 3 minuti. Se nel frattempo si sente un pacchetto
  di quel telefono, il giro parte subito. Stesso id del messaggio: se il primo era arrivato e
  si era persa solo la conferma, chi riceve non lo vede due volte.
- Stati di un messaggio: in attesa → inviato → **consegnato ✓** → **letto ✓✓** (la conferma
  di lettura vale anche per i messaggi precedenti).
- Il telefono ascolta l'eco dei suoi pacchetti ripetuti dai ripetitori: se non la sente,
  ritrasmette prima; se la sente, non manda altro finché il pacchetto non si è allontanato,
  per non coprirlo.

## Testi

Alfabeto ridotto a **7 bit** (lettere, cifre, punteggiatura, à è é ì ò ù e maiuscole, €, £,
°, « »...): 160 caratteri stanno in 140 byte, come gli SMS. I caratteri non previsti
diventano "?".

Contenuto: ora di invio (4 byte, secondi Unix; 0 se ignota) + testo compresso.

## Posizione

1. A manda `POS_REQ` a B.
2. B vede "A vuole sapere dove sei" e sceglie **Consenti** o **Rifiuta**.
3. B risponde con `POS_RESP`: posizione (lat/lon ×10⁷, quota, precisione, età del dato) oppure
   rifiuto, oppure "GPS senza posizione".

La posizione non si manda mai senza conferma, tranne nell'SOS.

## SOS

- Va a **tutti i contatti in rubrica**, un pacchetto cifrato per ognuno, con posizione e
  batteria.
- Ha la **priorità massima** in coda e si **ripete ogni 2 minuti** con la posizione
  aggiornata finché non si annulla (END tenuto premuto 3 secondi). L'annullamento si manda a
  tutti.
- Chi lo riceve vede una schermata rossa con sirena e conferma la ricezione; il mittente vede
  chi l'ha ricevuto.

## Vocali (walkie-talkie)

Codec 2 a 700 bit/s: 40 ms di voce = 4 byte. Massimo 15 secondi = 1500 byte, divisi in
pezzi da 216 byte (54 frame): al massimo 7 pacchetti.

1. A manda `VOICE_REQ` (sessione, numero di pezzi, durata) sul canale principale.
2. B risponde `VOICE_ACCEPT` (o "occupato"). I ripetitori che ripetono la risposta passano al
   **canale voce** per il tempo necessario (al massimo 2 minuti, 15 secondi senza traffico).
   Ognuno si segna da quale vicino gli è arrivato il "sì" e a chi l'ha passato.
3. A manda i pezzi `VOICE_DATA` sul canale voce, uno alla volta: il successivo parte quando
   sente l'eco del precedente dal ripetitore. I pezzi (e i `VOICE_END`) seguono la strada
   del "sì" al contrario, non la tabella delle strade. Con due ripetitori in parallelo la
   richiesta può passare da uno e il "sì" dall'altro: solo quello del "sì" è sul canale voce.
4. A manda `VOICE_END`; B risponde con i **pezzi mancanti** (bitmap a 32 bit) e A li rimanda.
   Se qualcosa manca ancora, B riproduce il vocale con silenzio al posto dei pezzi persi.
5. Tutti tornano al canale principale. B riproduce il vocale subito (come un walkie-talkie).

## Chiamate in diretta (a turni)

Si parla a turni tenendo premuto il PTT, come una radio, ma la voce arriva mentre si parla
(circa 0,6–1,1 s dopo). Codec 2 a 700 bit/s. I pacchetti della chiamata non hanno l'id del
messaggio: dentro la cifratura c'è subito il contenuto.

1. A manda `CALL_REQ` (sessione, codec) sul canale principale, con "vuole conferma": il
   ripetitore dell'ultimo salto aspetta di sentire B rispondere. Senza squillo A riprova ogni
   4 s, dal terzo tentativo in flood; dopo 4 tentativi **"Non raggiungibile"**. Se B chiede il
   certificato di A, B c'è: A gli lascia il tempo.
2. B squilla e risponde `CALL_RING` **per la strada da cui è arrivata la chiamata** (via =
   ultimo salto, oppure A stesso se si sentono direttamente). Se B è già in un'altra chiamata
   o in un vocale risponde "occupato". Mentre squilla, A ripete `CALL_REQ` ogni 10 s (se la
   risposta si perde, B la rimanda); dopo 45 s **"Non risponde"** (per B "chiamata persa").
3. B risponde: `CALL_ANSWER`, sempre per la strada della chiamata. I ripetitori che lo
   ripetono passano al **canale voce** e si segnano i vicini verso A (a chi l'hanno passato) e
   verso B (da chi è arrivato). Chi è sul percorso non sente più i messaggi fino alla fine. Se
   entro 4 s non passa niente della chiamata, la risposta si è persa più avanti: il ripetitore
   torna subito ai messaggi, così sente la risposta rimandata. Chiamata, squillo e risposta sono
   distanziati come i messaggi (il secondo parte quando il primo si è allontanato di un paio di
   salti), altrimenti una risposta data subito si scontra con lo squillo nella catena.
4. Sul canale voce A manda "ci sono" (`CALL_VOICE` vuoto con il bit PING) ogni 1,5 s finché non
   sente B; B risponde a ognuno. Se B non sente niente per 5 s rimanda la risposta sul canale
   principale (al terzo giro: caduta).
5. Si parla a turni: chi tiene premuto il PTT manda un `CALL_VOICE` ogni **400 ms** (10 frame,
   0 o 1 ripetitore) o ogni **480 ms** (12 frame, 2 o più ripetitori). In una catena il
   pacchetto nuovo non deve partire prima che il secondo ripetitore abbia finito di ripetere il
   precedente: servono 3 trasmissioni per pacchetto, e con 10 frame (133 ms in aria) non ci
   stanno in 400 ms. Con 0 o 1 ripetitore ogni pacchetto porta **anche la voce del
   precedente**: un pezzo perso si recupera dal successivo (0,4 s di ritardo in più).
   L'ultimo pacchetto del turno ha il bit LAST: ora può parlare l'altro. Il PTT non parte
   mentre arriva la voce dell'altro. Un turno dura al massimo 60 s.
6. I ripetitori ripetono la voce **subito**, solo se sono il "via" indicato, senza attese a
   caso e senza nuovi tentativi; un pezzo che resta in coda più di 400 ms si butta. Con il
   canale occupato (CAD) durante una chiamata si riprova fra 10 e 30 ms invece che fra 100 e
   700.
7. Nel silenzio un pacchetto vuoto ogni 3 s tiene aperta la strada. Niente dall'altro per 10 s
   (mentre non si parla): **caduta**. I ripetitori tornano ai messaggi dopo 8 s senza voce.
8. Chi riattacca manda `CALL_END` due volte sul canale voce (o una sul canale principale se
   non si era ancora risposto: annulla o rifiuta). Dopo **3 minuti** la chiamata si chiude da
   sola: chi è in chiamata, e i ripetitori sul percorso, non sentono i messaggi.

`CALL_VOICE` (dopo la cifratura): sessione (2), turno (1), numero del pacchetto nel turno (1),
bit PING/LAST (1), frame di questo pacchetto e di quello prima (4 + 4 bit), poi la voce del
pacchetto prima (se c'è) e quella di questo, frame di 28 bit uno dietro l'altro senza buchi.
Con 10 frame: 71 byte in tutto (133 ms in aria), 106 con la ridondanza (184 ms).

Messaggi e chiamate: un messaggio per chi è in chiamata resta senza conferma; chi lo manda
riprova per una quarantina di secondi, poi "riprovo più tardi", e 3,5 minuti dopo (quando la
chiamata è finita per forza) fa un altro giro. I messaggi scritti da chi è in chiamata partono
dopo. Un SOS chiude la chiamata.

## Beacon

Ogni 5 minuti, solo ai vicini diretti: ruolo (telefono/ripetitore), batteria, ora e se
l'ora viene dal GPS. Servono a tenere aggiornate le strade, a mostrare il segnale sul
telefono e a sincronizzare l'ora di chi non ha il GPS.
