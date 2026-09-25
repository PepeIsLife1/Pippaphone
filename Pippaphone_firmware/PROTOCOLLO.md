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
| Banda / SF / CR | 125 kHz / SF10 / 4:5 | 250 kHz / SF10 / 4:5 |
| Preambolo | 16 simboli | 12 simboli |
| Sync word | 0x6C | 0x6C |

Tutti ascoltano sempre il canale principale. Il canale voce si usa solo durante un vocale,
e ci passano solo i due telefoni e i ripetitori sul percorso.

Prima di trasmettere si fa **CAD** (channel activity detection): se qualcuno sta già
trasmettendo si aspetta un tempo casuale. Non c'è limite di duty cycle.

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
| 0x30–0x3F | riservati al programma di amministrazione dei ripetitori | | |

Coda di trasmissione: prima l'SOS, poi conferme e risposte, poi i messaggi, infine beacon.

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
  un tempo che dipende dal segnale (chi sente peggio, quindi è più lontano, parte prima) e
  **rinuncia se sente altri due ripetitori** ripetere lo stesso pacchetto prima di lui.
- **Ultimo salto**: il ripetitore che consegna a un telefono si aspetta di sentirlo
  rispondere subito dopo; se non lo sente, trasmette il pacchetto un'altra volta.
- Le strade scadono dopo 30 minuti senza traffico. I beacon ogni 5 minuti le tengono
  aggiornate.

## Affidabilità

- `DM_TEXT`, `POS_REQ`, `POS_RESP`, `SOS` e `GROUP_INVITE` chiedono conferma (`DM_ACK`). Senza conferma si ritrasmette fino a **4 volte**, dalla seconda in flood
  forzato; il tempo di attesa dipende dal tempo in aria del pacchetto. Dopo 4 tentativi il
  messaggio diventa **"Non raggiungibile"**.
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
2. B risponde `VOICE_ACCEPT` (o "occupato"). I ripetitori sul percorso vedono la risposta e
   passano al **canale voce** per il tempo necessario (al massimo 2 minuti, 15 secondi
   senza traffico).
3. A manda i pezzi `VOICE_DATA` sul canale voce, uno alla volta: il successivo parte quando
   sente l'eco del precedente dal ripetitore.
4. A manda `VOICE_END`; B risponde con i **pezzi mancanti** (bitmap a 32 bit) e A li rimanda.
   Se qualcosa manca ancora, B riproduce il vocale con silenzio al posto dei pezzi persi.
5. Tutti tornano al canale principale. B riproduce il vocale subito (come un walkie-talkie).

## Beacon

Ogni 5 minuti, solo ai vicini diretti: ruolo (telefono/ripetitore), batteria, ora e se
l'ora viene dal GPS. Servono a tenere aggiornate le strade, a mostrare il segnale sul
telefono e a sincronizzare l'ora di chi non ha il GPS.
