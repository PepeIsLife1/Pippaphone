// Simulatore di rete PippaNet per il pianificatore (WebAssembly).
//
// Usa il protocollo vero (componenti/pippanet: Phone, Repeater) e un'aria simulata come
// test/sim.h, ma piu' vicina a una radio SX1262 vera:
//  - collegamenti presi dalla mappa: RSSI, SNR e margine per verso e canale;
//  - "fortuna del posto": ogni coppia di dispositivi ha uno scarto fisso dalla mappa (normale,
//    sigma come l'incertezza del modello), uguale nei due versi e sui due canali, perche' il
//    percorso e' lo stesso. Un collegamento a 0 dB quindi o va quasi sempre o quasi mai, come
//    nella realta', invece di andare a caso un pacchetto si' e uno no;
//  - fading di ogni pacchetto (Rice: K 10 dB fra ripetitori fermi, 3 dB se c'e' un telefono in
//    mano) e curva di errore ripida attorno all'SNR minimo;
//  - aggancio come l'SX1262: il ricevitore si aggancia al primo preambolo che sente. Un segnale
//    piu' forte di 6 dB gli ruba il ricevitore solo se arriva prima degli ultimi 5 simboli del
//    preambolo; se arriva dopo, il primo si rovina (se non e' 6 dB sopra) e il secondo e' perso;
//  - CAD prima di trasmettere come radio_link.cpp: se il canale e' occupato si riprova fra
//    100 e 700 ms;
//  - passo di 1 ms (a SF6 un simbolo dura 1 ms);
//  - un registro di eventi in JSON per l'animazione nella pagina.
//
// Le chiamate in diretta sono quelle del firmware (pn::Phone::call, a turni): qui ci sono solo
// la persona che risponde dopo qualche secondo e parla tenendo premuto il PTT (un pacchetto ogni
// 400 o 480 ms, come il microfono) e le misure della voce arrivata.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "pippanet/phone.h"
#include "pippanet/repeater.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define ESPORTA extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define ESPORTA extern "C"
#endif

namespace {
std::mt19937 g_caso(1);
}

namespace pn {
void randomBytes(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = uint8_t(g_caso());
}
}  // namespace pn

namespace {

using pn::Channel;
using pn::Millis;

// ---- Chiamate in diretta (quelle del firmware): cosa fa la persona e cosa si misura ----
struct TelSim {
    int chiamata = -1;
    bool parla = false, vuole = false;  // vuole: tiene premuto il PTT ma parla ancora l'altro
    uint8_t turno = 0, indice = 0;      // gli stessi numeri del firmware, per riconoscere i pezzi
    Millis prossimoPezzo = 0, rispondeAlle = 0;
};

struct Chiamata {
    int a = -1, b = -1;  // nodi: chi chiama, chi e' chiamato
    Millis inizio = 0, connessa = 0, fine = 0;
    std::string esito = "squilla";
    Millis msPacchetto = 400;
    size_t byte = 0;
    uint8_t salti = 0;
    bool ridondanza = false;
    // [0] da a verso b, [1] da b verso a: pezzi di voce creati e arrivati, per (turno << 8 | indice).
    std::map<uint32_t, Millis> creati[2], arrivati[2];
};

struct Misura {
    int pezzi = 0, arrivati = 0;
    Millis buco = 0, mediano = 0, p95 = 0;
};

std::string json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o + "\"";
}

const char* nomeTipo(pn::Type t) {
    switch (t) {
        case pn::Type::CALL_REQ: return "chiamata";
        case pn::Type::CALL_RING: return "squillo";
        case pn::Type::CALL_ANSWER: return "risposta alla chiamata";
        case pn::Type::CALL_VOICE: return "voce in diretta";
        case pn::Type::CALL_END: return "fine chiamata";
        case pn::Type::DM_TEXT: return "messaggio";
        case pn::Type::DM_ACK: return "conferma";
        case pn::Type::GROUP_TEXT: return "gruppo";
        case pn::Type::GLOBAL_TEXT: return "a tutti";
        case pn::Type::CERT_REQ: return "chiede certificato";
        case pn::Type::CERT_RESP: return "certificato";
        case pn::Type::POS_REQ: return "chiede posizione";
        case pn::Type::POS_RESP: return "posizione";
        case pn::Type::SOS: return "SOS";
        case pn::Type::GROUP_INVITE: return "invito gruppo";
        case pn::Type::BEACON: return "beacon";
        case pn::Type::PROV_HELLO: return "nuovo telefono";
        case pn::Type::PROV_CERT: return "numero assegnato";
        case pn::Type::VOICE_REQ: return "chiede vocale";
        case pn::Type::VOICE_ACCEPT: return "accetta vocale";
        case pn::Type::VOICE_DATA: return "pezzo di vocale";
        case pn::Type::VOICE_END: return "fine vocale";
        default: return "amministrazione";
    }
}

const char* nomeConsegna(pn::Delivery d) {
    switch (d) {
        case pn::Delivery::PENDING: return "in attesa";
        case pn::Delivery::SENT: return "trasmesso";
        case pn::Delivery::DELIVERED: return "consegnato";
        case pn::Delivery::READ: return "letto";
        case pn::Delivery::RETRYING: return "riprova piu' tardi";
        default: return "non raggiungibile";
    }
}

const char* nomeStatoVocale(pn::VoiceSendState s) {
    switch (s) {
        case pn::VoiceSendState::REQUESTING: return "chiede il canale";
        case pn::VoiceSendState::SENDING: return "in invio";
        case pn::VoiceSendState::DELIVERED: return "consegnato";
        case pn::VoiceSendState::PARTIAL: return "arrivato in parte";
        case pn::VoiceSendState::BUSY: return "occupato";
        default: return "non raggiungibile";
    }
}

struct Eventi {
    std::string buf;
    void add(const std::string& e) {
        if (!buf.empty()) buf += ",";
        buf += e;
    }
};

class Rete;

struct Ascoltatore : pn::PhoneListener {
    Rete* rete;
    int n;
    Ascoltatore(Rete* r, int i) : rete(r), n(i) {}
    void evento(const std::string& corpo);
    void onText(pn::Addr from, uint32_t id, const pn::TextPayload& t) override {
        evento("\"e\":\"testo\",\"da\":" + std::to_string(from) + ",\"id\":" + std::to_string(id) + ",\"testo\":" + json(t.text));
    }
    void onGlobalText(pn::Addr from, uint32_t id, const pn::TextPayload& t) override {
        evento("\"e\":\"globale\",\"da\":" + std::to_string(from) + ",\"id\":" + std::to_string(id) + ",\"testo\":" + json(t.text));
    }
    void onDelivery(pn::Addr to, uint32_t id, pn::Delivery d) override {
        evento("\"e\":\"consegna\",\"a\":" + std::to_string(to) + ",\"id\":" + std::to_string(id) + ",\"stato\":" +
               json(nomeConsegna(d)));
    }
    void onSos(pn::Addr from, const pn::SosPayload& s) override {
        evento("\"e\":\"sos\",\"da\":" + std::to_string(from) + ",\"attivo\":" + (s.active ? "true" : "false") +
               ",\"giro\":" + std::to_string(s.round));
    }
    void onSosAck(pn::Addr from) override { evento("\"e\":\"sosVisto\",\"da\":" + std::to_string(from)); }
    void onVoiceIncoming(pn::Addr from, const pn::VoiceReq&) override {
        evento("\"e\":\"vocaleInArrivo\",\"da\":" + std::to_string(from));
    }
    void onVoice(pn::Addr from, pn::Codec, const std::vector<uint8_t>& d, uint16_t ms, uint32_t mancanti) override {
        evento("\"e\":\"vocale\",\"da\":" + std::to_string(from) + ",\"byte\":" + std::to_string(d.size()) +
               ",\"ms\":" + std::to_string(ms) + ",\"mancanti\":" + std::to_string(mancanti));
    }
    void onVoiceState(pn::Addr to, uint32_t id, pn::VoiceSendState s) override {
        evento("\"e\":\"statoVocale\",\"a\":" + std::to_string(to) + ",\"id\":" + std::to_string(id) + ",\"stato\":" +
               json(nomeStatoVocale(s)));
    }
    void onCallState(pn::Addr peer, pn::CallState s, pn::CallResult r) override;
    void onCallVoice(pn::Addr from, uint8_t turn, uint8_t index, const uint8_t* data, size_t len, uint8_t frames,
                     const uint8_t* prev, size_t prevLen, uint8_t prevFrames, bool last) override;
};

struct Collegamento {
    float rssi, snr, prob, margine;
    float scarto = 0;  // dB, la "fortuna del posto" (uguale nei due versi e sui due canali)
};

constexpr float CATTURA_DB = 6.0f;      // un segnale vince se e' almeno 6 dB sopra gli altri
constexpr float SENTE_DB = -2.0f;       // margine minimo per agganciare un preambolo
constexpr float CAD_DB = -3.0f;         // margine minimo perche' il CAD veda il canale occupato
constexpr float SNR_MAX_LETTO = 12.0f;  // l'SX1262 non legge SNR piu' alti
constexpr int SIMBOLI_FINALI = 5;       // negli ultimi 5 simboli del preambolo l'aggancio e' fatto
constexpr float NESSUNO = -999.0f;

struct Trasmissione {
    uint32_t id;
    int da;
    Channel ch;
    Millis inizio, fine;
    Millis finePreambolo;  // fino a qui un segnale piu' forte puo' ancora rubare il ricevitore
    pn::Frame f;
    std::vector<float> p;   // potenza a ogni nodo con fading (dBm), NESSUNO se non arriva
    std::vector<float> m;   // margine a ogni nodo con fading (dB)
    std::vector<float> snr;
    std::vector<uint8_t> buono;  // estrazione della curva d'errore (CRC giusto se non disturbato)
};

struct Ricevitore {
    uint32_t aggancio = 0;  // id della trasmissione agganciata, 0 = libero
    Millis fine = 0;
    float p = NESSUNO;
};

struct Statistiche {
    uint32_t tx = 0, rxOk = 0, collisioni = 0, persi = 0, sordo = 0;
    uint64_t ariaMs = 0;
};

class Rete {
public:
    Eventi eventi;
    Millis ora = 0;
    pn::Identity admin = pn::Identity::generate();

    int telefono(pn::Addr numero) {
        const int i = int(nodi_.size());
        auto asc = std::make_unique<Ascoltatore>(this, i);
        auto t = std::make_unique<pn::Phone>(pn::Identity::generate(), *asc);
        pn::Cert c;
        c.number = numero;
        c.edPublic = t->identity().edPublic;
        c.xPublic = t->identity().xPublic;
        c.issued = 1790000000;
        c.signWith(admin);
        t->setCredentials(c, admin.edPublic);
        nodi_.push_back(t.get());
        telefoni_[i] = {std::move(t), std::move(asc)};
        nuovoNodo();
        return i;
    }
    int ripetitore(pn::Addr indirizzo) {
        const int i = int(nodi_.size());
        auto r = std::make_unique<pn::Repeater>(indirizzo);
        nodi_.push_back(r.get());
        ripetitori_[i] = std::move(r);
        nuovoNodo();
        return i;
    }
    void semina(uint32_t seme) { caso_.seed(seme ^ 0x5eed1234u); }
    void variabilita(float sigma) { sigma_ = sigma; }
    float scarto(int a, int b) const {
        auto it = scarti_.find({std::min(a, b), std::max(a, b)});
        return it == scarti_.end() ? 0.0f : it->second;
    }
    pn::Phone* tel(int i) {
        auto it = telefoni_.find(i);
        return it == telefoni_.end() ? nullptr : it->second.first.get();
    }
    void certificati() {
        for (auto& [i, a] : telefoni_)
            for (auto& [j, b] : telefoni_)
                if (i != j) a.first->addKnownCert(b.first->ownCert());
    }
    void collega(int da, int a, int ch, Collegamento c) {
        const std::pair<int, int> coppia{std::min(da, a), std::max(da, a)};
        auto it = scarti_.find(coppia);
        if (it == scarti_.end())
            it = scarti_.emplace(coppia, sigma_ > 0 ? std::normal_distribution<float>(0, sigma_)(caso_) : 0.0f).first;
        c.scarto = it->second;
        link_[{da, a, ch}] = c;
    }

    void avanza(Millis ms) {
        const Millis fine = ora + ms;
        while (ora < fine) passo();
    }

    std::string statistiche() const {
        std::string s = "{\"nodi\":[";
        for (size_t i = 0; i < nodi_.size(); i++) {
            const auto& st = stat_[i];
            if (i) s += ",";
            s += "{\"tx\":" + std::to_string(st.tx) + ",\"ariaMs\":" + std::to_string(st.ariaMs) +
                 ",\"rxOk\":" + std::to_string(st.rxOk) + ",\"collisioni\":" + std::to_string(st.collisioni) +
                 ",\"persi\":" + std::to_string(st.persi) + ",\"sordo\":" + std::to_string(st.sordo);
            auto it = ripetitori_.find(int(i));
            if (it != ripetitori_.end()) {
                s += ",\"ripetuti\":" + std::to_string(it->second->relayed) + ",\"soppressi\":" +
                     std::to_string(it->second->suppressed) + ",\"recuperati\":" + std::to_string(it->second->rescued);
            }
            s += ",\"canale\":" + std::to_string(int(canale(int(i)))) + "}";
        }
        return s + "],\"ora\":" + std::to_string(ora) + "}";
    }

    // ---- chiamate in diretta (quelle del firmware) ----
    Millis squilloMs = 4000;  // dopo quanto risponde chi e' chiamato

    int chiama(int da, pn::Addr numero) {
        pn::Phone* t = tel(da);
        if (!t || ts_[size_t(da)].chiamata >= 0) return -1;
        int b = -1;
        for (const auto& [i, x] : telefoni_)
            if (x.first->address() == numero && i != da) b = i;
        if (b < 0) return -1;
        Chiamata c;
        c.a = da;
        c.b = b;
        c.inizio = ora;
        chiamate_.push_back(std::move(c));
        const int id = int(chiamate_.size()) - 1;
        ts_[size_t(da)] = TelSim();
        ts_[size_t(da)].chiamata = id;
        if (!t->call(numero, ora)) {
            chiamate_.back().fine = ora;
            chiamate_.back().esito = "occupato";
            ts_[size_t(da)].chiamata = -1;
            return -1;
        }
        return id;
    }
    // PTT premuto o lasciato. Se parla l'altro, si aspetta che finisca (come chi ripreme il tasto).
    void parla(int n, bool si) {
        if (!tel(n)) return;
        TelSim& s = ts_[size_t(n)];
        s.vuole = si;
        if (si) return provaAParlare(n);
        if (!s.parla) return;
        s.parla = false;
        tel(n)->callSendVoice(nullptr, 0, 0, true, ora);  // ultimo pacchetto (vuoto): tocca all'altro
    }
    void riattacca(int n) {
        pn::Phone* t = tel(n);
        if (!t) return;
        ts_[size_t(n)].parla = ts_[size_t(n)].vuole = false;
        t->endCall(ora);
    }
    int faseChiamata(int n) const {
        auto it = telefoni_.find(n);
        return it == telefoni_.end() ? -1 : int(it->second.first->callState());
    }
    const Chiamata* chiamata(int id) const {
        return id >= 0 && size_t(id) < chiamate_.size() ? &chiamate_[size_t(id)] : nullptr;
    }

    // Pezzi persi, buco piu' lungo e ritardo (dalla voce pronta all'arrivo) in un verso.
    Misura misura(const Chiamata& c, int d) const {
        Misura m;
        std::vector<Millis> rit;
        int fila = 0, peggiore = 0;
        for (const auto& [chiave, creato] : c.creati[d]) {
            if (!c.fine && creato + 3000 > ora) continue;  // forse ancora in viaggio
            m.pezzi++;
            auto it = c.arrivati[d].find(chiave);
            if (it == c.arrivati[d].end()) {
                peggiore = std::max(peggiore, ++fila);
                continue;
            }
            fila = 0;
            m.arrivati++;
            rit.push_back(it->second - creato);
        }
        m.buco = Millis(peggiore) * c.msPacchetto;
        if (!rit.empty()) {
            std::sort(rit.begin(), rit.end());
            m.mediano = rit[rit.size() / 2];
            m.p95 = rit[std::min(rit.size() - 1, rit.size() * 95 / 100)];
        }
        return m;
    }

    std::string chiamate() const {
        const Chiamata* u = chiamate_.empty() ? nullptr : &chiamate_.back();
        const size_t byte = u && u->byte ? u->byte : 70;
        std::string s = "{\"ms\":" + std::to_string(u ? u->msPacchetto : 400) + ",\"byte\":" + std::to_string(byte) +
                        ",\"aria\":" + std::to_string(pn::airtimeMs(pn::VOICE_PARAMS, byte)) + ",\"chiamate\":[";
        for (size_t i = 0; i < chiamate_.size(); i++) {
            const Chiamata& c = chiamate_[i];
            if (i) s += ",";
            s += "{\"id\":" + std::to_string(i) + ",\"a\":" + std::to_string(c.a) + ",\"b\":" + std::to_string(c.b) +
                 ",\"esito\":" + json(c.esito) +
                 ",\"attesa\":" + std::to_string(c.connessa ? long(c.connessa - c.inizio) : -1L) +
                 ",\"durata\":" + std::to_string(c.connessa ? long((c.fine ? c.fine : ora) - c.connessa) : 0L) +
                 ",\"ms\":" + std::to_string(c.msPacchetto) + ",\"byte\":" + std::to_string(c.byte) +
                 ",\"salti\":" + std::to_string(c.salti) + ",\"ridondanza\":" + (c.ridondanza ? "true" : "false") +
                 ",\"versi\":[";
            for (int d = 0; d < 2; d++) {
                const Misura m = misura(c, d);
                if (d) s += ",";
                s += "{\"pezzi\":" + std::to_string(m.pezzi) + ",\"arrivati\":" + std::to_string(m.arrivati) +
                     ",\"buco\":" + std::to_string(m.buco) + ",\"mediano\":" + std::to_string(m.mediano) +
                     ",\"p95\":" + std::to_string(m.p95) + "}";
            }
            s += "]}";
        }
        return s + "]}";
    }

    // Dal firmware (Ascoltatore): stato della chiamata e voce arrivata al telefono n.
    void statoChiamata(int n, pn::Addr con, pn::CallState st, pn::CallResult r) {
        TelSim& t = ts_[size_t(n)];
        pn::Phone* ph = tel(n);
        if (st == pn::CallState::INCOMING) {
            t = TelSim();
            t.chiamata = chiamataDi(con, ph->address());
            t.rispondeAlle = ora + squilloMs;
        }
        Chiamata* c = t.chiamata >= 0 ? &chiamate_[size_t(t.chiamata)] : nullptr;
        const bool chiama = c && c->a == n;
        const char* cosa = "";
        switch (st) {
            case pn::CallState::DIALING: cosa = "chiama"; break;
            case pn::CallState::RINGBACK: cosa = "sta squillando"; break;
            case pn::CallState::INCOMING: cosa = "squilla"; break;
            case pn::CallState::CONNECTING: cosa = chiama ? "ha risposto" : "risponde"; break;
            case pn::CallState::ACTIVE:
                cosa = "in chiamata";
                // Collegata quando chi chiama sente l'altro (chi risponde puo' sentire senza essere sentito).
                if (c && chiama && !c->connessa) {
                    c->connessa = ora;
                    c->esito = "in corso";
                    c->msPacchetto = ph->callPacketMs();
                    c->salti = ph->callHops();
                    c->ridondanza = ph->callRedundant();
                    const size_t voce = pn::codecBytes(pn::Codec::CODEC2_700C, ph->callFramesPerPacket());
                    c->byte = pn::HEADER_SIZE + pn::TAG_SIZE + 6 + voce * (c->ridondanza ? 2 : 1);
                }
                break;
            case pn::CallState::IDLE:
                switch (r) {
                    case pn::CallResult::ENDED:
                    case pn::CallResult::HUNG_UP: cosa = "finita"; break;
                    case pn::CallResult::REFUSED: cosa = "rifiutata"; break;
                    case pn::CallResult::BUSY: cosa = "occupato"; break;
                    case pn::CallResult::NO_ANSWER: cosa = "nessuna risposta"; break;
                    case pn::CallResult::UNREACHABLE: cosa = "non raggiungibile"; break;
                    case pn::CallResult::DROPPED: cosa = "caduta"; break;
                    case pn::CallResult::TOO_LONG: cosa = "durata massima"; break;
                    case pn::CallResult::MISSED: cosa = "persa"; break;
                    default: cosa = "finita"; break;
                }
                // L'esito e' quello di chi chiama (chi riceve vede "persa" se chi chiama rinuncia).
                if (c && !c->fine && (chiama || r == pn::CallResult::DROPPED || r == pn::CallResult::HUNG_UP)) {
                    c->fine = ora;
                    c->esito = cosa;
                }
                t = TelSim();
                break;
        }
        eventoChiamata(n, cosa, con);
    }
    void voceChiamata(int n, uint8_t turno, uint8_t indice, uint8_t frames, uint8_t prevFrames) {
        const TelSim& t = ts_[size_t(n)];
        if (t.chiamata < 0) return;
        Chiamata& c = chiamate_[size_t(t.chiamata)];
        const int d = c.a == n ? 1 : 0;  // arrivata a n: viene dall'altro
        if (frames) c.arrivati[d].emplace((uint32_t(turno) << 8) | indice, ora);
        if (prevFrames && indice > 0) c.arrivati[d].emplace((uint32_t(turno) << 8) | uint8_t(indice - 1), ora);  // resta il primo
    }

private:
    void nuovoNodo() {
        stat_.emplace_back();
        occupatoFino_.push_back(0);
        attesaCad_.push_back(0);
        rx_.emplace_back();
        ts_.emplace_back();
        inChiamata_.push_back(false);
    }

    Channel canale(int n) const { return nodi_[size_t(n)]->activeChannel(ora); }

    int chiamataDi(pn::Addr x, pn::Addr y) const {
        for (int i = int(chiamate_.size()) - 1; i >= 0; i--) {
            const Chiamata& c = chiamate_[size_t(i)];
            if (c.fine) continue;
            const pn::Addr a = nodi_[size_t(c.a)]->address(), b = nodi_[size_t(c.b)]->address();
            if ((a == x && b == y) || (a == y && b == x)) return i;
        }
        return -1;
    }
    void eventoChiamata(int n, const char* cosa, pn::Addr con) {
        eventi.add("{\"t\":" + std::to_string(ora) + ",\"n\":" + std::to_string(n) + ",\"e\":\"chiamata\",\"stato\":" +
                   json(cosa) + ",\"con\":" + std::to_string(con) + "}");
    }
    void provaAParlare(int n) {
        TelSim& s = ts_[size_t(n)];
        pn::Phone* t = tel(n);
        if (s.parla || !s.vuole || !t->callTalk(true, ora)) return;
        s.parla = true;
        s.turno++;
        s.indice = 0;
        s.prossimoPezzo = ora + t->callPacketMs();
    }
    // La persona: risponde dopo lo squillo, e mentre parla il microfono da' un pacchetto ogni 400/480 ms.
    void tickChiamate() {
        for (const auto& [n, x] : telefoni_) {
            TelSim& s = ts_[size_t(n)];
            pn::Phone& t = *x.first;
            if (s.rispondeAlle && ora >= s.rispondeAlle) {
                s.rispondeAlle = 0;
                if (t.callState() == pn::CallState::INCOMING) t.answerCall(ora);
            }
            if (s.vuole && !s.parla) provaAParlare(n);
            if (!s.parla || ora < s.prossimoPezzo) continue;
            if (t.callState() != pn::CallState::ACTIVE || !t.callTalking()) {
                s.parla = false;
                continue;
            }
            const uint8_t frames = t.callFramesPerPacket();
            uint8_t dati[64];
            const size_t len = pn::codecBytes(pn::Codec::CODEC2_700C, frames);
            for (size_t k = 0; k < len; k++) dati[k] = uint8_t(caso_());
            if (s.chiamata >= 0) {
                Chiamata& c = chiamate_[size_t(s.chiamata)];
                c.creati[c.a == n ? 0 : 1][(uint32_t(s.turno) << 8) | s.indice] = ora;
            }
            t.callSendVoice(dati, len, frames, false, ora);
            s.indice++;
            s.prossimoPezzo += t.callPacketMs();
        }
        // I ripetitori che passano al canale voce per una chiamata (per il registro della pagina).
        for (const auto& [n, r] : ripetitori_) {
            const bool ora_ = r->callActive(ora);
            if (ora_ && !inChiamata_[size_t(n)]) eventoChiamata(n, "sul canale voce", 0);
            inChiamata_[size_t(n)] = ora_;
        }
    }
    const Collegamento* trova(int da, int a, Channel ch) const {
        auto it = link_.find({da, a, int(ch)});
        return it == link_.end() ? nullptr : &it->second;
    }
    bool trasmetteDurante(int n, Millis a, Millis b) const {
        for (const auto& o : registro_)
            if (o.da == n && o.inizio < b && o.fine > a) return true;
        return false;
    }
    bool eTelefono(int n) const { return telefoni_.count(n) > 0; }

    // Fading di un pacchetto (dB): Rice con fattore K.
    float fading(float kDb) {
        const float k = std::pow(10.0f, kDb / 10.0f);
        const float los = std::sqrt(k / (k + 1)), s = std::sqrt(0.5f / (k + 1));
        std::normal_distribution<float> n(0, 1);
        const float re = los + s * n(caso_), im = s * n(caso_);
        return 10.0f * std::log10(std::max(re * re + im * im, 1e-6f));
    }

    void eventoRx(int rx, const Trasmissione& t, const char* esito) {
        eventi.add("{\"t\":" + std::to_string(ora) + ",\"e\":\"rx\",\"n\":" + std::to_string(rx) + ",\"daNodo\":" +
                   std::to_string(t.da) + ",\"ch\":" + std::to_string(int(t.ch)) + ",\"esito\":\"" + esito + "\"}");
    }

    // Una trasmissione comincia: chi e' libero e la sente si aggancia; un segnale molto piu' forte
    // puo' rubare un ricevitore ancora nel preambolo di un altro.
    void inizia(Trasmissione& t) {
        const size_t n = nodi_.size();
        t.p.assign(n, NESSUNO);
        t.m.assign(n, NESSUNO);
        t.snr.assign(n, 0);
        t.buono.assign(n, 0);
        for (int rx = 0; rx < int(n); rx++) {
            if (rx == t.da) continue;
            const Collegamento* c = trova(t.da, rx, t.ch);
            if (!c) continue;
            const float f = fading(eTelefono(rx) || eTelefono(t.da) ? 3.0f : 10.0f);
            t.p[rx] = c->rssi + c->scarto + f;
            t.m[rx] = c->margine + c->scarto + f;
            t.snr[rx] = std::min(SNR_MAX_LETTO, c->snr + c->scarto + f);
            // Curva d'errore: da quasi tutto perso a quasi tutto preso in circa 2 dB.
            const float pOk = 1.0f / (1.0f + std::exp(-t.m[rx] / 0.6f));
            t.buono[rx] = std::uniform_real_distribution<float>(0, 1)(caso_) < pOk;
            if (t.m[rx] < SENTE_DB) continue;
            if (canale(rx) != t.ch) continue;
            bool trasmette = false;
            for (const auto& o : aria_)
                if (o.da == rx) trasmette = true;
            if (trasmette) continue;
            Ricevitore& r = rx_[rx];
            const Trasmissione* prima = nullptr;
            if (r.aggancio && r.fine > ora)
                for (const auto& o : aria_)
                    if (o.id == r.aggancio) prima = &o;
            if (!prima || (ora < prima->finePreambolo && t.p[rx] >= r.p + CATTURA_DB)) {
                r.aggancio = t.id;
                r.fine = t.fine;
                r.p = t.p[rx];
            }
        }
    }

    // Una trasmissione finisce: arriva solo a chi era agganciato a lei, se nessun altro l'ha coperta.
    void finisce(const Trasmissione& t) {
        for (int rx = 0; rx < int(nodi_.size()); rx++) {
            if (rx == t.da || t.m[rx] < -12.0f) continue;
            Ricevitore& r = rx_[rx];
            const bool agganciato = r.aggancio == t.id;
            if (agganciato) r.aggancio = 0;
            if (canale(rx) != t.ch) continue;
            if (trasmetteDurante(rx, t.inizio, t.fine)) {
                stat_[rx].sordo++;
                eventoRx(rx, t, "occupato");
                continue;
            }
            if (!agganciato) {
                // Non sentito, o ricevitore preso da un altro pacchetto.
                if (t.m[rx] < SENTE_DB) { stat_[rx].persi++; eventoRx(rx, t, "perso"); }
                else { stat_[rx].collisioni++; eventoRx(rx, t, "collisione"); }
                continue;
            }
            double interf = 0;
            for (const auto& o : registro_) {
                if (o.id == t.id || o.ch != t.ch || !(o.inizio < t.fine && o.fine > t.inizio)) continue;
                if (rx < int(o.p.size()) && o.p[rx] > NESSUNO) interf += std::pow(10.0, o.p[rx] / 10.0);
            }
            if (interf > 0 && t.p[rx] < 10.0 * std::log10(interf) + CATTURA_DB) {
                stat_[rx].collisioni++;
                eventoRx(rx, t, "collisione");
                continue;
            }
            if (!t.buono[rx]) {
                stat_[rx].persi++;
                eventoRx(rx, t, "perso");
                continue;
            }
            stat_[rx].rxOk++;
            eventoRx(rx, t, "ok");
            pn::RxInfo info{int16_t(std::lround(t.p[rx])), t.snr[rx], t.ch};
            nodi_[rx]->onReceive(t.f.data, t.f.len, info, ora);
        }
    }

    void passo() {
        ora += 1;
        for (auto* n : nodi_) n->tick(ora);
        tickChiamate();
        // Fine delle trasmissioni.
        for (auto it = aria_.begin(); it != aria_.end();) {
            if (it->fine > ora) { ++it; continue; }
            const Trasmissione t = *it;
            finisce(t);
            nodi_[t.da]->onTransmitted(t.f.data, t.f.len, ora);
            it = aria_.erase(it);
        }
        // Nuove trasmissioni, dopo il CAD (come radio_link.cpp: canale occupato -> riprova fra 100 e
        // 700 ms, o fra 10 e 30 durante una chiamata).
        for (int i = 0; i < int(nodi_.size()); i++) {
            if (occupatoFino_[i] > ora || attesaCad_[i] > ora) continue;
            const Channel ch = canale(i);
            if (!nodi_[i]->hasTx(ora, ch)) continue;
            bool occupato = false;
            for (const auto& t : aria_)
                if (t.ch == ch && t.da != i && t.m[i] >= CAD_DB) { occupato = true; break; }
            if (occupato) {
                if (nodi_[i]->liveVoice(ora, ch))
                    attesaCad_[i] = ora + 10 + std::uniform_int_distribution<int>(0, 19)(caso_);
                else
                    attesaCad_[i] = ora + 100 + std::uniform_int_distribution<int>(0, 599)(caso_);
                continue;
            }
            pn::Frame f;
            if (!nodi_[i]->popTx(ora, ch, f)) continue;
            const pn::RadioParams& rp = pn::paramsFor(ch);
            const Millis air = pn::airtimeMs(rp, f.len);
            const double tsymMs = double(1u << rp.sf) / rp.bwKHz;
            Trasmissione t{++ultimoId_, i, ch, ora, ora + air,
                           ora + Millis(std::max(0.0, (rp.preamble - SIMBOLI_FINALI) * tsymMs)), f, {}, {}, {}, {}};
            inizia(t);
            aria_.push_back(t);
            registro_.push_back(t);
            occupatoFino_[i] = ora + air + 20;
            stat_[i].tx++;
            stat_[i].ariaMs += air;
            pn::Header h;
            pn::decodeHeader(f.data, f.len, h);
            eventi.add("{\"t\":" + std::to_string(ora) + ",\"e\":\"tx\",\"n\":" + std::to_string(i) + ",\"ch\":" +
                       std::to_string(int(ch)) + ",\"tipo\":" + json(nomeTipo(h.type)) + ",\"src\":" + std::to_string(h.src) +
                       ",\"dst\":" + std::to_string(h.dst) + ",\"seq\":" + std::to_string(h.seq) + ",\"salti\":" +
                       std::to_string(h.hops) + ",\"via\":" + std::to_string(h.via) + ",\"byte\":" + std::to_string(f.len) +
                       ",\"aria\":" + std::to_string(air) + "}");
        }
        while (!registro_.empty() && registro_.front().fine + 5000 < ora) registro_.erase(registro_.begin());
    }

    std::vector<pn::Node*> nodi_;
    std::map<int, std::pair<std::unique_ptr<pn::Phone>, std::unique_ptr<Ascoltatore>>> telefoni_;
    std::map<int, std::unique_ptr<pn::Repeater>> ripetitori_;
    std::map<std::tuple<int, int, int>, Collegamento> link_;
    std::map<std::pair<int, int>, float> scarti_;
    std::vector<Trasmissione> aria_, registro_;
    std::vector<Millis> occupatoFino_, attesaCad_;
    std::vector<Ricevitore> rx_;
    std::vector<Statistiche> stat_;
    std::mt19937 caso_{7};
    float sigma_ = 0;
    uint32_t ultimoId_ = 0;
    // Chiamate: cosa fa ogni persona, chiamate fatte, ripetitori sul canale voce per una chiamata.
    std::vector<TelSim> ts_;
    std::vector<Chiamata> chiamate_;
    std::vector<bool> inChiamata_;
};

void Ascoltatore::evento(const std::string& corpo) {
    rete->eventi.add("{\"t\":" + std::to_string(rete->ora) + ",\"n\":" + std::to_string(n) + "," + corpo + "}");
}
void Ascoltatore::onCallState(pn::Addr peer, pn::CallState s, pn::CallResult r) { rete->statoChiamata(n, peer, s, r); }
void Ascoltatore::onCallVoice(pn::Addr, uint8_t turn, uint8_t index, const uint8_t*, size_t, uint8_t frames,
                              const uint8_t*, size_t, uint8_t prevFrames, bool) {
    rete->voceChiamata(n, turn, index, frames, prevFrames);
}

std::unique_ptr<Rete> g_rete;
std::string g_uscita;

}  // namespace

ESPORTA void sim_nuova(uint32_t seme) {
    g_caso.seed(seme);
    g_rete = std::make_unique<Rete>();
    g_rete->semina(seme);
}

// Incertezza della mappa (dB): ogni coppia di dispositivi pesca il suo scarto. 0 = la mappa e' esatta.
// Va chiamata prima dei collegamenti.
ESPORTA void sim_variabilita(float sigma) { g_rete->variabilita(sigma); }
ESPORTA float sim_scarto(int a, int b) { return g_rete->scarto(a, b); }

ESPORTA void sim_profilo(float fM, float bwM, int sfM, float fV, float bwV, int sfV) {
    pn::MAIN_PARAMS = {fM, bwM, uint8_t(sfM), 5, 16, 22};
    pn::VOICE_PARAMS = {fV, bwV, uint8_t(sfV), 5, 12, 22};
}

ESPORTA int sim_telefono(int numero) { return g_rete->telefono(pn::Addr(numero)); }
ESPORTA int sim_ripetitore(int numero) { return g_rete->ripetitore(pn::Addr(pn::ADDR_REPEATER_FIRST + numero)); }

ESPORTA void sim_posizione(int n, double lat, double lon) {
    if (pn::Phone* t = g_rete->tel(n)) {
        t->position.valid = true;
        t->position.latE7 = int32_t(std::lround(lat * 1e7));
        t->position.lonE7 = int32_t(std::lround(lon * 1e7));
        t->position.accM = 5;
    }
}

ESPORTA void sim_collegamento(int da, int a, int canale, float rssi, float snr, float prob, float margine) {
    g_rete->collega(da, a, canale, {rssi, snr, prob, margine});
}

ESPORTA void sim_certificati() { g_rete->certificati(); }

ESPORTA void sim_avanza(int ms) { g_rete->avanza(Millis(ms)); }

ESPORTA double sim_ora() { return double(g_rete->ora); }

ESPORTA const char* sim_eventi() {
    g_uscita = "[" + g_rete->eventi.buf + "]";
    g_rete->eventi.buf.clear();
    return g_uscita.c_str();
}

ESPORTA const char* sim_statistiche() {
    g_uscita = g_rete->statistiche();
    return g_uscita.c_str();
}

ESPORTA uint32_t sim_testo(int da, int numeroA, const char* testo) {
    pn::Phone* t = g_rete->tel(da);
    return t ? t->sendText(pn::Addr(numeroA), testo, g_rete->ora) : 0;
}

ESPORTA uint32_t sim_globale(int da, const char* testo) {
    pn::Phone* t = g_rete->tel(da);
    return t ? t->sendGlobalText(testo, g_rete->ora) : 0;
}

ESPORTA uint32_t sim_vocale(int da, int numeroA, int secondi) {
    pn::Phone* t = g_rete->tel(da);
    if (!t) return 0;
    std::vector<uint8_t> dati(size_t(secondi) * 100);  // Codec2 700C: 4 byte ogni 40 ms
    pn::randomBytes(dati.data(), dati.size());
    return t->sendVoice(pn::Addr(numeroA), pn::Codec::CODEC2_700C, dati, uint16_t(secondi * 1000), g_rete->ora);
}

ESPORTA void sim_sos(int da, int attivo, const int* contatti, int n) {
    pn::Phone* t = g_rete->tel(da);
    if (!t) return;
    if (!attivo) return t->stopSos(g_rete->ora);
    std::vector<pn::Addr> c(contatti, contatti + n);
    t->startSos(c, g_rete->ora);
}

// Chiamate in diretta (quelle del firmware): squillo = dopo quanti ms risponde chi e' chiamato.
ESPORTA void sim_chiamata_imp(int squillo) { g_rete->squilloMs = Millis(std::max(0, squillo)); }
ESPORTA int sim_chiama(int da, int numeroA) { return g_rete->chiama(da, pn::Addr(numeroA)); }
ESPORTA void sim_parla(int n, int si) { g_rete->parla(n, si != 0); }
ESPORTA void sim_riattacca(int n) { g_rete->riattacca(n); }
// pn::CallState: 0 libero, 1 chiama, 2 squilla dall'altro, 3 mi chiamano, 4 collegamento, 5 in chiamata.
ESPORTA int sim_fase_chiamata(int n) { return g_rete->faseChiamata(n); }
ESPORTA const char* sim_chiamate() {
    g_uscita = g_rete->chiamate();
    return g_uscita.c_str();
}

#ifndef __EMSCRIPTEN__
// Prova sul PC:  simulatore [sf] [prove] [sigma] [perdita in piu' (dB)]
// Rete di esempio: telefono A - R1 - R2 - telefono C, con R3 vicino a R2 (due ripetitori che
// sentono le stesse cose) e un telefono B che manda in contemporanea a A. Per ogni prova una
// rete nuova (altra fortuna dei posti): A manda un messaggio a C, B uno ad A, poi A un vocale
// di 10 s a C. Conta consegne e tempi.
#include <chrono>
#include <cstdlib>

static float rumoreDbm(float bwKHz) { return -174.0f + 10.0f * std::log10(bwKHz * 1000.0f) + 6.0f; }

// Tempo (ms) del primo evento che contiene tutte le parti, o -1.
static long primo(const std::string& ev, std::initializer_list<const char*> parti) {
    size_t pos = 0;
    while ((pos = ev.find("{\"t\":", pos)) != std::string::npos) {
        const size_t fine = ev.find('}', pos);
        const std::string e = ev.substr(pos, fine - pos);
        bool tutte = true;
        for (const char* p : parti) tutte = tutte && e.find(p) != std::string::npos;
        if (tutte) return std::atol(e.c_str() + 5);
        pos = fine;
    }
    return -1;
}

static double mediana(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// CHIAMATA=1: chiamate in diretta su cinque reti, con il codice del firmware. A chiama C, C
// risponde dopo 4 s; parlano a turni (10 s a testa, quattro turni); dopo 5 s B scrive a C.
// Per provare altri pacchetti oltre un ripetitore: compilare con -DPN_CALL_FRAMES_FAR=13 (o altro).
static int provaChiamate(int sf, int prove, float sigma, float extra) {
    const float bwM = 62.5f, bwV = 125.0f;
    const char* nomi[] = {"diretto (con R1 vicino)", "1 ripetitore", "2 ripetitori in fila", "2 in fila + 1 parallelo",
                          "3 ripetitori in fila"};
    std::printf("SF%d, voce a %.0f kHz, sigma %.0f dB, %+.0f dB, %d prove per rete, a turni; oltre un ripetitore %d frame\n",
                sf, double(bwV), double(sigma), double(extra), prove, int(pn::CALL_FRAMES_FAR));
    for (int topo = 0; topo < 5; topo++) {
        int connesse = 0, cadute = 0, msgOk = 0, classi[3] = {0, 0, 0};
        long pezzi[2] = {0, 0}, arrivati[2] = {0, 0};
        std::vector<double> attese, mediani, p95, buchi, tMsg;
        Millis msPacchetto = 400;
        size_t byte = 0;
        bool rid = false;
        for (int p = 0; p < prove; p++) {
            sim_nuova(uint32_t(7000 + 100 * topo + p));
            sim_profilo(869.525f, bwM, sf, 869.85f, bwV, sf);
            sim_variabilita(sigma);
            const int a = sim_telefono(1001), r1 = sim_ripetitore(1), r2 = sim_ripetitore(2), r3 = sim_ripetitore(3),
                      r4 = sim_ripetitore(4);
            const int c = sim_telefono(1002), b = sim_telefono(1003);
            auto lega = [&](int x, int y, float perdita) {
                for (int chn = 0; chn < 2; chn++) {
                    const float bw = chn ? bwV : bwM;
                    const float rssi = 24.0f - perdita - extra, snr = rssi - rumoreDbm(bw);
                    const float m = snr + 2.5f * float(sf - 4);
                    sim_collegamento(x, y, chn, rssi, snr, 0, m);
                    sim_collegamento(y, x, chn, rssi, snr, 0, m);
                }
            };
            lega(a, r1, 140);
            lega(b, r1, 136);
            lega(b, a, 150);
            switch (topo) {
                case 0: lega(a, c, 139); lega(r1, c, 140); break;
                case 1: lega(r1, c, 140); break;
                case 2: lega(r1, r2, 138); lega(r2, c, 141); break;
                case 3: lega(r1, r2, 138); lega(r2, c, 141); lega(r3, c, 143); lega(r1, r3, 139); lega(r2, r3, 115); break;
                default: lega(r1, r2, 138); lega(r2, r4, 139); lega(r4, c, 140); break;
            }
            sim_certificati();
            sim_avanza(60000);
            sim_eventi();
            sim_chiamata_imp(4000);
            const int id = sim_chiama(a, 1002);
            if (id < 0) continue;
            for (int k = 0; k < 600 && !g_rete->chiamata(id)->connessa && !g_rete->chiamata(id)->fine; k++) sim_avanza(100);
            // DUMP=rete.prova (es. 3.1): stampa gli eventi dell'apertura di quella chiamata.
            if (const char* d = std::getenv("DUMP"))
                if (std::atof(d) == topo + p / 10.0 || (std::atoi(d) == topo && std::string(d).find('.') == std::string::npos)) {
                    const std::string ev = sim_eventi();
                    size_t pos = 0;
                    while ((pos = ev.find("{\"t\":", pos)) != std::string::npos) {
                        const size_t fine = ev.find('}', pos);
                        const std::string e = ev.substr(pos, fine - pos + 1);
                        if (e.find("beacon") == std::string::npos) std::printf("  %s\n", e.c_str());
                        pos = fine;
                    }
                    std::printf("prova %d: %s\n", p, g_rete->chiamata(id)->esito.c_str());
                }
            if (!g_rete->chiamata(id)->connessa) {
                if (std::getenv("VERBOSO")) std::printf("  prova %d non collegata: %s\n", p, g_rete->chiamata(id)->esito.c_str());
                sim_avanza(30000);
                sim_eventi();
                continue;
            }
            connesse++;
            attese.push_back(double(g_rete->chiamata(id)->connessa - g_rete->chiamata(id)->inizio) / 1000 - 4);
            long inizioMsg = -1;
            sim_parla(a, 1); sim_avanza(5000);
            inizioMsg = long(sim_ora());
            sim_testo(b, 1002, "sei libero stasera? chiamami quando puoi");
            sim_avanza(5000); sim_parla(a, 0); sim_avanza(1000);
            for (int giro = 0; giro < 3; giro++) {
                const int chi = giro % 2 ? a : c;
                sim_parla(chi, 1); sim_avanza(10000); sim_parla(chi, 0); sim_avanza(1000);
            }
            sim_riattacca(a);
            sim_avanza(300000);  // il messaggio di B, se non e' passato, riprova 3,5 minuti dopo
            const std::string ev = sim_eventi();
            const long tm = primo(ev, {"\"e\":\"consegna\"", "\"a\":1002", "\"stato\":\"consegnato\""});
            if (tm >= 0) { msgOk++; tMsg.push_back(double(tm - inizioMsg) / 1000); }
            const Chiamata& ch = *g_rete->chiamata(id);
            if (ch.esito == "caduta") cadute++;
            // CADUTE=1: per le chiamate cadute, gli stati dei due telefoni e quando.
            if (ch.esito == "caduta" && std::getenv("CADUTE")) {
                std::printf("  prova %d caduta a %.1f s dalla risposta:", p, double(ch.fine - ch.connessa) / 1000);
                size_t pos = 0;
                while ((pos = ev.find("\"e\":\"chiamata\"", pos)) != std::string::npos) {
                    const size_t ini = ev.rfind("{\"t\":", pos), fine = ev.find('}', pos);
                    std::printf(" %s", ev.substr(ini, fine - ini + 1).c_str());
                    pos = fine;
                }
                std::printf("\n");
                if (std::getenv("CADUTE")[0] == '2')
                    for (size_t q = 0; (q = ev.find("{\"t\":", q)) != std::string::npos; q++) {
                        const std::string e = ev.substr(q, ev.find('}', q) - q + 1);
                        const long t = std::atol(e.c_str() + 5);
                        if (t >= long(ch.connessa) - 1500 && t <= long(ch.connessa) + 7000 &&
                            e.find("beacon") == std::string::npos)
                            std::printf("    %s\n", e.c_str());
                    }
            }
            msPacchetto = ch.msPacchetto;
            byte = ch.byte;
            rid = ch.ridondanza;
            double peggio = 0;
            for (int d = 0; d < 2; d++) {
                const Misura m = g_rete->misura(ch, d);
                pezzi[d] += m.pezzi;
                arrivati[d] += m.arrivati;
                buchi.push_back(double(m.buco) / 1000);
                if (m.arrivati) { mediani.push_back(double(m.mediano)); p95.push_back(double(m.p95)); }
                if (m.pezzi) peggio = std::max(peggio, 1.0 - double(m.arrivati) / m.pezzi);
            }
            // Com'e' andata la chiamata (verso peggiore): buona < 5% perso, si capisce < 15%, poi a scatti.
            classi[peggio < 0.05 ? 0 : peggio < 0.15 ? 1 : 2]++;
            (void)r2; (void)r3; (void)r4;
        }
        auto persi = [&](int d) { return pezzi[d] ? 100.0 * double(pezzi[d] - arrivati[d]) / double(pezzi[d]) : 0.0; };
        const double peggiore = buchi.empty() ? 0 : *std::max_element(buchi.begin(), buchi.end());
        std::printf("%-26s collegate %d/%d (oltre lo squillo %.1f s), cadute %d\n", nomi[topo], connesse, prove,
                    mediana(attese), cadute);
        if (!connesse) continue;
        std::printf("    pacchetto ogni %u ms, %zu byte (%u ms in aria)%s\n", unsigned(msPacchetto), byte,
                    unsigned(pn::airtimeMs(pn::VOICE_PARAMS, byte)), rid ? ", con ridondanza" : "");
        std::printf("    chiamate buone (<5%% perso) %d, si capisce (<15%%) %d, a scatti %d\n", classi[0], classi[1], classi[2]);
        std::printf("    voce persa A->C %.1f%%, C->A %.1f%%; buco piu' lungo: mediano %.1f s, peggiore %.1f s\n", persi(0),
                    persi(1), mediana(buchi), peggiore);
        std::printf("    ritardo della rete: mediano %.0f ms, 95%% entro %.0f ms -> dalla bocca all'orecchio ~%.1f s\n",
                    mediana(mediani), mediana(p95), (double(msPacchetto) + 40 + mediana(p95)) / 1000);
        std::printf("    messaggio di B a C durante la chiamata: consegnato %d/%d (mediano %.0f s, entro un minuto %d)\n",
                    msgOk, connesse, mediana(tMsg),
                    int(std::count_if(tMsg.begin(), tMsg.end(), [](double x) { return x < 60; })));
    }
    return 0;
}

int main(int argc, char** argv) {
    const int sf = argc > 1 ? std::atoi(argv[1]) : 7;
    const int prove = argc > 2 ? std::atoi(argv[2]) : 20;
    const float sigma = argc > 3 ? float(std::atof(argv[3])) : 9.0f;
    const float extra = argc > 4 ? float(std::atof(argv[4])) : 0.0f;
    if (std::getenv("CHIAMATA")) return provaChiamate(sf, prove, sigma, extra);
    const float bwM = 62.5f, bwV = 125.0f;
    int msgOk = 0, msg2Ok = 0, vocOk = 0, vocParz = 0, globA = 0, globB = 0, globTx = 0;
    double tMsg = 0, tVoc = 0;
    std::vector<double> lMsg, lVoc;
    const auto t0 = std::chrono::steady_clock::now();
    for (int p = 0; p < prove; p++) {
        sim_nuova(1000 + p);
        sim_profilo(869.525f, bwM, sf, 869.85f, bwV, sf);
        sim_variabilita(sigma);
        const int a = sim_telefono(1001), r1 = sim_ripetitore(1), r2 = sim_ripetitore(2), r3 = sim_ripetitore(3);
        const int c = sim_telefono(1002), b = sim_telefono(1003);
        // Perdita di percorso (dB) -> RSSI con 22 dBm e 2 dBi in tutto; margine dallo SF.
        auto lega = [&](int x, int y, float perdita) {
            for (int ch = 0; ch < 2; ch++) {
                const float bw = ch ? bwV : bwM;
                const float rssi = 24.0f - perdita - extra, snr = rssi - rumoreDbm(bw);
                const float m = snr + 2.5f * float(sf - 4);
                sim_collegamento(x, y, ch, rssi, snr, 0, m);
                sim_collegamento(y, x, ch, rssi, snr, 0, m);
            }
        };
        // FILA=1: niente R3 (una sola strada).
        const bool fila = std::getenv("FILA") != nullptr;
        lega(a, r1, 140); lega(r1, r2, 138); lega(r2, c, 141); lega(b, r1, 136); lega(b, a, 150);
        if (!fila) { lega(r3, c, 143); lega(r1, r3, 139); lega(r2, r3, 115); }
        sim_certificati();
        sim_avanza(60000);
        sim_eventi();
        // DUMP=msg o DUMP=voce: stampa trasmissioni ed esiti non riusciti della prima prova.
        auto stampa = [&](const std::string& ev, const char* fase) {
            const char* d = std::getenv("DUMP");
            const int quale = std::getenv("PROVA") ? std::atoi(std::getenv("PROVA")) : 0;
            if (!d || p != quale || std::string(d) != fase) return;
            size_t pos = 0;
            while ((pos = ev.find("{\"t\":", pos)) != std::string::npos) {
                const size_t fine = ev.find('}', pos);
                const std::string e = ev.substr(pos, fine - pos + 1);
                if (e.find("\"e\":\"rx\"") == std::string::npos || e.find("\"esito\":\"ok\"") == std::string::npos)
                    std::printf("%s\n", e.c_str());
                pos = fine;
            }
            std::printf("%s\n", sim_statistiche());
        };
        const long inizio = long(sim_ora());
        sim_testo(a, 1002, "ciao dal rifugio, tutto bene? ci vediamo alle otto al solito posto");
        sim_testo(b, 1001, "arrivo fra dieci minuti");
        sim_avanza(40000);
        std::string ev = sim_eventi();
        stampa(ev, "msg");
        const long t1 = primo(ev, {"\"e\":\"consegna\"", "\"a\":1002", "\"stato\":\"consegnato\""});
        if (t1 >= 0) { msgOk++; tMsg += double(t1 - inizio); lMsg.push_back(double(t1 - inizio) / 1000); }
        if (primo(ev, {"\"e\":\"consegna\"", "\"a\":1001", "\"stato\":\"consegnato\""}) >= 0) msg2Ok++;
        const long iv = long(sim_ora());
        sim_vocale(a, 1002, 10);
        sim_avanza(120000);
        ev = sim_eventi();
        stampa(ev, "voce");
        const long tv = primo(ev, {"\"e\":\"statoVocale\"", "\"a\":1002", "\"stato\":\"consegnato\""});
        if (tv >= 0) { vocOk++; tVoc += double(tv - iv); lVoc.push_back(double(tv - iv) / 1000); }
        else if (primo(ev, {"\"e\":\"statoVocale\"", "\"a\":1002", "\"stato\":\"arrivato in parte\""}) >= 0) vocParz++;
        // Messaggio a tutti da C (senza conferma): chi lo riceve, quante ripetizioni costa.
        sim_avanza(20000);
        sim_eventi();
        sim_globale(c, "domani si parte alle sette dal piazzale");
        sim_avanza(40000);
        ev = sim_eventi();
        stampa(ev, "tutti");
        globA += primo(ev, {"\"e\":\"globale\"", std::string("\"n\":" + std::to_string(a) + ",").c_str()}) >= 0;
        globB += primo(ev, {"\"e\":\"globale\"", std::string("\"n\":" + std::to_string(b) + ",").c_str()}) >= 0;
        for (size_t pos = 0; (pos = ev.find("\"tipo\":\"a tutti\"", pos)) != std::string::npos; pos++) globTx++;
    }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto med = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    std::printf("SF%d, sigma %.0f dB, %+.0f dB, %d prove (%.1f s di calcolo)\n", sf, double(sigma), double(extra), prove, sec);
    std::printf("  messaggio A->C (3 salti): consegnato %d/%d, tempo medio %.1f s, mediano %.1f s\n", msgOk, prove,
                msgOk ? tMsg / msgOk / 1000 : 0.0, med(lMsg));
    std::printf("  messaggio B->A (insieme): consegnato %d/%d\n", msg2Ok, prove);
    std::printf("  vocale 10 s A->C: completo %d/%d (tempo medio %.1f s, mediano %.1f s), in parte %d\n", vocOk, prove,
                vocOk ? tVoc / vocOk / 1000 : 0.0, med(lVoc), vocParz);
    std::printf("  messaggio a tutti da C: arrivato ad A %d/%d, a B %d/%d, %.2f trasmissioni a prova\n", globA, prove, globB,
                prove, double(globTx) / prove);
    return 0;
}
#endif
