#include "app/giochi.h"

#include <algorithm>

#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "pippanet/node.h"
#include "pippanet/packet.h"

namespace app {

namespace {
constexpr uint8_t REC_MAGIC = 0x9A, REC_VERSIONE = 1;
constexpr int ESITO_ACQUA = 1, ESITO_COLPITO = 2, ESITO_AFFONDATO = 3, ESITO_FINE = 4;
// Id della partita di un messaggio (byte 2 e 3: il messaggio ne ha almeno 4).
uint16_t idDi(const std::vector<uint8_t>& m) { return uint16_t(m[2] | (m[3] << 8)); }
// Il messaggio m e' della partita (gioco g, id).
bool diPartita(const std::vector<uint8_t>& m, Gioco g, uint16_t id) {
    return m.size() >= 4 && m[0] == uint8_t(g) && idDi(m) == id;
}
}  // namespace

const char* nomeGioco(Gioco g) { return g == Gioco::TRIS ? "Tris" : "Battaglia navale"; }

Giochi& giochi() {
    static Giochi g;
    return g;
}

// ---------------------------------------------------------------- record

void Giochi::load() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    std::vector<uint8_t> b;
    if (!readFile(DATI_PATH("/giochi.bin"), b) || b.size() < 2 || b[0] != REC_MAGIC) return;
    pn::Reader r(b.data(), b.size());
    r.u8();
    r.u8();
    const uint32_t s = r.u32();
    uint32_t m[3];
    for (auto& x : m) x = r.u32();
    if (!r.ok()) return;
    recSerpente_ = s;
    for (int i = 0; i < 3; i++) recMine_[i] = m[i];
}

void Giochi::saveRecords() {
    uint8_t buf[32];
    pn::Writer w(buf, sizeof(buf));
    w.u8(REC_MAGIC);
    w.u8(REC_VERSIONE);
    w.u32(recSerpente_);
    for (uint32_t x : recMine_) w.u32(x);
    writeFile(DATI_PATH("/giochi.bin"), std::vector<uint8_t>(buf, buf + w.len()));
}

uint32_t Giochi::recordSerpente() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return recSerpente_;
}

bool Giochi::nuovoRecordSerpente(uint32_t punti) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (punti <= recSerpente_) return false;
    recSerpente_ = punti;
    saveRecords();
    return true;
}

uint32_t Giochi::recordMine(int livello) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return livello >= 0 && livello < 3 ? recMine_[livello] : 0;
}

bool Giochi::nuovoRecordMine(int livello, uint32_t secondi) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (livello < 0 || livello >= 3 || !secondi || (recMine_[livello] && secondi >= recMine_[livello])) return false;
    recMine_[livello] = secondi;
    saveRecords();
    return true;
}

// ---------------------------------------------------------------- flotta

bool Giochi::flottaACaso(uint8_t mare[100], uint32_t seme) {
    uint32_t x = seme ? seme : 1;
    auto rnd = [&x](uint32_t n) {
        x = x * 1103515245u + 12345u;
        return (x >> 16) % n;
    };
    for (int giro = 0; giro < 50; giro++) {
        std::fill(mare, mare + 100, uint8_t(0));
        bool ok = true;
        for (int n = 0; n < 10 && ok; n++) {
            const int len = FLOTTA[n];
            bool messa = false;
            for (int tent = 0; tent < 200 && !messa; tent++) {
                const bool oriz = rnd(2) == 0;
                const int cx = int(rnd(uint32_t(oriz ? 11 - len : 10))), cy = int(rnd(uint32_t(oriz ? 10 : 11 - len)));
                // Le navi non si toccano, neanche in diagonale.
                bool libero = true;
                for (int k = 0; k < len && libero; k++) {
                    const int px = cx + (oriz ? k : 0), py = cy + (oriz ? 0 : k);
                    for (int dy = -1; dy <= 1 && libero; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            const int qx = px + dx, qy = py + dy;
                            if (qx >= 0 && qx < 10 && qy >= 0 && qy < 10 && mare[qy * 10 + qx]) {
                                libero = false;
                                break;
                            }
                        }
                }
                if (!libero) continue;
                for (int k = 0; k < len; k++) mare[(cy + (oriz ? 0 : k)) * 10 + cx + (oriz ? k : 0)] = uint8_t(n + 1);
                messa = true;
            }
            ok = messa;
        }
        if (ok) return true;
    }
    return false;
}

// ---------------------------------------------------------------- sfide

std::vector<uint8_t> Giochi::msg(Tipo t, std::initializer_list<uint8_t> dati) const {
    std::vector<uint8_t> m = {uint8_t(s_.gioco), uint8_t(t), uint8_t(s_.id), uint8_t(s_.id >> 8)};
    m.insert(m.end(), dati.begin(), dati.end());
    return m;
}

void Giochi::manda(uint16_t to, const std::vector<uint8_t>& m, bool tieni) {
    uscita_.push_back({to, m, tieni});
    if (tieni) {
        ultimo_ = m;
        s_.inViaggio = true;
        s_.nonArrivata = false;
    }
}

void Giochi::servizio() {
    std::vector<Uscita> out;
    std::vector<std::pair<uint16_t, uint32_t>> via;
    {
        std::lock_guard<std::recursive_mutex> l(mu_);
        out.swap(uscita_);
        via.swap(annulla_);
    }
    // Senza il blocco dei giochi: la rete prende il suo (e dalla radio arriva prima il suo, poi il nostro).
    // Prima escono dai tentativi gli inviti che non servono piu', poi parte il resto (l'ABBANDONO).
    for (const auto& a : via) rete().cancelGame(a.first, a.second);
    for (const auto& u : out) {
        const uint32_t id = rete().sendGame(u.to, u.m);
        std::lock_guard<std::recursive_mutex> l(mu_);
        if (!u.tieni) {
            // L'esito di un colpo e le risposte che chiudono (ABBANDONO, RIFIUTO): se ne segue la
            // consegna (vedi consegna()).
            if (!ultimoEsito_.empty() && u.m == ultimoEsito_) {
                esitoId_ = id;
                esitoTo_ = u.to;
                esitoMsg_ = u.m;
            } else if (!rispostaAuto_.empty() && u.to == rispostaAutoTo_ && u.m == rispostaAuto_) {
                rispostaAutoId_ = id;
            }
            continue;
        }
        // La mossa che ha chiuso il tris: la si segue anche dopo che la partita si e' chiusa.
        if (!finale_.empty() && u.to == finaleTo_ && u.m == finale_) finaleId_ = id;
        // Un mio invito: se ne ricorda l'id, per toglierlo dai tentativi quando non serve piu'. Se
        // intanto la partita non lo aspetta piu' (accettato, rifiutato, abbandonato), esce subito.
        if (id && u.m.size() >= 2 && u.m[1] == INVITO) {
            if (s_.stato == Sfida::INVITATO && u.m == ultimo_) mieiInviti_.push_back(id);
            else annulla_.push_back({u.to, id});
        }
        // Partita chiusa o cambiata intanto (un abbandono arrivato dalla radio): la consegna di questo
        // messaggio non si segue piu'.
        if (u.m != ultimo_) continue;
        ultimoId_ = id;
        if (!id) {
            s_.inViaggio = false;
            s_.nonArrivata = true;
            evento(7);
        }
    }
}

void Giochi::evento(int32_t a) { post({Ev::GAME, {}, s_.avversario, s_.id, a}); }

void Giochi::nuovaIdentita(uint16_t numero) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (s_.avversario == numero) daLui_ = 0;
}

Sfida Giochi::sfida() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return s_;
}

bool Giochi::attiva() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return s_.stato != Sfida::NESSUNA && s_.stato != Sfida::FINITA;
}

bool Giochi::sfidaNuova(Gioco g, uint16_t avversario) {
    const uint16_t io = rete().number();  // prima del blocco dei giochi (la radio li prende al contrario)
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (attiva() || !pn::isPhoneAddr(avversario) || avversario == io) return false;
    ricordaChiusa();  // una partita finita ancora aperta si toglie
    ritiraInviti();
    s_ = Sfida();
    risultato_ = false;
    daLui_ = 0;
    s_.gioco = g;
    s_.avversario = avversario;
    s_.id = uint16_t(1 + pn::randomBelow(0xFFFE));
    s_.invitante = true;
    ultimo_.clear();
    ultimoId_ = 0;  // la consegna di una mossa della partita di prima non riguarda questa
    // (L'ultimo esito della battaglia di prima resta: chi ha sparato puo' ancora aspettarlo.)
    if (g == Gioco::TRIS) {
        s_.stato = Sfida::INVITATO;
        manda(avversario, msg(INVITO), true);
    } else {
        s_.stato = Sfida::PREPARA;
        rimescola();
    }
    return true;
}

void Giochi::rimescola() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    seme_ = seme_ * 2654435761u + pn::randomBelow(0x7FFFFFFF) + 1;
    flottaACaso(s_.mare, seme_);
}

void Giochi::pronto() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (s_.stato != Sfida::PREPARA) return;
    if (s_.invitante) {
        s_.stato = Sfida::INVITATO;
        manda(s_.avversario, msg(INVITO), true);
    } else {
        s_.stato = Sfida::IN_CORSO;
        s_.tocca = false;  // comincia chi ha invitato
        manda(s_.avversario, msg(ACCETTO), true);
    }
}

void Giochi::accetta() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (s_.stato != Sfida::INVITO_ARRIVATO) return;
    if (s_.gioco == Gioco::TRIS) {
        s_.stato = Sfida::IN_CORSO;
        s_.tocca = false;
        manda(s_.avversario, msg(ACCETTO), true);
    } else {
        s_.stato = Sfida::PREPARA;
        rimescola();
    }
}

void Giochi::rifiuta() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (s_.stato != Sfida::INVITO_ARRIVATO && !(s_.stato == Sfida::PREPARA && !s_.invitante)) return;
    rispondi(s_.avversario, msg(RIFIUTO, {0}));
    s_ = Sfida();
}

void Giochi::abbandona() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!attiva()) return;
    if (s_.stato == Sfida::INVITO_ARRIVATO || (s_.stato == Sfida::PREPARA && !s_.invitante)) {
        rifiuta();
        return;
    }
    // Battaglia ancora da cominciare (non ho invitato nessuno): niente da dire all'altro.
    if (!(s_.stato == Sfida::PREPARA && s_.invitante)) rispondi(s_.avversario, msg(ABBANDONO));
    // Il mio invito che aspettava ancora esce dai tentativi della radio: se no, arrivato piu' tardi,
    // prenderebbe il posto di una partita nuova.
    ritiraInviti();
    fine(false, "Hai abbandonato la partita", false);
}

void Giochi::ritiraInviti() {
    for (uint32_t id : mieiInviti_) annulla_.push_back({s_.avversario, id});
    mieiInviti_.clear();
    // Un invito non ancora partito non parte piu'.
    if (ultimo_.size() >= 2 && ultimo_[1] == INVITO)
        uscita_.erase(std::remove_if(uscita_.begin(), uscita_.end(), [&](const Uscita& u) { return u.tieni && u.m == ultimo_; }),
                      uscita_.end());
}

void Giochi::chiudi() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (s_.stato != Sfida::FINITA) return;
    ricordaChiusa();
    s_ = Sfida();
}

void Giochi::fine(bool vinto, const std::string& testo, bool risultato) {
    s_.stato = Sfida::FINITA;
    s_.vinto = vinto;
    s_.esito = testo;
    s_.tocca = false;
    risultato_ = risultato;
    if (risultato) return;
    // Abbandono o rifiuto: niente da rimandare (ne' la mia ultima mossa ne' l'esito di questa
    // partita), e la consegna della mia ultima mossa, se arriva dopo, si ignora. Quando invece la
    // chiude la mia mossa (tris vinto o pari) la mossa resta: se non arriva, si deve poter rimandare.
    ultimo_.clear();
    ultimoId_ = 0;
    s_.inViaggio = false;
    s_.nonArrivata = false;
    // Solo l'esito di questa partita: quello di una battaglia finita prima resta (l'altro puo'
    // ancora aspettarlo).
    if (diPartita(ultimoEsito_, s_.gioco, s_.id)) ultimoEsito_.clear();
    if (diPartita(esitoMsg_, s_.gioco, s_.id)) esitoMsg_.clear();
}

// La partita finita in s_ si toglie (chiusa, o sostituita da un'altra): la si ricorda, per
// riconoscerne le copie che arrivano dopo. Se la mia mossa che l'ha chiusa risulta non arrivata (e
// non l'ho rimandata), la si rimanda adesso, una volta: l'altro la aspetta ancora.
void Giochi::ricordaChiusa() {
    if (s_.stato != Sfida::FINITA) return;
    chiusa_.avversario = s_.avversario;
    chiusa_.id = s_.id;
    chiusa_.gioco = s_.gioco;
    chiusa_.mosse = s_.mosse;
    chiusa_.risultato = risultato_;
    if (s_.nonArrivata && !finaleRimandato_ && finaleDi(s_.avversario, s_.gioco, s_.id)) {
        finaleRimandato_ = true;
        uscita_.push_back({finaleTo_, finale_, false});
    }
}

// Risposta che chiude (ABBANDONO, RIFIUTO): se ne segue la consegna, e se non arriva si rimanda una
// volta (vedi consegna()). Si segue solo l'ultima.
void Giochi::rispondi(uint16_t to, const std::vector<uint8_t>& m) {
    uscita_.push_back({to, m, false});
    rispostaAuto_ = m;
    rispostaAutoTo_ = to;
    rispostaAutoId_ = 0;
    rispostaAutoRimandata_ = false;
}

bool Giochi::finaleDi(uint16_t to, Gioco g, uint16_t id) const { return finaleTo_ == to && diPartita(finale_, g, id); }

void Giochi::trisControlla() {
    static const uint8_t L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
    for (const auto& l : L) {
        const uint8_t v = s_.tris[l[0]];
        if (v && v == s_.tris[l[1]] && v == s_.tris[l[2]]) {
            fine(v == 1, v == 1 ? "Hai vinto!" : "Ha vinto " + dati().nameOf(s_.avversario), true);
            return;
        }
    }
    if (s_.mosse >= 9) fine(false, "Pareggio", true);
}

bool Giochi::gioca(int casella) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (s_.stato != Sfida::IN_CORSO || !s_.tocca || s_.inViaggio || s_.nonArrivata) return false;
    if (s_.gioco == Gioco::TRIS) {
        if (casella < 0 || casella > 8 || s_.tris[casella]) return false;
        s_.tris[casella] = 1;
    } else {
        if (casella < 0 || casella > 99 || s_.miei[casella]) return false;
        s_.ultimoMio = int8_t(casella);
    }
    const uint8_t n = ++s_.mosse;
    s_.tocca = false;
    manda(s_.avversario, msg(MOSSA, {n, uint8_t(casella)}), true);
    if (s_.gioco == Gioco::TRIS) {
        trisControlla();
        // La mia mossa ha chiuso la partita: la si segue anche dopo che la partita si chiude.
        if (s_.stato == Sfida::FINITA) {
            finale_ = ultimo_;
            finaleTo_ = s_.avversario;
            finaleId_ = 0;
            finaleRimandato_ = false;
        }
    }
    return true;
}

void Giochi::rimanda() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!s_.nonArrivata) return;
    // Niente da rimandare (la partita si e' chiusa intanto): OK deve poter chiudere.
    if (ultimo_.empty()) {
        s_.nonArrivata = false;
        return;
    }
    manda(s_.avversario, ultimo_, true);
}

void Giochi::consegna(uint16_t to, uint32_t msgId, pn::Delivery d) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!msgId) return;
    // Battaglia: l'esito di un suo colpo non e' arrivato. Lui ha la mossa confermata e non la
    // rimanda: senza esito aspetterebbe per sempre. Si rimanda una volta (anche a partita finita).
    if (msgId == esitoId_ && to == esitoTo_) {
        if (d == pn::Delivery::UNREACHABLE && !esitoRimandato_ && !esitoMsg_.empty()) {
            esitoRimandato_ = true;
            uscita_.push_back({to, esitoMsg_, false});
        }
        return;
    }
    // Un ABBANDONO o un RIFIUTO non arrivato: l'altro non manda piu' niente e aspetterebbe per sempre.
    // Si rimanda una volta, se intanto quella partita qui non e' tornata viva.
    if (msgId == rispostaAutoId_ && to == rispostaAutoTo_) {
        const bool viva = rispostaAuto_.size() >= 4 && to == s_.avversario && rispostaAuto_[0] == uint8_t(s_.gioco) &&
                          idDi(rispostaAuto_) == s_.id && attiva();
        if (d == pn::Delivery::UNREACHABLE && !rispostaAutoRimandata_ && !rispostaAuto_.empty() && !viva) {
            rispostaAutoRimandata_ = true;
            uscita_.push_back({to, rispostaAuto_, false});
        }
        return;
    }
    // Tris: la mia mossa che ha chiuso la partita, non arrivata, con la partita gia' chiusa qui (o
    // sostituita da un'altra): si rimanda da sola, una volta. Finche' e' aperta si rimanda a mano
    // (sotto: nonArrivata, OK sulla schermata).
    if (msgId == finaleId_ && to == finaleTo_ && (msgId != ultimoId_ || to != s_.avversario)) {
        if (d == pn::Delivery::UNREACHABLE && !finaleRimandato_ && !finale_.empty()) {
            finaleRimandato_ = true;
            uscita_.push_back({to, finale_, false});
        }
        return;
    }
    if (msgId != ultimoId_ || to != s_.avversario) return;
    if (d == pn::Delivery::DELIVERED || d == pn::Delivery::READ) {
        s_.inViaggio = false;
    } else if (d == pn::Delivery::UNREACHABLE) {
        s_.inViaggio = false;
        s_.nonArrivata = true;
    } else {
        return;  // RETRYING: si continua ad aspettare
    }
    evento(7);
}

void Giochi::ricevi(uint16_t from, const uint8_t* data, size_t len, uint32_t msgId) {
    if (len < 4) return;
    const Gioco g = Gioco(data[0]);
    if (g != Gioco::TRIS && g != Gioco::NAVALE) return;
    const uint8_t tipo = data[1];
    const uint16_t id = uint16_t(data[2] | (data[3] << 8));
    std::lock_guard<std::recursive_mutex> l(mu_);
    const bool stessa = s_.avversario == from && s_.id == id && s_.gioco == g;
    // L'ultima partita chiusa qui (con chiudi(), o finita e sostituita da un'altra).
    const bool vecchia = !stessa && chiusa_.avversario == from && chiusa_.id == id && chiusa_.gioco == g;
    // Il suo messaggio piu' recente di questa partita, tra quelli che manda solo con la partita viva
    // (invito, accetto, mosse): dice quali suoi inviti sono vecchi. Gli esiti no: uno rimandato da
    // solo puo' partire dopo un suo invito nuovo.
    if (stessa && (tipo == INVITO || tipo == ACCETTO || tipo == MOSSA) && msgId > daLui_) daLui_ = msgId;
    // Messaggio di una partita che qui non c'e' (riavvio, partita chiusa o sostituita) o che qui e'
    // finita per abbandono o rifiuto: la conferma radio e' gia' partita, quindi l'altro aspetterebbe per
    // sempre. Gli si risponde da soli che la partita e' finita. All'ABBANDONO non si risponde mai:
    // niente giri infiniti.
    auto nonCe = [&]() { rispondi(from, {uint8_t(g), ABBANDONO, uint8_t(id), uint8_t(id >> 8)}); };
    switch (tipo) {
        case INVITO:
            // Copia di un invito di questa partita (anche gia' finita) o dell'ultima chiusa.
            if (stessa || vecchia) return;
            // Occupato in un'altra partita: si risponde da soli.
            if (attiva() && s_.avversario != from) {
                rispondi(from, {uint8_t(g), RIFIUTO, uint8_t(id), uint8_t(id >> 8), 1});
                return;
            }
            if (attiva()) {  // il mio avversario, con un'altra partita
                if (s_.stato == Sfida::INVITATO) {
                    // Inviti incrociati (ci siamo sfidati quasi insieme): tutti e due tengono quello con
                    // l'id piu' basso. Se e' il mio glielo si rimanda (se lo aveva gia', per lui e' una
                    // copia; se si era riavviato, cosi' lo riceve), se no si prende il suo.
                    const bool tengoIlMio = s_.id < id || (s_.id == id && uint8_t(s_.gioco) < uint8_t(g));
                    if (tengoIlMio) {
                        manda(from, msg(INVITO), true);
                        return;
                    }
                } else {
                    // Un suo invito vecchio, rimasto nei tentativi della radio: la partita di adesso e'
                    // piu' recente e resta.
                    if (msgId && msgId < daLui_) return;
                    // Un invito nuovo: per lui la partita di adesso non c'e' piu' (si sfida solo senza
                    // partite in corso). Glielo si dice: se per lui era ancora viva, finisce invece di
                    // aspettare. (Una battaglia che preparavo per sfidarlo lui non la conosce.)
                    if (!(s_.stato == Sfida::PREPARA && s_.invitante)) rispondi(from, msg(ABBANDONO));
                }
            }
            ricordaChiusa();  // se era finita
            ritiraInviti();   // il mio invito sostituito (inviti incrociati) non serve piu'
            s_ = Sfida();
            risultato_ = false;
            daLui_ = msgId;
            s_.stato = Sfida::INVITO_ARRIVATO;
            s_.gioco = g;
            s_.avversario = from;
            s_.id = id;
            ultimo_.clear();
            ultimoId_ = 0;  // la consegna di una mossa della partita di prima non riguarda questa
            // (L'ultimo esito della battaglia di prima resta: chi ha sparato puo' ancora aspettarlo.)
            evento(INVITO);
            return;
        case ACCETTO:
            // Chi accetta aspetta la prima mossa di chi ha invitato.
            if (!stessa || !attiva()) {
                nonCe();
                return;
            }
            if (s_.stato != Sfida::INVITATO) return;
            // L'accetto dice che il mio invito e' arrivato (anche se la conferma no): i suoi tentativi
            // non servono piu'.
            ritiraInviti();
            s_.stato = Sfida::IN_CORSO;
            s_.tocca = true;
            s_.inViaggio = false;
            s_.nonArrivata = false;
            ultimoId_ = 0;
            evento(ACCETTO);
            return;
        case RIFIUTO:
            if (!stessa || (s_.stato != Sfida::INVITATO && s_.stato != Sfida::IN_CORSO)) return;
            ritiraInviti();
            fine(false, dati().nameOf(from) + (len > 4 && data[4] == 1 ? " sta già giocando con un altro" : " non vuole giocare"),
                 false);
            evento(RIFIUTO);
            return;
        case ABBANDONO:
            if (!stessa || !attiva()) return;
            ritiraInviti();
            fine(true, dati().nameOf(from) + " ha abbandonato la partita", false);
            evento(ABBANDONO);
            return;
        case MOSSA: {
            if (len < 6) return;
            const uint8_t n = data[4], c = data[5];
            // Battaglia: colpo gia' visto (l'esito era andato perso): si rimanda l'esito, anche a
            // partita chiusa o dopo una sfida nuova (finche' non ho mandato un altro esito).
            if ((stessa || vecchia || from == ultimoEsitoA_) && g == Gioco::NAVALE && n == ultimoEsitoN_ &&
                diPartita(ultimoEsito_, g, id)) {
                uscita_.push_back({from, ultimoEsito_, false});
                return;
            }
            // Tris: copia di una sua mossa di una partita che ho chiuso io con la mia mossa (anche se
            // chiusa_ e' gia' un'altra partita): gli manca la mia mossa finale, non un ABBANDONO, che
            // darebbe la vittoria a chi ha perso.
            if (g == Gioco::TRIS && !(stessa && attiva()) && finaleDi(from, g, id) && finale_.size() >= 6 &&
                n <= finale_[4]) {
                uscita_.push_back({from, finale_, false});
                return;
            }
            // Copia di una mossa gia' vista di un'altra partita finita con un risultato (qui ancora
            // aperta o l'ultima chiusa): all'altro manca la mia risposta, non un ABBANDONO.
            const bool finita = stessa ? s_.stato == Sfida::FINITA : vecchia;
            const bool conRisultato = stessa ? risultato_ : chiusa_.risultato;
            const uint8_t mosse = stessa ? s_.mosse : chiusa_.mosse;
            if (finita && conRisultato && n <= mosse) return;
            // L'altro aspetta la mia risposta, ma qui la partita non c'e' (piu'), e' finita per
            // abbandono o rifiuto, o la mossa e' nuova dopo la fine: ABBANDONO.
            if (!stessa || !attiva()) {
                nonCe();
                return;
            }
            if (s_.stato != Sfida::IN_CORSO || s_.tocca || n != uint8_t(s_.mosse + 1)) return;
            // Se l'altro muove, la mia ultima mossa gli e' arrivata (anche se la conferma no).
            s_.inViaggio = false;
            s_.nonArrivata = false;
            ultimoId_ = 0;
            if (g == Gioco::TRIS) {
                if (c > 8 || s_.tris[c]) return;
                s_.tris[c] = 2;
                s_.mosse = n;
                s_.tocca = true;
                trisControlla();
                evento(MOSSA);
                return;
            }
            if (c > 99) return;
            s_.mosse = n;
            s_.ultimoSuo = int8_t(c);
            const uint8_t nave = s_.mare[c];
            uint8_t ris = ESITO_ACQUA;
            std::vector<uint8_t> affondata;
            if (nave) {
                s_.suoi[c] = 2;
                ris = ESITO_COLPITO;
                bool tutta = true, flotta = true;
                for (int i = 0; i < 100; i++) {
                    if (s_.mare[i] == nave) {
                        affondata.push_back(uint8_t(i));
                        if (s_.suoi[i] != 2) tutta = false;
                    }
                    if (s_.mare[i] && s_.suoi[i] != 2) flotta = false;
                }
                if (tutta) ris = flotta ? ESITO_FINE : ESITO_AFFONDATO;
                if (!tutta) affondata.clear();
            } else if (!s_.suoi[c]) {
                s_.suoi[c] = 1;
            }
            std::vector<uint8_t> e = msg(ESITO, {n, c, ris, uint8_t(affondata.size())});
            e.insert(e.end(), affondata.begin(), affondata.end());
            ultimoEsitoN_ = n;
            ultimoEsitoA_ = from;
            ultimoEsito_ = e;
            esitoRimandato_ = false;
            uscita_.push_back({from, e, false});
            if (ris == ESITO_FINE) fine(false, "Ha vinto " + dati().nameOf(from) + ": ha affondato tutta la flotta", true);
            else s_.tocca = true;
            evento(MOSSA);
            return;
        }
        case ESITO: {
            if (g != Gioco::NAVALE || len < 8) return;
            // Esito dell'ultima partita chiusa, o di una finita qui: si ignora (dopo un esito tocca a
            // lui; se l'avevo abbandonata lo sapra' alla sua mossa). Di una partita che qui non c'e':
            // ABBANDONO.
            if (vecchia) return;
            if (!stessa) {
                nonCe();
                return;
            }
            if (s_.stato != Sfida::IN_CORSO) return;
            const uint8_t n = data[4], c = data[5], ris = data[6], k = data[7];
            // Esito di un mio colpo, anche di uno vecchio: se il primo invio si perde, il colpo dopo
            // dell'altro puo' arrivare prima. I colpi si alternano: chi invita tira i dispari.
            const bool mio = ((n & 1) != 0) == s_.invitante;
            if (!mio || n == 0 || n > s_.mosse || c > 99) return;
            const bool ultimo = n == s_.mosse;
            if (ultimo) {  // il mio ultimo colpo e' arrivato (anche se la conferma no)
                s_.inViaggio = false;
                s_.nonArrivata = false;
                ultimoId_ = 0;
            }
            if (s_.miei[c]) {  // esito gia' visto
                if (ultimo) evento(ESITO);
                return;
            }
            s_.miei[c] = ris == ESITO_ACQUA ? 1 : 2;
            if (ris >= ESITO_AFFONDATO)
                for (size_t i = 0; i < k && 8 + i < len; i++)
                    if (data[8 + i] < 100) s_.miei[data[8 + i]] = 3;
            if (ris == ESITO_FINE) fine(true, "Hai vinto! Flotta di " + dati().nameOf(from) + " affondata", true);
            evento(ESITO);
            return;
        }
        default:
            return;
    }
}

}  // namespace app
