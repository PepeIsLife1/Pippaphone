// Simulatore nel browser (Emscripten): quello che il resto del simulatore chiede alla pagina.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "scheda/audio.h"

namespace web {

int64_t nowUs();                                     // tempo vero dall'avvio della pagina
void sleep(uint32_t ms);                             // vTaskDelay: servizi, poi si cede al browser
void after(uint32_t ms, std::function<void()> fn);   // fra ms millisecondi, nel compito dell'interfaccia
void frameChanged();                                 // lo schermo e' cambiato
void backlight(uint8_t percent);                     // retroilluminazione del display
void keyboardLight(uint8_t level);                   // luce della tastiera (comando 0x01 al C3)
void play(const std::vector<scheda::Note>& notes, bool loop, uint8_t volume);
void stopSound();
[[noreturn]] void powerOff();
[[noreturn]] void restart();

// Rete finta (rete_web.cpp): comandi della pagina ("fai succedere qualcosa").
void command(int cmd, int arg);

}  // namespace web
