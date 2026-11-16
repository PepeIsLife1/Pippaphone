#include "app/orologio.h"

#include <algorithm>
#include <ctime>

#include "app/memoria.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "pippanet/packet.h"
#include "scheda/scheda.h"

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR  // simulatore: niente memoria RTC
#endif

namespace app {

namespace {
// File delle sveglie: 0xA1, versione, numero di sveglie, poi per ognuna ora, minuti, giorni,
// attiva e il nome (lunghezza + byte).
constexpr uint8_t MAGIC = 0xA1, VERSIONE = 1;
// Una sveglia saltata di piu' di cosi' (orologio spostato in avanti) non suona piu'.
constexpr uint32_t RITARDO_MAX_S = 120;
// ... tranne quella per cui il T-Deck si e' risvegliato, che suona fino a questo ritardo.
constexpr uint32_t RITARDO_RISVEGLIO_S = 30 * 60;

// Copia del posticipo nella memoria RTC: resta nel sonno profondo ("Spegni" sul T-Deck, che si
// risveglia per suonare) e torna -1 a ogni altro avvio.
RTC_DATA_ATTR int g_snoozeIndex = -1;
RTC_DATA_ATTR uint32_t g_snoozeAt = 0;
// Ora della sveglia (o del posticipo) per cui il T-Deck si risveglia (0 = nessuna), anche lei nella
// memoria RTC. Vale solo dopo un risveglio per la sveglia; si azzera quando ha suonato.
RTC_DATA_ATTR uint32_t g_wakeFor = 0;

bool uguali(const Sveglia& a, const Sveglia& b) {
    return a.ora == b.ora && a.minuti == b.minuti && a.giorni == b.giorni && a.attiva == b.attiva && a.nome == b.nome;
}
}  // namespace

Orologio& orologio() {
    static Orologio o;
    return o;
}

int64_t Orologio::nowMs() { return esp_timer_get_time() / 1000; }

// ---------------------------------------------------------------- sveglie

void Orologio::load() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    sveglie_.clear();
    // Uscito dal sonno profondo di "Spegni" (per suonare, o col trackball del T-Deck): il posticipo
    // di prima vale ancora. A ogni altro avvio si parte senza.
    if (scheda::wokeFromSleep()) {
        snoozeIndex_ = g_snoozeIndex;
        snoozeAt_ = g_snoozeAt;
    } else {
        annullaPosticipo();
    }
    std::vector<uint8_t> b;
    if (!readFile(DATI_PATH("/sveglie.bin"), b) || b.size() < 3 || b[0] != MAGIC) {
        annullaPosticipo();  // nessuna sveglia da far suonare di nuovo
        return;
    }
    pn::Reader r(b.data(), b.size());
    r.u8();
    r.u8();  // versione: per ora c'e' solo la 1
    const uint8_t n = r.u8();
    for (uint8_t i = 0; i < n && i < MAX_SVEGLIE && r.ok(); i++) {
        Sveglia s;
        s.ora = uint8_t(std::min<int>(23, r.u8()));
        s.minuti = uint8_t(std::min<int>(59, r.u8()));
        s.giorni = uint8_t(r.u8() & 0x7F);
        s.attiva = r.u8() != 0;
        const uint8_t len = r.u8();
        if (len && r.remaining() >= len) s.nome.assign(reinterpret_cast<const char*>(r.ptr()), len);
        r.skip(len);
        if (r.ok()) sveglie_.push_back(s);
    }
    if (snoozeIndex_ >= int(sveglie_.size())) annullaPosticipo();
}

void Orologio::save() {
    std::vector<uint8_t> b(3 + MAX_SVEGLIE * (5 + 255));
    pn::Writer w(b.data(), b.size());
    w.u8(MAGIC);
    w.u8(VERSIONE);
    w.u8(uint8_t(sveglie_.size()));
    for (const auto& s : sveglie_) {
        w.u8(s.ora);
        w.u8(s.minuti);
        w.u8(s.giorni);
        w.u8(s.attiva ? 1 : 0);
        const size_t len = std::min<size_t>(s.nome.size(), 255);
        w.u8(uint8_t(len));
        w.put(s.nome.data(), len);
    }
    b.resize(w.len());
    writeFile(DATI_PATH("/sveglie.bin"), b);
}

std::vector<Sveglia> Orologio::sveglie() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return sveglie_;
}

void Orologio::setSveglie(const std::vector<Sveglia>& s) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    const bool posticipo = snoozeIndex_ >= 0 && snoozeIndex_ < int(sveglie_.size());
    const Sveglia vecchia = posticipo ? sveglie_[size_t(snoozeIndex_)] : Sveglia();
    sveglie_ = s;
    if (sveglie_.size() > MAX_SVEGLIE) sveglie_.resize(MAX_SVEGLIE);
    // Una sveglia cambiata, spenta, accesa o tolta non resta posticipata. Se e' rimasta uguale la
    // si ritrova anche spostata (tolta una sveglia prima di lei).
    int nuovo = -1;
    if (posticipo) {
        if (snoozeIndex_ < int(sveglie_.size()) && uguali(sveglie_[size_t(snoozeIndex_)], vecchia)) nuovo = snoozeIndex_;
        for (int i = 0; nuovo < 0 && i < int(sveglie_.size()); i++)
            if (uguali(sveglie_[size_t(i)], vecchia)) nuovo = i;
    }
    if (nuovo < 0) {
        annullaPosticipo();
    } else {
        snoozeIndex_ = nuovo;
        g_snoozeIndex = nuovo;
    }
    save();
}

uint32_t Orologio::prossima(const Sveglia& s, uint32_t after) {
    if (!s.attiva || !after) return 0;
    const time_t t0 = time_t(after);
    std::tm base{};
    localtime_r(&t0, &base);
    for (int d = 0; d <= 7; d++) {
        std::tm tm = base;
        tm.tm_mday += d;
        tm.tm_hour = s.ora;
        tm.tm_min = s.minuti;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;  // mktime sceglie ora solare o legale per quel giorno
        const time_t t = mktime(&tm);
        if (t <= t0) continue;
        if (s.giorni) {
            const int lun0 = (tm.tm_wday + 6) % 7;  // 0 = lunedi'
            if (!(s.giorni & (1 << lun0))) continue;
        }
        return uint32_t(t);
    }
    return 0;
}

bool Orologio::prossimaDi(uint32_t now, uint32_t& quando, int& indice) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    quando = 0;
    indice = -1;
    if (!now) return false;
    // Ora appena corretta indietro di poco (vedi scaduta): le sveglie fino a lastCheck_ hanno gia'
    // suonato ("Spegni" subito dopo non deve risvegliare il telefono per la stessa).
    const uint32_t da = lastCheck_ > now && lastCheck_ - now <= RITARDO_RISVEGLIO_S ? lastCheck_ : now;
    for (int i = 0; i < int(sveglie_.size()); i++) {
        const uint32_t t = prossima(sveglie_[i], da);
        if (t && (!quando || t < quando)) {
            quando = t;
            indice = i;
        }
    }
    if (snoozeIndex_ >= 0 && snoozeAt_ > now && (!quando || snoozeAt_ < quando)) {
        quando = snoozeAt_;
        indice = snoozeIndex_;
    }
    return quando != 0;
}

int Orologio::scaduta(uint32_t now) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!now) return -1;
    // Risveglio per la sveglia di g_wakeFor (solo T-Deck): vale fino a quando suona, o per
    // RITARDO_RISVEGLIO_S. In ogni altro avvio non c'e'.
    if (g_wakeFor && (!scheda::wokeForAlarm() || now > g_wakeFor + RITARDO_RISVEGLIO_S)) g_wakeFor = 0;
    const uint32_t risveglio = g_wakeFor;
    // Primo controllo dopo il risveglio: si parte da appena prima di quella sveglia, che deve suonare
    // anche se il risveglio arriva dopo di lei.
    if (!lastCheck_ && risveglio) lastCheck_ = std::min(now, risveglio - 1);
    // Ora corretta indietro di poco (GPS o rete dopo il risveglio: nel sonno l'orologio del chip
    // correva): le sveglie fino a lastCheck_ sono gia' controllate (e magari suonate), si riprende da
    // li' quando l'ora ci ritorna. Il posticipo invece si controlla subito. Prima suonava due volte.
    const bool indietro = lastCheck_ && now < lastCheck_ && lastCheck_ - now <= RITARDO_RISVEGLIO_S;
    // Primo controllo, o orologio tornato indietro di molto: si riparte da adesso.
    if (!indietro && (!lastCheck_ || now < lastCheck_ || now - lastCheck_ > 6 * 3600)) {
        lastCheck_ = now;
        return -1;
    }
    const uint32_t from = lastCheck_;  // con 'indietro' e' dopo now: nessuna sveglia scade
    if (!indietro) lastCheck_ = now;
    // In ritardo di piu' di RITARDO_MAX_S (salto dell'orologio): non suona, tranne la sveglia del
    // risveglio, che suona finche' il ritardo non supera RITARDO_RISVEGLIO_S.
    auto inTempo = [&](uint32_t t) { return now - t <= (risveglio && t == risveglio ? RITARDO_RISVEGLIO_S : RITARDO_MAX_S); };
    // Tutte le sveglie scadute da 'from' a 'now' suonano insieme, con la schermata della prima: due
    // alla stessa ora non se ne perde una. Le "una volta sola" si spengono, anche quelle saltate (se
    // no suonerebbero domani).
    int suona = -1;
    bool spente = false, suonaRisveglio = false;
    for (int i = 0; i < int(sveglie_.size()); i++) {
        const uint32_t t = prossima(sveglie_[i], from);
        if (!t || t > now) continue;
        if (!sveglie_[i].giorni) {
            sveglie_[i].attiva = false;  // una volta sola
            spente = true;
        }
        if (!inTempo(t)) continue;  // saltata da un salto dell'orologio
        if (risveglio && t == risveglio) suonaRisveglio = true;
        if (suona < 0) suona = i;
    }
    if (spente) save();
    // Il posticipo scaduto suona anche lui (insieme alla sveglia nuova, se ce n'e' una adesso).
    if (snoozeIndex_ >= 0 && snoozeAt_ <= now) {
        const int i = snoozeIndex_;
        const bool ok = inTempo(snoozeAt_) && i < int(sveglie_.size());
        if (ok && risveglio && snoozeAt_ == risveglio) suonaRisveglio = true;
        annullaPosticipo();
        if (suona < 0 && ok) suona = i;
    }
    if (suonaRisveglio) g_wakeFor = 0;
    return suona;
}

void Orologio::posticipa(int indice, uint32_t now) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    snoozeIndex_ = indice;
    snoozeAt_ = now + POSTICIPA_S;
    g_snoozeIndex = snoozeIndex_;
    g_snoozeAt = snoozeAt_;
}

void Orologio::annullaPosticipo() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    snoozeIndex_ = -1;
    snoozeAt_ = 0;
    g_snoozeIndex = -1;
    g_snoozeAt = 0;
}

bool Orologio::posticipata(uint32_t& quando, int& indice) const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    quando = snoozeAt_;
    indice = snoozeIndex_;
    return snoozeIndex_ >= 0;
}

void Orologio::risveglioPer(uint32_t quando) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    g_wakeFor = quando;
}

// ---------------------------------------------------------------- timer

void Orologio::timerAvvia(uint32_t durataMs) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!durataMs) return;
    timerDurata_ = durataMs;
    timerFine_ = nowMs() + durataMs;
    timer_ = Timer::VA;
}

void Orologio::timerPausa() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (timer_ != Timer::VA) return;
    timerResta_ = timerRestaMs();
    timer_ = Timer::PAUSA;
}

void Orologio::timerRiprendi() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (timer_ != Timer::PAUSA) return;
    timerFine_ = nowMs() + timerResta_;
    timer_ = Timer::VA;
}

void Orologio::timerAzzera() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    timer_ = Timer::FERMO;
    timerResta_ = 0;
}

uint32_t Orologio::timerRestaMs() const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (timer_ == Timer::PAUSA) return timerResta_;
    if (timer_ != Timer::VA) return timerDurata_;
    const int64_t left = timerFine_ - nowMs();
    return left > 0 ? uint32_t(left) : 0;
}

bool Orologio::timerScaduto() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (timer_ != Timer::VA || nowMs() < timerFine_) return false;
    timer_ = Timer::FERMO;
    return true;
}

// ---------------------------------------------------------------- cronometro

void Orologio::cronoAvvia() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (cronoVa_) return;
    cronoInizio_ = nowMs();
    cronoVa_ = true;
}

void Orologio::cronoFerma() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!cronoVa_) return;
    cronoAccumulato_ = cronoMs();
    cronoVa_ = false;
}

void Orologio::cronoAzzera() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    cronoVa_ = false;
    cronoAccumulato_ = 0;
    giri_.clear();
    giriTotali_ = 0;
}

void Orologio::cronoGiro() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (!cronoVa_) return;
    if (giri_.size() >= MAX_GIRI) giri_.erase(giri_.begin());
    giri_.push_back(cronoMs());
    giriTotali_++;
}

uint32_t Orologio::cronoMs() const {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return cronoAccumulato_ + (cronoVa_ ? uint32_t(nowMs() - cronoInizio_) : 0);
}

// ---------------------------------------------------------------- testi

std::string giorniText(uint8_t g) {
    g &= 0x7F;
    if (!g) return "una volta";
    if (g == 0x7F) return "tutti i giorni";
    if (g == 0x1F) return "lun-ven";
    if (g == 0x60) return "sab e dom";
    static const char* nomi[7] = {"lun", "mar", "mer", "gio", "ven", "sab", "dom"};
    std::string s;
    for (int i = 0; i < 7; i++)
        if (g & (1 << i)) s += (s.empty() ? "" : " ") + std::string(nomi[i]);
    return s;
}

}  // namespace app
