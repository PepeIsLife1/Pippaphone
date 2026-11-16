// Giochi: schermate interne (le apre il menu Giochi in schermate_giochi.cpp).
#pragma once

#include "ui/ui.h"

namespace ui {

Screen* makeSnake();             // serpente (gioco_serpente.cpp)
Screen* makeMines(int livello);  // campo minato 0 facile, 1 medio, 2 difficile (gioco_mine.cpp)
Screen* makeTris();              // sfida via radio in corso: tris (gioco_tris.cpp)
Screen* makeNavale();            // sfida via radio in corso: battaglia navale (gioco_navale.cpp)
// Apre la schermata della sfida in corso (tris o battaglia).
void openChallenge();

// Schermate della sfida via radio: si registrano da sole (costruttore e distruttore) con l'id della
// partita che mostrano. Se quella in cima e' della partita di un avviso, l'avviso non serve: si
// aggiorna da sola. (Un elenco e non un si'/no: la schermata vecchia viene distrutta dopo che la
// nuova e' gia' stata creata.)
void challengeScreen(Screen* s, bool open, uint16_t id = 0);
// Id della partita della schermata di sfida in cima; -1 se in cima non c'e' una schermata di sfida.
int challengeOnTop();

// Cifra in testo fisso per le caselle ("1".."8").
const char* digitText(int n);

}  // namespace ui
