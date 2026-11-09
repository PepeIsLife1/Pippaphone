// Simulatore dell'interfaccia del telefono Pippaphone sul PC (senza hardware).
//
// Compila le schermate vere (telefono/main/ui), i dati veri (app/dati.cpp) e LVGL, con una
// scheda, una rete e un GPS finti. Un copione apre una dopo l'altra tutte le schermate, preme
// qualche tasto e salva uno screenshot PNG di ognuna nella cartella data come argomento.
//
//   sim_pcb   <cartella>   480x480, PCB Pippaphone
//   sim_tdeck <cartella>   320x240, LilyGO T-Deck Plus
//
// Serve anche per controllare che una modifica non cambi l'interfaccia della PCB: gli screenshot
// fatti coi sorgenti vecchi e con quelli nuovi devono essere identici pixel per pixel.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "scheda/scheda.h"
#include "sim.h"
#include "ui/schermate.h"
#include "ui/ui.h"

using scheda::Key;

namespace sim {

State& state() {
    static State s;
    return s;
}

#ifdef SIM_WEB
uint32_t unixNow() { return uint32_t(std::time(nullptr)); }  // nel browser: l'ora vera
#else
uint32_t unixNow() { return 1794231120u + uint32_t(nowMs() / 1000); }
#endif

void key(Key k, bool pressed) {
    scheda::KeyEvent e{k, pressed};
    xQueueSend(scheda::tastiera().queue(), &e, 0);
}

void tap(Key k) {
    key(k, true);
    key(k, false);
}

namespace {
struct Step {
    int wait;
    std::function<void()> fn;
};
std::vector<Step> g_steps;
size_t g_next = 0;
int64_t g_at = 0;
std::string g_out = "screenshot";
int g_shots = 0;
int g_overflows = 0;
}  // namespace

void step(int waitMs, std::function<void()> fn) { g_steps.push_back({waitMs, std::move(fn)}); }

void run() {
    while (nowMs() >= g_at) {
        if (g_next >= g_steps.size()) {
            std::fprintf(stderr, "fatto: %d screenshot in %s, %d oggetti fuori posto\n", g_shots, g_out.c_str(),
                         g_overflows);
            std::exit(0);
        }
        Step& s = g_steps[g_next++];
        s.fn();
        g_at = nowMs() + s.wait;
    }
}

// Oggetti che escono dal loro contenitore (o dallo schermo): testi troppo lunghi, righe che vanno a
// capo... Non si guardano le immagini (tile della mappa) e i figli degli elenchi che scorrono.
std::string firstText(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_label_class)) return lv_label_get_text(o);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        const std::string t = firstText(lv_obj_get_child(o, int32_t(i)));
        if (!t.empty()) return t;
    }
    return "";
}

// SIM_ALBERO=<nome schermata>: stampa gli oggetti con le coordinate (per capire un'impaginazione).
void dumpTree(lv_obj_t* o, int depth) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    std::string t = lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "";
    if (t.size() > 30) t = t.substr(0, 30);
    std::fprintf(stderr, "%*s%d,%d - %d,%d %s%s \"%s\"\n", depth * 2, "", int(a.x1), int(a.y1), int(a.x2), int(a.y2),
                 lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) ? "S" : "", lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) ? "H" : "",
                 t.c_str());
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dumpTree(lv_obj_get_child(o, int32_t(i)), depth + 1);
}

void checkOverflow(lv_obj_t* o, const std::string& shotName, bool inScroll = false) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || lv_obj_check_type(o, &lv_image_class)) return;
    const bool isLabel = lv_obj_check_type(o, &lv_label_class);
    lv_obj_t* parent = lv_obj_get_parent(o);
    if (parent && (lv_obj_check_type(parent, &lv_label_class) || !lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE))) {
        lv_area_t a, p;
        lv_obj_get_coords(o, &a);
        lv_obj_get_coords(parent, &p);
        const int32_t dx = std::max(p.x1 - a.x1, a.x2 - p.x2), dy = std::max(p.y1 - a.y1, a.y2 - p.y2);
        // Fuori dallo schermo conta solo se non e' dentro un elenco che scorre.
        const bool screen = !inScroll && (a.x1 < 0 || a.y1 < 0 || a.x2 >= width() || a.y2 >= height());
        if (dx > 1 || dy > 1 || screen) {
            std::string t = firstText(o);
            if (t.size() > 40) t = t.substr(0, 40) + "...";
            std::fprintf(stderr, "ESCE %s: \"%s\" di %d px in orizzontale, %d in verticale%s\n", shotName.c_str(),
                         t.c_str(), int(std::max<int32_t>(dx, 0)), int(std::max<int32_t>(dy, 0)),
                         screen ? " (fuori dallo schermo)" : "");
            g_overflows++;
            return;  // i figli escono di conseguenza
        }
    }
    const bool scroll = inScroll || (!isLabel && lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE));
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        checkOverflow(lv_obj_get_child(o, int32_t(i)), shotName, scroll);
}

// Azione, poi (dopo 600 ms simulati) lo screenshot.
void shot(const std::string& name, std::function<void()> action) {
    step(600, [action] {
        ui::wake();
        action();
    });
    step(10, [name] {
        const std::string path = g_out + "/" + name + ".png";
        if (!savePng(path)) std::fprintf(stderr, "non riesco a scrivere %s\n", path.c_str());
        g_shots++;
        const char* dump = std::getenv("SIM_ALBERO");
        if (dump && name == dump) dumpTree(lv_screen_active(), 0);
        checkOverflow(lv_screen_active(), name);
        checkOverflow(lv_layer_top(), name);
    });
}

void setOutput(const std::string& dir) { g_out = dir; }

using app::ChatId;
using app::ChatKind;
using app::Message;
using app::MsgKind;

// Rubrica, conversazioni e gruppi di prova.
void fillData() {
    app::Dati& d = app::dati();
    const uint32_t now = sim::unixNow();
    const struct {
        uint16_t n;
        const char* name;
    } people[] = {{101, "Marco Rossi"}, {102, "Giulia Bianchi"}, {205, "Luca Verdi"}, {318, "Sara Neri"},
                  {412, "Anna Colombo"}, {517, "Bruno Ferrari"}, {623, "Carla Esposito"}, {734, "Davide Romano"},
                  {845, "Elena Galli"}, {956, "Franco Conti"}, {777, "Massimiliano Bartolomeo Santangelo"}};
    // SIM_NOMI_LUNGHI=1: tutti i nomi lunghissimi, per scovare le scritte che escono.
    // SIM_NOMI_MAX=1: nomi lunghi quanto quelli che si possono scrivere davvero (24 caratteri per i
    // contatti e 20 per i gruppi, come i campi in schermate_contatti.cpp).
    const bool lunghi = std::getenv("SIM_NOMI_LUNGHI") != nullptr;
    const bool massimi = std::getenv("SIM_NOMI_MAX") != nullptr;
    auto taglia = [massimi](std::string s, size_t n) {
        if (massimi && s.size() > n) s.resize(n);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    for (const auto& p : people)
        d.setContact({p.n, taglia(std::string(p.name) + (lunghi || massimi ? " Bartolomeo Santangelo" : ""), 24)});

    pn::GroupInfo g;
    g.id = 3;
    g.name = "Escursione";
    g.members = {100, 101, 102, 205};
    d.setGroup(g);
    // Nomi lunghi: per vedere che le scritte restano dentro.
    pn::GroupInfo g2;
    g2.id = 4;
    g2.name = taglia("Gruppo di escursionismo del sabato mattina", 20);
    g2.members = {100, 777, 412, 517, 623};
    d.setGroup(g2);

    uint32_t id = 1;
    auto msg = [&](ChatId c, uint16_t from, int agoMin, MsgKind k, const std::string& text,
                   pn::Delivery st = pn::Delivery::READ, bool unread = false) {
        Message m;
        m.id = id++;
        m.from = from;
        m.out = from == 100;
        m.status = st;
        m.time = now - uint32_t(agoMin) * 60;
        m.kind = k;
        m.text = text;
        if (k == MsgKind::VOICE) {
            m.voiceMs = 7400;
            m.voiceFile = 1;
        }
        d.addMessage(c, m, unread);
    };
    const ChatId marco{ChatKind::DIRECT, 101}, giulia{ChatKind::DIRECT, 102}, luca{ChatKind::DIRECT, 205};
    const ChatId gruppo{ChatKind::GROUP, 3}, globale{ChatKind::GLOBAL, 0};
    msg(marco, 101, 190, MsgKind::TEXT, "Ciao! Domani si parte alle 7 dal rifugio?");
    msg(marco, 100, 186, MsgKind::TEXT, "Sì, ci vediamo al parcheggio alle 6:45");
    msg(marco, 101, 120, MsgKind::VOICE, "");
    msg(marco, 100, 50, MsgKind::TEXT, "Ok, porto io la corda e il fornelletto", pn::Delivery::DELIVERED);
    msg(marco, 100, 40, MsgKind::INFO, "Posizione inviata");
    msg(marco, 101, 12, MsgKind::POSITION, "454652000,91950000,130,9,30");
    msg(marco, 101, 2, MsgKind::TEXT, "Siamo quasi arrivati, ci vediamo su", pn::Delivery::READ, true);
    msg(marco, 100, 1, MsgKind::TEXT, "Vi vedo, arrivo!", pn::Delivery::RETRYING);
    msg(giulia, 102, 75, MsgKind::SOS, "454610000,91800000,118,12,40,63");
    msg(giulia, 102, 30, MsgKind::TEXT, "Hai visto il messaggio nel gruppo?", pn::Delivery::READ, true);
    msg(giulia, 102, 25, MsgKind::TEXT, "Chiamami quando puoi", pn::Delivery::READ, true);
    msg(gruppo, 205, 300, MsgKind::TEXT, "Partenza confermata per sabato");
    msg(gruppo, 100, 290, MsgKind::TEXT, "Perfetto, io ci sono");
    msg(luca, 100, 1500, MsgKind::TEXT, "Ci sei?", pn::Delivery::UNREACHABLE);
    msg(globale, 318, 600, MsgKind::TEXT, "Prova radio dal Monte Rosa, mi sentite?");

    // Vocale salvato: codec, durata (ms), pezzi persi.
    app::writeFile("/dati/voce/1.c2", {1, 0xE8, 0x1C, 0, 0, 0, 0, 0, 0, 0, 0});
}

#if CONFIG_SCHEDA_TDECK_PLUS
// Tile finti per provare la mappa (niente download): sfondo, strade, un fiume, il bordo del tile
// e, se labels, le coordinate z/x/y scritte sopra, cosi' si vede subito se sono messi al posto giusto.
void makeTiles(bool labels) {
    static const uint8_t DIGITS[11][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
                                          {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
                                          {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {1, 1, 2, 4, 4}};  // 0-9 e '/'
    for (int z = 13; z <= 15; z++) {
        const double n = double(1 << z);
        const double lat = 45.4642 * M_PI / 180;
        const int cx = int((9.19 + 180) / 360 * n);
        const int cy = int((1 - std::log(std::tan(lat) + 1 / std::cos(lat)) / M_PI) / 2 * n);
        for (int x = cx - 2; x <= cx + 2; x++)
            for (int y = cy - 2; y <= cy + 2; y++) {
                std::vector<uint8_t> rgb(256 * 256 * 3);
                for (int py = 0; py < 256; py++)
                    for (int px = 0; px < 256; px++) {
                        uint8_t r = 236, g = 232, b = 222;
                        const int gx = x * 256 + px, gy = y * 256 + py;  // coordinate globali: strade continue
                        if ((gx / 40 + gy / 56) % 7 == 0) r = 200, g = 226, b = 190;          // parchi
                        if (gx % 96 < 5 || gy % 128 < 5) r = 255, g = 255, b = 255;          // strade
                        if (std::abs((gx - 2 * gy) % 900) < 9) r = 150, g = 190, b = 235;   // fiume
                        if (labels && (px == 0 || py == 0)) r = 180, g = 170, b = 160;       // bordo del tile
                        uint8_t* p = &rgb[(size_t(py) * 256 + px) * 3];
                        p[0] = r;
                        p[1] = g;
                        p[2] = b;
                    }
                const std::string label = std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
                for (size_t i = 0; labels && i < label.size(); i++) {
                    const int d = label[i] == '/' ? 10 : label[i] - '0';
                    for (int ry = 0; ry < 5; ry++)
                        for (int rx = 0; rx < 3; rx++)
                            if (DIGITS[d][ry] & (4 >> rx))
                                for (int s = 0; s < 4; s++) {
                                    uint8_t* p = &rgb[(size_t(8 + ry * 2 + s / 2) * 256 + 8 + i * 8 + rx * 2 + s % 2) * 3];
                                    p[0] = 120;
                                    p[1] = 90;
                                    p[2] = 60;
                                }
                }
                const std::string dir = std::string(app::MAPPE) + "/" + std::to_string(z) + "/" + std::to_string(x);
                std::filesystem::create_directories(dir);
                savePngRgb(dir + "/" + std::to_string(y) + ".png", 256, 256, rgb);
            }
    }
}
#endif

}  // namespace sim

#ifndef SIM_WEB  // nel browser il main e' in web/sim_web.cpp e non c'e' il copione
namespace {

using app::ChatId;
using app::ChatKind;

void script() {
    using sim::shot;
    shot("01_principale", [] { ui::goHome(); });
    shot("02_menu", [] { sim::tap(Key::MENU); });
    shot("03_menu_selezione", [] {
        sim::tap(Key::RIGHT);
        sim::tap(Key::DOWN);
    });
    shot("04_messaggi", [] {
        ui::goHome();
        ui::push(ui::makeChats());
    });
    shot("05_chat", [] { ui::push(ui::makeChat({ChatKind::DIRECT, 101})); });
    shot("06_chat_scrivo", [] {
        for (Key k : {Key::C, Key::I, Key::A, Key::O, Key::SPACE, Key::M, Key::A, Key::R, Key::C, Key::O}) sim::tap(k);
    });
    shot("07_chat_simboli", [] { sim::tap(Key::SYM); });
    shot("08_chat_opzioni", [] {
        sim::tap(Key::BACK);
        sim::tap(Key::MENU);
    });
    shot("09_chat_gruppo", [] {
        ui::goHome();
        ui::push(ui::makeChat({ChatKind::GROUP, 3}));
    });
    shot("10_contatti", [] {
        ui::goHome();
        ui::push(ui::makeContacts());
        sim::tap(Key::DOWN);
    });
    shot("11_scheda_contatto", [] { ui::push(ui::makeContact(101)); });
    shot("12_modifica_contatto", [] { ui::push(ui::makeEditContact(101)); });
    shot("13_gruppi", [] {
        ui::goHome();
        ui::push(ui::makeGroups());
        sim::tap(Key::DOWN);
    });
    shot("14_nuovo_gruppo", [] { ui::push(ui::makeNewGroup()); });
    shot("15_walkie", [] {
        ui::goHome();
        ui::push(ui::makeWalkie(101));
    });
    shot("16_walkie_registro", [] {
        sim::state().recordedMs = 3200;
        sim::key(Key::PTT, true);
    });
    shot("17_vocale", [] {
        sim::key(Key::PTT, false);
        sim::state().recording = false;
        ui::goHome();
        ui::push(ui::makeVoicePlayer(101, 1, false));
    });
    shot("18_posizione", [] {
        ui::goHome();
        ui::push(ui::makePosition());
    });
    shot("19_richiesta_posizione", [] {
        ui::goHome();
        ui::push(ui::makePositionRequest(102, 55));
    });
    shot("20_posizione_contatto", [] {
        ui::goHome();
        ui::push(ui::makePositionView(101, "454652000,91950000,130,9,30", sim::unixNow() - 720));
    });
    shot("21_sos", [] {
        ui::goHome();
        ui::push(ui::makeSosActive(false));
    });
    shot("22_sos_attivo", [] {
        sim::state().sos = true;
        ui::goHome();
        ui::push(ui::makeSosActive(false));
    });
    shot("23_sos_ricevuto", [] {
        sim::state().sos = false;
        ui::goHome();
        ui::push(ui::makeSosReceived(102));
    });
    shot("24_impostazioni", [] {
        ui::goHome();
        ui::push(ui::makeSettings());
    });
    shot("25_impostazioni_fondo", [] {
        for (int i = 0; i < 12; i++) sim::tap(Key::DOWN);
    });
    shot("26_segnale", [] {
        ui::goHome();
        ui::push(ui::makeSignal());
    });
    shot("27_aggiorna", [] {
        ui::goHome();
        ui::push(ui::makeOta());
    });
    shot("28_amministrazione", [] {
        ui::goHome();
        ui::push(ui::makeAdmin());
    });
    shot("29_scegli_contatto", [] {
        ui::goHome();
        ui::push(ui::makePickContact("Nuovo messaggio", [](uint16_t) {}));
    });
    shot("30_blocco", [] {
        ui::goHome();
        ui::push(ui::makeLock());
    });
    shot("31_primo_avvio", [] {
        sim::state().hasNumber = false;
        ui::goHome();
        ui::replace(ui::makeFirstBoot());
    });
    shot("32_primo_avvio_offerta", [] {
        sim::state().offer = true;
        ui::replace(ui::makeFirstBoot());
    });
    shot("33_notifica", [] {
        sim::state().hasNumber = true;
        sim::state().offer = false;
        ui::replace(ui::makeHome());
        ui::toast("Nuovo messaggio · Marco Rossi", "Siamo quasi arrivati, ci vediamo su");
    });
    // PWR tenuto premuto: la domanda vera dell'interfaccia (sul T-Deck PWR e' SYM+spazio).
    sim::step(1800, [] { sim::key(Key::PWR, true); });
    shot("34_spegni", [] { sim::key(Key::PWR, false); });
#if CONFIG_SCHEDA_TDECK_PLUS
    shot("35_mappa", [] {
        ui::goHome();  // chiude anche la domanda dello spegnimento
        ui::push(ui::makeMap());
    });
    shot("36_mappa_zoom", [] { sim::tap(Key::I); });
    shot("37_mappa_spostata", [] {
        sim::tap(Key::RIGHT);
        sim::tap(Key::DOWN);
    });
    shot("38_posizione_voce_mappa", [] {
        ui::goHome();
        ui::push(ui::makePosition());
        sim::tap(Key::DOWN);
    });
    shot("39_sos_ricevuto_mappa", [] {
        ui::goHome();
        ui::push(ui::makeSosReceived(102));
        sim::tap(Key::OK);  // silenzia e mostra la posizione
    });
#endif
    // Chiamata in diretta: arriva, si risponde, si parla a turni, si riattacca; poi una in uscita.
    sim::step(6000, [] { ui::goHome(); });  // le notifiche di prima spariscono
    shot("40_chiamata_in_arrivo", [] {
        auto& s = sim::state();
        s.call = uint8_t(pn::CallState::INCOMING);
        s.callPeer = 101;
        s.callOut = false;
        s.callHops = 2;
        app::post({app::Ev::CALL, {ChatKind::DIRECT, 101}, 101, 0, int32_t(pn::CallState::INCOMING)});
    });
    sim::step(600, [] { sim::tap(Key::CALL); });  // risponde (il tasto arriva all'interfaccia dopo)
    shot("41_chiamata_in_corso", [] {
        sim::state().callHearing = 0;
        sim::state().callT0 = sim::nowMs() - 83000;
    });
    shot("42_chiamata_parli", [] { sim::key(Key::PTT, true); });
    shot("43_chiamata_parla_lui", [] {
        sim::key(Key::PTT, false);
        sim::state().callHearing = 1;
    });
    shot("44_chiamata_quasi_finita", [] {
        sim::state().callHearing = 0;
        sim::state().callT0 = sim::nowMs() - 158000;
    });
    shot("45_chiamata_finita", [] { sim::tap(Key::END); });
    shot("46_chiamata_in_uscita", [] {
        ui::goHome();
        sim::state().callHearing = -1;
        sim::state().callHops = 0;
        ui::push(ui::makeContact(102));
        sim::tap(Key::DOWN);
        sim::tap(Key::OK);  // "Chiama"
    });
}

}  // namespace

#ifdef _WIN32
// Crash: indirizzi dello stack rispetto all'eseguibile (da passare ad addr2line -e sim_*.exe).
#include <windows.h>
static LONG WINAPI onCrash(EXCEPTION_POINTERS* e) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::fprintf(stderr, "crash 0x%lx, base %p\n", e->ExceptionRecord->ExceptionCode, reinterpret_cast<void*>(base));
    std::fprintf(stderr, "  0x%llx\n",
                 (unsigned long long)(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) - base + 0x140000000ull));
    void* frames[40];
    const USHORT n = CaptureStackBackTrace(0, 40, frames, nullptr);
    for (USHORT i = 0; i < n; i++)
        std::fprintf(stderr, "  0x%llx\n", (unsigned long long)(reinterpret_cast<uintptr_t>(frames[i]) - base + 0x140000000ull));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(onCrash);
#endif
    const std::string out = argc > 1 ? argv[1] : "screenshot";
    std::filesystem::create_directories(out);
    sim::setOutput(out);
    sim::setSize(scheda::Display::WIDTH, scheda::Display::HEIGHT);
    app::memoriaInit();
    app::eventsInit();
    scheda::tastiera().begin();
#if CONFIG_SCHEDA_TDECK_PLUS
    sim::makeTiles(true);
#endif
    sim::fillData();
    script();
    ui::init();  // non torna: il compito dell'interfaccia gira finche' il copione non finisce
    return 0;
}
#endif  // SIM_WEB
