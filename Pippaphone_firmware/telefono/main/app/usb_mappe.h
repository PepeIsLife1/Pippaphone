// Mappe dal computer (solo T-Deck Plus): la pagina del flasher (strumenti/flasher) manda i tile
// della mappa offline lungo il cavo USB e il telefono li scrive in /mappe della microSD.
//
// Sulla stessa porta continua a passare il log: le risposte del telefono sono righe che
// cominciano con "@PIPPA ". Comandi (righe di testo):
//   @PIPPA?                         -> @PIPPA ciao <versione> sd=<0|1> libero=<MB liberi>
//   @HAI z/x/y.png <byte>           -> @PIPPA si (c'e' gia', uguale) | @PIPPA no
//   @FILE z/x/y.png <byte> <crc32>  seguito da esattamente <byte> byte (il PNG)
//                                   -> @PIPPA ok z/x/y.png | @PIPPA errore <motivo>
//   @FINE                           -> @PIPPA fine <tile ricevuti>
//   @WIFI?                          -> (diagnosi) accende per 20 s la rete Wi-Fi dell'aggiornamento:
//                                      @PIPPA wifi acceso <rete> ... | @PIPPA wifi errore <motivo>
//   @OTA!                           -> (prova) accende la rete e manda a se' stesso il firmware che sta
//                                      usando, come il browser; se va bene si riavvia dall'altra
//                                      partizione: @PIPPA ota ... righe di avanzamento e risultato
//   @RIAVVIA!                       -> (prova) @PIPPA riavvio, poi riavvio software
//   @SPEGNI!                        -> (prova) "Spegni" con risveglio da solo dopo 10 s
//   @RETI?                          -> (diagnosi) le reti Wi-Fi che il telefono sente:
//                                      @PIPPA rete <nome> canale <n> <dBm> ... @PIPPA reti <quante>, ...
//   @CUFFIE?                        -> (diagnosi) cuffie USB-C, la sessione in corso o l'ultima (app/cuffie.h):
//                                      @PIPPA cuffie <stato> errore <motivo> sessioni <n> nome "<prodotto>"
//                                      id <vid:pid> uac <1|2> formato <Hz> Hz <canali> canali <bit> bit
//                                      suonate <s> s sottoflussi <n> saltati <n> errori <n> volume <0-10>
//                                      ram interna prima <byte> minima <byte> dopo <byte>
//   @CUFFIE! <secondi>              -> (prova) cuffie USB-C accese per 5-600 s, anche col computer attaccato:
//                                      @PIPPA cuffie accese per <s> s: poi la console torna (e la console
//                                      sparisce fino ad allora) | @PIPPA cuffie errore <motivo>
// Solo percorsi "cifre/cifre/cifre.png" (zoom 0-20) dentro /mappe, al massimo 1 MB per file; il
// CRC32 e' quello di zlib. Un file si scrive come .tmp e si rinomina solo se e' arrivato intero.
#pragma once

namespace app {

// Prende la console USB (driver USB-Serial-JTAG, il log continua a uscire) e avvia il compito che
// ascolta i comandi. Da chiamare all'avvio, dopo la microSD.
void usbMappeAvvia();

}  // namespace app
