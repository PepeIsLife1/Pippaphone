// T-Deck Plus: analizzatore NMEA, frame UBX e pilota del ricevitore GPS (vedi gnss_pilota.h).
// Niente ESP-IDF qui dentro: lo stesso file si prova sul PC (componenti/scheda/test_host).
#include "gnss_pilota.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace scheda {

namespace {
// Le quattro funzioni di aiuto sono le stesse di gnss.cpp (che sulla PCB resta com'e').
int hexVal(char c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; }

// Divide la frase in campi separati da virgole (modifica la stringa).
int split(char* s, char** f, int max) {
    int n = 0;
    f[n++] = s;
    for (char* p = s; *p && n < max; p++)
        if (*p == ',') {
            *p = 0;
            f[n++] = p + 1;
        }
    return n;
}

double coord(const char* v, const char* hemi) {
    if (!*v) return 0;
    double x = std::atof(v);
    int deg = int(x / 100);
    double d = deg + (x - deg * 100) / 60.0;
    if (*hemi == 'S' || *hemi == 'W') d = -d;
    return d;
}

// Giorni dal 1970 (algoritmo di Howard Hinnant, senza dipendere dal fuso orario).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

// Testo di al massimo max caratteri da un campo UBX (CH[n], finisce col primo 0): i caratteri non
// stampabili diventano '?'.
void copiaTesto(char* dst, size_t cap, const uint8_t* src, size_t max) {
    size_t i = 0;
    for (; i < max && i + 1 < cap && src[i]; i++) dst[i] = (src[i] >= 32 && src[i] < 127) ? char(src[i]) : '?';
    dst[i] = 0;
}

int32_t ms32(uint64_t ms) { return int32_t(std::min<uint64_t>(ms, INT32_MAX)); }

// ms da t a now; 0 se t e' dopo now (un istante preso piu' tardi, in un altro punto del giro).
uint64_t dopo(uint64_t now, uint64_t t) { return now > t ? now - t : 0; }
}  // namespace

// ---------------------------------------------------------------- analizzatore NMEA (T-Deck)

bool NmeaParser::feed(char c, Fix& fix, uint64_t nowMs) {
    if (c == '$') {
        if (len_ > 1) fix.frasiKo++;  // la frase di prima e' stata interrotta
        len_ = 0;
        line_[len_++] = c;
        return false;
    }
    if (len_ == 0) return false;
    if (c == '\r' || c == '\n') {
        line_[len_] = 0;
        bool ok = parse(fix, nowMs);
        len_ = 0;
        return ok;
    }
    if (len_ < int(sizeof(line_)) - 1) {
        line_[len_++] = c;
    } else {
        len_ = 0;  // troppo lunga (NMEA: al massimo 82 caratteri)
        fix.frasiKo++;
    }
    return false;
}

bool NmeaParser::parse(Fix& fix, uint64_t nowMs) {
    // Controllo del checksum.
    char* star = std::strchr(line_, '*');
    if (!star || star[1] == 0 || star[2] == 0) {
        fix.frasiKo++;
        return false;
    }
    uint8_t sum = 0;
    for (char* p = line_ + 1; p < star; p++) sum ^= uint8_t(*p);
    const int hi = hexVal(star[1]), lo = hexVal(star[2]);
    if (hi < 0 || lo < 0 || sum != uint8_t(hi * 16 + lo)) {
        fix.frasiKo++;
        return false;
    }
    *star = 0;
    fix.frasiAtMs = nowMs;  // il ricevitore parla (anche solo GSA, TXT...)
    fix.frasiOk++;
    // 24 campi: la GSV con 4 satelliti e il signalId (NMEA 4.10+) ne ha 21.
    char* f[24];
    const int n = split(line_ + 1, f, 24);
    if (n < 1 || std::strlen(f[0]) < 5) return false;
    const char* type = f[0] + 2;  // GPRMC, GNRMC, BDRMC...
    if (std::strcmp(type, "RMC") == 0 && n >= 10) {
        // hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,v,c,ddmmyy
        fix.rmcAtMs = nowMs;
        if (std::strlen(f[1]) >= 6 && std::strlen(f[9]) == 6) {
            auto two = [](const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
            const int hh = two(f[1]), mi = two(f[1] + 2), ss = two(f[1] + 4);
            const int dd = two(f[9]), mo = two(f[9] + 2), yy = two(f[9] + 4);
            // Il ricevitore appena acceso puo' dare date del 1980 o 2080 ("..80"): un anno cosi' preso per
            // buono passerebbe anche ai ripetitori, come ora del GPS. Si accettano solo 2024-2079.
            if (yy >= 24 && yy < 80 && mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && hh < 24 && mi < 60 && ss < 61) {
                fix.unixTime = uint32_t(daysFromCivil(2000 + yy, unsigned(mo), unsigned(dd)) * 86400 + hh * 3600 +
                                        mi * 60 + ss);
                fix.timeValid = true;
                fix.timeAtMs = nowMs;
            }
        }
        // 'A' senza coordinate (ricevitore strano) non e' una posizione: sarebbe 0,0.
        if (f[2][0] == 'A' && f[3][0] && f[5][0]) {
            fix.lat = coord(f[3], f[4]);
            fix.lon = coord(f[5], f[6]);
            fix.valid = true;
            fix.atMs = nowMs;
        } else {
            fix.valid = false;
        }
        return true;
    }
    if (std::strcmp(type, "GSV") == 0 && n >= 4) {
        fix.gsvAtMs = nowMs;
        gsv(f, n, fix, nowMs);
        return true;
    }
    if (std::strcmp(type, "GGA") == 0 && n >= 10) {
        // hhmmss,lat,N,lon,E,qualita',satelliti,hdop,altitudine,M
        fix.ggaAtMs = nowMs;
        const int q = std::atoi(f[6]);
        fix.sats = uint8_t(std::atoi(f[7]));
        fix.hdop = f[8][0] ? float(std::atof(f[8])) : 99.0f;
        if (q > 0 && f[9][0]) fix.altM = float(std::atof(f[9]));
        return true;
    }
    return false;
}

// Satelliti dalle GSV: $xxGSV,numMsg,msgNum,numSV{,svid,elv,az,cno}[,signalId]. Ogni costellazione (e,
// da NMEA 4.10, ogni segnale) ha la sua serie di 1-9 messaggi da 4 satelliti; cno vuoto = non lo sente.
// La serie si raccoglie dal messaggio 1 e vale solo se arriva intera, fino all'ultimo.
void NmeaParser::gsv(char** f, int n, Fix& fix, uint64_t nowMs) {
    const int numMsg = std::atoi(f[1]), msgNum = std::atoi(f[2]);
    if (numMsg < 1 || numMsg > 9 || msgNum < 1 || msgNum > numMsg) return;
    int campi = n - 4;  // dopo numSV: 4 per satellite, piu' il signalId da NMEA 4.10
    char segnale = 0;
    if (campi % 4 == 1) {
        segnale = f[n - 1][0] ? f[n - 1][0] : '?';
        campi--;
    }
    Serie* s = nullptr;
    Serie* libera = nullptr;
    Serie* vecchia = &serie_[0];
    for (Serie& x : serie_) {
        if (x.talker[0] == f[0][0] && x.talker[1] == f[0][1] && x.segnale == segnale) {
            s = &x;
            break;
        }
        if (!libera && !x.talker[0]) libera = &x;
        if (!x.letta || (vecchia->letta && x.atMs < vecchia->atMs)) vecchia = &x;
    }
    if (!s) {
        s = libera ? libera : vecchia;
        *s = Serie();
        s->talker[0] = f[0][0];
        s->talker[1] = f[0][1];
        s->segnale = segnale;
    }
    if (msgNum == 1) {
        s->numMsg = uint8_t(numMsg);
        s->prossimo = 1;
        s->elencatiP = s->conSegnaleP = s->migliorP = 0;
    }
    if (s->prossimo != msgNum || s->numMsg != numMsg) {
        s->prossimo = 0;  // manca un messaggio o non torna: questa serie non vale, resta la precedente
        return;
    }
    for (int i = 4; i < 4 + campi; i += 4) {
        const char* cno = i + 3 < 4 + campi ? f[i + 3] : "";
        if (!f[i][0] && !cno[0]) continue;  // posto vuoto
        s->elencatiP++;                     // anche senza numero (GLONASS "R?") se ha il segnale
        const int c = std::atoi(cno);
        if (c > 0) {
            s->conSegnaleP++;
            s->migliorP = uint8_t(std::max<int>(s->migliorP, std::min(99, c)));
        }
    }
    if (msgNum < numMsg) {
        s->prossimo++;
        return;
    }
    s->elencati = s->elencatiP;
    s->conSegnale = s->conSegnaleP;
    s->miglior = s->migliorP;
    s->letta = true;
    s->atMs = nowMs;
    s->prossimo = 0;
    cielo(fix, nowMs);
}

// Totali dalle serie fresche: per costellazione il massimo tra i suoi segnali (lo stesso satellite su
// due segnali non conta due volte; esatto sui ricevitori a una banda come M10, L76K e AT6558R), poi la
// somma delle costellazioni. Una costellazione che non manda piu' GSV da 5 s non conta piu'.
void NmeaParser::cielo(Fix& fix, uint64_t nowMs) const {
    auto fresca = [&](const Serie& x) { return x.letta && dopo(nowMs, x.atMs) <= CIELO_MS; };
    int elencati = 0, conSegnale = 0, miglior = 0;
    for (int i = 0; i < SERIE; i++) {
        const Serie& a = serie_[i];
        if (!fresca(a)) continue;
        bool giaContata = false;
        for (int j = 0; j < i && !giaContata; j++)
            giaContata = fresca(serie_[j]) && serie_[j].talker[0] == a.talker[0] && serie_[j].talker[1] == a.talker[1];
        if (giaContata) continue;
        int e = 0, c = 0;
        for (int j = i; j < SERIE; j++) {
            const Serie& b = serie_[j];
            if (!fresca(b) || b.talker[0] != a.talker[0] || b.talker[1] != a.talker[1]) continue;
            e = std::max<int>(e, b.elencati);
            c = std::max<int>(c, b.conSegnale);
            miglior = std::max<int>(miglior, b.miglior);
        }
        elencati += e;
        conSegnale += c;
    }
    fix.inVista = uint8_t(std::min(255, elencati));
    fix.conSegnale = uint8_t(std::min(255, conSegnale));
    fix.snrMax = uint8_t(miglior);
}

void NmeaParser::scadenze(Fix& fix, uint64_t nowMs) {
    if (fix.valid && dopo(nowMs, fix.atMs) > POSIZIONE_MS) fix.valid = false;
    if (dopo(nowMs, fix.frasiAtMs) > POSIZIONE_MS) {  // nessuna frase: non c'e' niente di vero da mostrare
        fix.sats = 0;
        fix.hdop = 99;
    }
    cielo(fix, nowMs);
}

void NmeaParser::azzera() {
    len_ = 0;
    for (Serie& s : serie_) s = Serie();
}

// ---------------------------------------------------------------- testi (Gnss, T-Deck)

const char* Gnss::nomeStato(Stato s) {
    switch (s) {
        case Stato::RILEVA:
            return "rileva";
        case Stato::MUTO:
            return "muto";
        case Stato::ACCESO:
            return "acceso";
        case Stato::SVEGLIA:
            return "sveglia";
        case Stato::ADDORMENTA:
            return "addormenta";
        case Stato::STANDBY:
            return "standby";
        case Stato::PROVA:
            return "prova";
    }
    return "?";
}

void Gnss::testoEvento(const Evento& e, char* out, size_t n) {
    if (!n) return;
    const long a = long(e.a), b = long(e.b), c = long(e.c);
    switch (e.tipo) {
        case Ev::VOLUTO:
            std::snprintf(out, n, "voluto %s%s%s%s", a ? "acceso" : "spento", e.perche ? " (" : "",
                          e.perche ? e.perche : "", e.perche ? ")" : "");
            break;
        case Ev::RILEVATO:
            std::snprintf(out, n, "ricevitore %s a %ld baud (trovato in %ld ms)", a == 1 ? "u-blox" : "L76K", b, c);
            break;
        case Ev::MUTO:
            std::snprintf(out, n, "GPS muto: nessuna frase NMEA a 38400 o 9600 baud in %ld ms (riprovo tra 30 s)", c);
            break;
        case Ev::SILENZIO:
            std::snprintf(out, n, "voluto acceso ma muto da %ld ms: lo risveglio", a);
            break;
        case Ev::SVEGLIO:
            std::snprintf(out, n, "sveglio in %ld ms (%ld %s)", a, b, b == 1 ? "tentativo" : "tentativi");
            break;
        case Ev::SVEGLIA_FALLITA:
            std::snprintf(out, n, "non si sveglia dopo %ld tentativi: lo cerco di nuovo", a);
            break;
        case Ev::PMREQ_OK:
            if (c) std::snprintf(out, n, "standby: frasi ferme %ld ms dopo il PMREQ (flag 0x%02lx)", a, b);
            else std::snprintf(out, n, "standby: taceva gia' quando e' partito il PMREQ (flag 0x%02lx)", b);
            break;
        case Ev::PMREQ_IGNORATO:
            std::snprintf(out, n, "PMREQ ignorato (flag 0x%02lx): frasi ancora dopo 3 s, resta sempre acceso", a);
            break;
        case Ev::PMREQ_RIPROVA:
            std::snprintf(out, n, "il PMREQ e' partito mentre si svegliava: lo rimando");
            break;
        case Ev::DA_SOLO:
            std::snprintf(out, n, "in standby sono tornate le frasi: si e' svegliato da solo");
            break;
        case Ev::RINUNCIA:
            std::snprintf(out, n, "si risveglia da solo: resta acceso fino alla prossima accensione");
            break;
        case Ev::PRIMA_ORA:
            std::snprintf(out, n, "prima ora %ld.%ld s dopo la sveglia", a / 1000, (a % 1000) / 100);
            break;
        case Ev::PRIMO_FIX:
            std::snprintf(out, n, "prima posizione %ld.%ld s dopo la sveglia (%ld usati, %ld con segnale)", a / 1000,
                          (a % 1000) / 100, b, c);
            break;
        case Ev::RICERCA:
            std::snprintf(out, n, "cerco: ne sento %ld su %ld in vista, il migliore a %ld dB-Hz", a, c, b);
            break;
        case Ev::FREDDO:
            std::snprintf(out, n, "GPS fatto ripartire da freddo (prova)");
            break;
        case Ev::FREDDO_SCADUTO:
            std::snprintf(out, n, "ripartenza da freddo non fatta: non si e' svegliato entro 60 s");
            break;
        case Ev::PROVA_INIZIO:
            std::snprintf(out, n, "prova al banco: %ld cicli, PMREQ flag 0x%02lx", a, b);
            break;
        case Ev::PROVA_CICLO:
            if (a == 1 && c >= 0) std::snprintf(out, n, "prova: standby (frasi ferme in %ld ms), sveglio in %ld ms", b, c);
            else if (a == 1) std::snprintf(out, n, "prova: standby (frasi ferme in %ld ms), non si sveglia", b);
            else std::snprintf(out, n, "prova: PMREQ ignorato, frasi ancora dopo 3 s");
            break;
        case Ev::PROVA_FINE:
            std::snprintf(out, n, "prova finita: %ld cicli, %ld standby, %ld ignorati%s%s", a, b, c,
                          e.perche ? ": " : "", e.perche ? e.perche : "");
            break;
        default:
            std::snprintf(out, n, "evento %d", int(e.tipo));
            break;
    }
}

namespace gps {

// ---------------------------------------------------------------- UBX

size_t frameUbx(uint8_t cls, uint8_t id, const uint8_t* dati, uint16_t len, uint8_t* out, size_t cap) {
    const size_t n = size_t(len) + 8;
    if (n > cap) return 0;
    out[0] = 0xB5;
    out[1] = 0x62;
    out[2] = cls;
    out[3] = id;
    out[4] = uint8_t(len);
    out[5] = uint8_t(len >> 8);
    if (len) std::memcpy(out + 6, dati, len);
    uint8_t ca = 0, cb = 0;
    for (size_t i = 2; i < n - 2; i++) {
        ca += out[i];
        cb += ca;
    }
    out[n - 2] = ca;
    out[n - 1] = cb;
    return n;
}

size_t fraseNmea(const char* corpo, char* out, size_t cap) {
    uint8_t sum = 0;
    for (const char* p = corpo; *p; p++) sum ^= uint8_t(*p);
    const int n = std::snprintf(out, cap, "$%s*%02X\r\n", corpo, unsigned(sum));
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool LettoreUbx::byte(uint8_t c) {
    fatto_ = false;
    switch (stato_) {
        case 0:
            if (c != 0xB5) return false;
            stato_ = 1;
            return true;
        case 1:
            if (c == 0x62) {
                stato_ = 2;
                ca_ = cb_ = 0;
                return true;
            }
            stato_ = c == 0xB5 ? 1 : 0;
            return c == 0xB5;  // un altro carattere va all'analizzatore NMEA
        case 2:
            cls_ = c;
            break;
        case 3:
            id_ = c;
            break;
        case 4:
            len_ = c;
            break;
        case 5:
            len_ = uint16_t(len_ | (uint16_t(c) << 8));
            if (len_ > MAX) {  // non e' un frame che aspettiamo (o e' rotto): non si inghiottono le frasi dopo
                scartati_++;
                stato_ = 0;
                return true;
            }
            pos_ = 0;
            ca_ += c;
            cb_ += ca_;
            stato_ = len_ ? 6 : 7;
            return true;
        case 6:
            dati_[pos_++] = c;
            ca_ += c;
            cb_ += ca_;
            if (pos_ >= len_) stato_ = 7;
            return true;
        case 7:
            ckA_ = c;
            stato_ = 8;
            return true;
        default:  // 8: secondo byte del checksum
            stato_ = 0;
            if (ckA_ == ca_ && c == cb_) fatto_ = true;
            else scartati_++;
            return true;
    }
    ca_ += c;  // classe, id, lunghezza bassa
    cb_ += ca_;
    stato_++;
    return true;
}

// ---------------------------------------------------------------- pilota

Pilota::Pilota(Porta& porta, Fix& fix) : porta_(porta), fix_(fix) {}

void Pilota::vuoi(bool acceso, const char* perche) {
    perche_.store(perche);
    if (!acceso) voluto_.store(false);
    else if (!voluto_.exchange(true)) accesoChiesto_.store(porta_.ms());
    volutoGen_.fetch_add(1);
}

void Pilota::freddo() { freddoChiesto_.store(uint32_t(porta_.ms()) | 1u); }

void Pilota::chiediVersione() { versioneChiesta_.store(uint32_t(porta_.ms()) | 1u); }

void Pilota::prova(int cicli, bool forza) {
    cicli = std::max(1, std::min(cicli, Gnss::PROVA_MAX));
    porta_.blocca();
    const bool gia = prova_.inCorso;
    if (!gia) {
        const uint32_t numero = prova_.numero + 1;
        prova_ = Gnss::Prova();
        prova_.numero = numero;
        prova_.inCorso = true;
        prova_.forza = forza;
        prova_.cicli = uint8_t(cicli);
    }
    porta_.sblocca();
    if (!gia) provaChiesta_.store(uint32_t(cicli) | (forza ? 0x100u : 0u) | 0x10000u);
}

void Pilota::avvio() {
    const uint64_t now = porta_.ms();
    genVista_ = volutoGen_.load();
    volutoVisto_ = voluto_.load();
    volutoDa_ = now;
    evento(Gnss::Ev::VOLUTO, volutoVisto_ ? 1 : 0, 0, 0, perche_.load());
    rileva();
}

void Pilota::giro() {
    uint8_t buf[128];
    const int n = porta_.leggi(buf, sizeof(buf), GIRO_MS);
    const uint64_t now = porta_.ms();
    porta_.blocca();
    for (int i = 0; i < n; i++) {
        // I frame UBX (risposte a MON-VER, MON-HW3) non passano dall'analizzatore NMEA: dentro possono
        // esserci '$' e a capo, che farebbero contare frasi rotte che non ci sono.
        if (ubx_.byte(buf[i])) {
            if (ubx_.fatto()) ricevutoUbx();
        } else {
            nmea_.feed(char(buf[i]), fix_, now);
        }
    }
    nmea_.scadenze(fix_, now);
    // Torna a parlare dopo almeno 1,5 s di silenzio: si e' appena svegliato (anche nella prova al banco, o
    // da solo). I comandi UBX aspettano 1 s da qui (pronto()).
    const uint64_t prima = ultima_;
    ultima_ = fix_.frasiAtMs;
    if (ultima_ > prima && (!prima || ultima_ - prima >= FERME_MS)) svegliatoDa_ = ultima_;
    porta_.sblocca();
    passo(now);
}

void Pilota::entra(Gnss::Stato s, uint64_t now) {
    stato_.store(uint8_t(s));
    statoDa_ = now;
    dorme_.store(s == Gnss::Stato::STANDBY);
}

void Pilota::passo(uint64_t now) {
    const uint32_t gen = volutoGen_.load();
    if (gen != genVista_) {
        genVista_ = gen;
        const bool v = voluto_.load();
        if (v != volutoVisto_) {
            volutoVisto_ = v;
            evento(Gnss::Ev::VOLUTO, v ? 1 : 0, 0, 0, perche_.load());
            if (v) {
                volutoDa_ = now;
                riproveDormi_ = 0;
                rinuncia_ = false;
                rilevaDopo_ = now;  // MUTO: si riprova subito
            }
        }
    }
    richieste(now);
    const bool v = volutoVisto_;
    switch (stato()) {
        case Gnss::Stato::RILEVA:
            rileva();
            break;
        case Gnss::Stato::MUTO:
            // Frasi valide alla velocita' rimasta (9600): un ricevitore c'e', lo si riconosce subito.
            if (ultima_ > statoDa_ || (v && now >= rilevaDopo_)) rileva();
            break;
        case Gnss::Stato::ACCESO: {
            if (!v) {
                vaiSpento(now);
                break;
            }
            // Cane da guardia: voluto acceso e muto da 5 s (contando dall'accensione chiesta e
            // dall'ingresso nello stato): lo si risveglia; se non si sa che ricevitore e', lo si cerca.
            const uint64_t da = std::max(ultima_, std::max(volutoDa_, statoDa_));
            if (dopo(now, da) > SILENZIO_MS) {
                evento(Gnss::Ev::SILENZIO, ms32(dopo(now, da)));
                if (tipo_ != M_NESSUNO) entraSveglia(now);
                else rileva();
            }
            break;
        }
        case Gnss::Stato::SVEGLIA:
            passoSveglia(now);
            break;
        case Gnss::Stato::ADDORMENTA:
            passoAddormenta(now);
            break;
        case Gnss::Stato::STANDBY:
            if (v) {
                entraSveglia(now);
            } else if (ultima_ > standbyDa_) {
                evento(Gnss::Ev::DA_SOLO);
                nuovaSessione(ultima_);
                entra(Gnss::Stato::ACCESO, now);  // al prossimo passo di nuovo in standby (con un limite)
            }
            break;
        case Gnss::Stato::PROVA:
            passoProva(now);
            break;
    }
    sessione(now);
}

// Ricerca del ricevitore: prima 38400 (u-blox M10Q, col byte che lo sveglia se e' in standby), poi
// 9600 (L76K); tre giri. Blocca il compito del GPS fino a 9 s: intanto le richieste aspettano.
void Pilota::rileva() {
    const uint64_t t0 = porta_.ms();
    entra(Gnss::Stato::RILEVA, t0);
    for (int volta = 0; volta < 3; volta++) {
        porta_.velocita(38400);
        mandaFF();
        if (nmeaEntro(RILEVA_ATTESA_MS)) {
            trovato(M_UBLOX, 38400, t0);
            return;
        }
        porta_.velocita(9600);
        if (nmeaEntro(RILEVA_ATTESA_MS)) {
            trovato(M_L76K, 9600, t0);
            return;
        }
    }
    const uint64_t t = porta_.ms();
    tipo_ = M_NESSUNO;
    ublox_.store(false);
    modulo_.store("muto");
    evento(Gnss::Ev::MUTO, 0, 0, ms32(t - t0));
    rilevaDopo_ = t + RILEVA_OGNI_MS;
    entra(Gnss::Stato::MUTO, t);
}

void Pilota::trovato(uint8_t tipo, uint32_t baud, uint64_t t0) {
    const uint64_t t = porta_.ms();
    tipo_ = Modulo(tipo);
    rilevato_ = true;
    ublox_.store(tipo_ == M_UBLOX);
    modulo_.store(tipo_ == M_UBLOX ? "u-blox" : "L76K");
    porta_.blocca();
    fix_.frasiAtMs = t;  // la frase che l'ha fatto riconoscere (non e' passata dal fix)
    ultima_ = t;
    nmea_.azzera();      // via i pezzi di frase letti alla velocita' sbagliata
    porta_.sblocca();
    evento(Gnss::Ev::RILEVATO, tipo_ == M_UBLOX ? 1 : 2, int32_t(baud), ms32(t - t0));
    nuovaSessione(t);
    entra(Gnss::Stato::ACCESO, t);
}

// Aspetta una frase NMEA con il checksum giusto per al massimo ms millisecondi (con un analizzatore a
// parte: le frasi alle velocita' provate non vanno nel fix).
bool Pilota::nmeaEntro(uint32_t ms) {
    NmeaParser p;
    Fix f;
    uint8_t b[64];
    const uint64_t fine = porta_.ms() + ms;
    while (porta_.ms() < fine) {
        const int n = porta_.leggi(b, sizeof(b), 100);
        for (int i = 0; i < n; i++)
            if (p.feed(char(b[i]), f, 0)) return true;
    }
    return false;
}

// Voluto spento e ricevitore sveglio: standby, se si puo'. L'L76K resta acceso (come prima), e cosi'
// l'u-blox che ignora il PMREQ o che si risveglia da solo.
void Pilota::vaiSpento(uint64_t now) {
    if (tipo_ != M_UBLOX || pmreq_ < 0 || rinuncia_) return;
    if (riproveDormi_ >= TENTATIVI_DORMI) {
        rinuncia_ = true;
        evento(Gnss::Ev::RINUNCIA);
        return;
    }
    riproveDormi_++;
    entraAddormenta(now);
}

void Pilota::entraSveglia(uint64_t now) {
    entra(Gnss::Stato::SVEGLIA, now);
    svegliaDa_ = now;
    tentativi_ = 1;
    tentativoDa_ = now;
    mandaFF();  // un fronte sulla seriale lo sveglia
}

void Pilota::passoSveglia(uint64_t now) {
    if (ultima_ > svegliaDa_) {  // una frase valida dopo il primo 0xFF: e' sveglio
        const int32_t ms = ms32(ultima_ - svegliaDa_);
        porta_.blocca();
        diag_.svegliaMs = ms;
        diag_.svegliaTentativi = uint8_t(tentativi_);
        porta_.sblocca();
        evento(Gnss::Ev::SVEGLIO, ms, tentativi_);
        nuovaSessione(ultima_);
        entra(Gnss::Stato::ACCESO, now);
        return;
    }
    if (!volutoVisto_) {  // non lo si vuole piu': da ACCESO il passo dopo lo rimette in standby
        entra(Gnss::Stato::ACCESO, now);
        return;
    }
    if (dopo(now, tentativoDa_) < SVEGLIA_ATTESA_MS) return;
    if (tentativi_ < SVEGLIA_TENTATIVI) {
        tentativi_++;
        tentativoDa_ = now;
        mandaFF();
        return;
    }
    evento(Gnss::Ev::SVEGLIA_FALLITA, tentativi_);
    rileva();
}

void Pilota::entraAddormenta(uint64_t now) {
    entra(Gnss::Stato::ADDORMENTA, now);
    dormiDa_ = now;
    faseDormi_ = 0;
    passoAddormenta(now);
}

void Pilota::passoAddormenta(uint64_t now) {
    if (volutoVisto_) {
        if (faseDormi_ == 2) entraSveglia(now);  // il PMREQ e' partito: puo' darsi che dorma
        else entra(Gnss::Stato::ACCESO, now);
        return;
    }
    if (faseDormi_ == 0) {
        if (!parla(now, TACE_PRIMA_MS)) {
            // Tace da 1,5 s: forse dorme gia', e un M10 addormentato non sente i comandi. Prima lo si
            // sveglia (come Meshtastic), poi il PMREQ.
            faseDormi_ = 1;
            dormiDa_ = now;
            mandaFF();
            return;
        }
        if (!pronto(now)) return;  // appena svegliato: un attimo, se no il PMREQ puo' perdersi
        parlavaPrima_ = true;
        prontoPrima_ = true;
        pmreqDa_ = now;
        faseDormi_ = 2;
        mandaPmreq(PMREQ_NORMALE);
        return;
    }
    if (faseDormi_ == 1) {
        if (dopo(now, dormiDa_) < DOPO_FF_MS) return;
        parlavaPrima_ = parla(now, TACE_PRIMA_MS);
        prontoPrima_ = pronto(now);  // ha risposto allo 0xFF da meno di 1 s: no
        pmreqDa_ = now;
        faseDormi_ = 2;
        mandaPmreq(PMREQ_NORMALE);
        return;
    }
    if (ultima_ > pmreqDa_ + PMREQ_ENTRO_MS) {  // parla ancora 3 s dopo il PMREQ
        if (parlavaPrima_ && prontoPrima_) {
            // Era sveglio da almeno 1 s quando gli e' arrivato: lo ignora. Da qui in poi resta sempre acceso.
            pmreq_ = -1;
            porta_.blocca();
            diag_.pmreq = -1;
            porta_.sblocca();
            evento(Gnss::Ev::PMREQ_IGNORATO, PMREQ_NORMALE);
        } else {
            // Taceva, o rispondeva allo 0xFF da meno di 1 s: il PMREQ e' arrivato mentre si svegliava e si e'
            // perso. Si rimanda (da ACCESO, a ricevitore sveglio da 1 s).
            evento(Gnss::Ev::PMREQ_RIPROVA);
        }
        entra(Gnss::Stato::ACCESO, now);
        return;
    }
    const uint64_t da = std::max(ultima_, pmreqDa_);
    if (dopo(now, da) < FERME_MS) return;
    const int32_t ferme = ultima_ > pmreqDa_ ? ms32(ultima_ - pmreqDa_) : 0;
    if (parlavaPrima_) {
        pmreq_ = 1;
        porta_.blocca();
        diag_.pmreq = 1;
        porta_.sblocca();
    }
    evento(Gnss::Ev::PMREQ_OK, ferme, PMREQ_NORMALE, parlavaPrima_ ? 1 : 0);
    entraStandby(now);
}

void Pilota::entraStandby(uint64_t now) {
    entra(Gnss::Stato::STANDBY, now);
    standbyDa_ = now;
    porta_.blocca();
    datiARiposo();
    porta_.sblocca();
}

void Pilota::richieste(uint64_t now) {
    const uint32_t n32 = uint32_t(now);
    if (const uint32_t p = provaChiesta_.exchange(0)) iniziaProva(p, now);
    // I comandi solo a ricevitore sveglio da un attimo (frase nell'ultimo secondo): un M10 che dorme o
    // che si sta svegliando li perde.
    const bool sveglio = stato() == Gnss::Stato::ACCESO && pronto(now);
    uint32_t fr = freddoChiesto_.load();
    if (fr) {
        if (int32_t(n32 - fr) > int32_t(FREDDO_VALE_MS)) {
            if (freddoChiesto_.compare_exchange_strong(fr, 0)) evento(Gnss::Ev::FREDDO_SCADUTO);
        } else if (sveglio && tipo_ != M_NESSUNO && freddoChiesto_.compare_exchange_strong(fr, 0)) {
            if (tipo_ == M_UBLOX) {
                // UBX-CFG-RST: navBbrMask 0xFFFF (cancella tutto: ripartenza da freddo), resetMode 2 (solo
                // il GNSS, la seriale resta com'e').
                const uint8_t rst[4] = {0xFF, 0xFF, 0x02, 0x00};
                mandaUbx(0x06, 0x04, rst, sizeof(rst));
            } else {
                char b[32];
                const size_t k = fraseNmea("PCAS10,2", b, sizeof(b));  // L76K (comandi CASIC): ripartenza da freddo
                if (k) porta_.scrivi(reinterpret_cast<const uint8_t*>(b), k);
            }
            porta_.blocca();
            // Come dopo l'interruttore: niente posizione ne' ora. La diagnosi (frasi, contatori) resta.
            Fix nuovo;
            nuovo.frasiAtMs = fix_.frasiAtMs;
            nuovo.rmcAtMs = fix_.rmcAtMs;
            nuovo.ggaAtMs = fix_.ggaAtMs;
            nuovo.gsvAtMs = fix_.gsvAtMs;
            nuovo.frasiOk = fix_.frasiOk;
            nuovo.frasiKo = fix_.frasiKo;
            fix_ = nuovo;
            nmea_.azzera();
            diag_.freddi++;
            porta_.sblocca();
            evento(Gnss::Ev::FREDDO);
            nuovaSessione(now);
        }
    }
    uint32_t vr = versioneChiesta_.load();
    if (vr) {
        if (int32_t(n32 - vr) > int32_t(VERSIONE_VALE_MS) || tipo_ == M_L76K) {
            versioneChiesta_.compare_exchange_strong(vr, 0);  // l'L76K non parla UBX
        } else if (sveglio && tipo_ == M_UBLOX && versioneChiesta_.compare_exchange_strong(vr, 0)) {
            mandaUbx(0x0A, 0x04, nullptr, 0);  // UBX-MON-VER
            mandaUbx(0x0A, 0x37, nullptr, 0);  // UBX-MON-HW3
        }
    }
}

void Pilota::iniziaProva(uint32_t richiesta, uint64_t now) {
    provaCicli_ = int(richiesta & 0xFF);
    provaForza_ = (richiesta & 0x100) != 0;
    if (tipo_ != M_UBLOX) {  // non parte: lo stato del ricevitore non cambia
        const char* errore = tipo_ == M_NESSUNO ? "il GPS non risponde" : "solo con l'u-blox";
        porta_.blocca();
        prova_.inCorso = false;
        prova_.errore = errore;
        porta_.sblocca();
        evento(Gnss::Ev::PROVA_FINE, 0, 0, 0, errore);
        return;
    }
    evento(Gnss::Ev::PROVA_INIZIO, provaCicli_, provaForza_ ? PMREQ_FORZA : PMREQ_NORMALE);
    entra(Gnss::Stato::PROVA, now);
    fase_ = P_PRONTO;
    faseDa_ = now;
    provaPronta_ = false;
}

void Pilota::passoProva(uint64_t now) {
    switch (fase_) {
        case P_PRONTO:  // deve parlare; se tace lo si sveglia (fino a 5 volte)
            if (parla(now, FERME_MS)) {
                fase_ = P_PMREQ;  // il PMREQ a ricevitore sveglio da 1 s (anche da una sveglia di prima della prova)
                break;
            }
            if (!provaPronta_) {
                provaPronta_ = true;
                provaTentativi_ = 1;
                provaTentativoDa_ = now;
                mandaFF();
                break;
            }
            if (dopo(now, provaTentativoDa_) < SVEGLIA_ATTESA_MS) break;
            if (provaTentativi_ < SVEGLIA_TENTATIVI) {
                provaTentativi_++;
                provaTentativoDa_ = now;
                mandaFF();
                break;
            }
            fineProva(now, "il GPS non risponde");
            break;
        case P_PMREQ:  // a ricevitore sveglio da almeno 1 s, come nel caso normale
            if (!parla(now, FERME_MS)) {
                fase_ = P_PRONTO;
                provaPronta_ = false;
                break;
            }
            if (!pronto(now)) break;
            ciclo_ = Gnss::CicloProva();
            provaPmreqDa_ = now;
            fase_ = P_FERMA;
            mandaPmreq(provaForza_ ? PMREQ_FORZA : PMREQ_NORMALE);
            break;
        case P_FERMA: {
            if (ultima_ > provaPmreqDa_ + PMREQ_ENTRO_MS) {  // parla ancora: PMREQ ignorato
                ciclo_.esito = 0;
                chiudiCiclo(now);
                break;
            }
            const uint64_t da = std::max(ultima_, provaPmreqDa_);
            if (dopo(now, da) < FERME_MS) break;
            ciclo_.esito = 1;
            ciclo_.fermeMs = int16_t(ultima_ > provaPmreqDa_ ? std::min<uint64_t>(ultima_ - provaPmreqDa_, INT16_MAX) : 0);
            fase_ = P_SONNO;
            faseDa_ = now;
            dorme_.store(true);
            porta_.blocca();
            frasiOkSonno_ = fix_.frasiOk;
            datiARiposo();
            porta_.sblocca();
            break;
        }
        case P_SONNO:
            if (dopo(now, faseDa_) < SONNO_PROVA_MS) break;
            porta_.blocca();
            ciclo_.frasiNelSonno = uint8_t(std::min<uint32_t>(fix_.frasiOk - frasiOkSonno_, 255));
            porta_.sblocca();
            dorme_.store(false);
            fase_ = P_SVEGLIA;
            provaSvegliaDa_ = now;
            provaTentativi_ = 1;
            provaTentativoDa_ = now;
            mandaFF();
            break;
        case P_SVEGLIA:
            if (ultima_ > provaSvegliaDa_) {
                ciclo_.sveglioMs = int16_t(std::min<uint64_t>(ultima_ - provaSvegliaDa_, INT16_MAX));
                ciclo_.tentativi = uint8_t(provaTentativi_);
                chiudiCiclo(now);
                break;
            }
            if (dopo(now, provaTentativoDa_) < SVEGLIA_ATTESA_MS) break;
            if (provaTentativi_ < SVEGLIA_TENTATIVI) {
                provaTentativi_++;
                provaTentativoDa_ = now;
                mandaFF();
                break;
            }
            ciclo_.tentativi = uint8_t(provaTentativi_);
            chiudiCiclo(now);  // sveglioMs -1: la prova si ferma
            break;
    }
}

void Pilota::chiudiCiclo(uint64_t now) {
    porta_.blocca();
    const int k = prova_.fatti;
    if (k < Gnss::PROVA_MAX) prova_.ciclo[k] = ciclo_;
    prova_.fatti = uint8_t(k + 1);
    porta_.sblocca();
    evento(Gnss::Ev::PROVA_CICLO, ciclo_.esito, ciclo_.fermeMs, ciclo_.sveglioMs);
    if (ciclo_.esito == 1 && ciclo_.sveglioMs < 0) {
        fineProva(now, "non si sveglia piu'");
    } else if (k + 1 >= provaCicli_) {
        fineProva(now, nullptr);
    } else {
        fase_ = P_PMREQ;  // parla: appena svegliato (il PMREQ tra 1 s) o col PMREQ ignorato (subito)
    }
}

void Pilota::fineProva(uint64_t now, const char* errore) {
    int fatti, standby = 0, ignorati = 0;
    porta_.blocca();
    prova_.inCorso = false;
    prova_.errore = errore;
    fatti = prova_.fatti;
    for (int i = 0; i < fatti && i < Gnss::PROVA_MAX; i++) {
        if (prova_.ciclo[i].esito == 1) standby++;
        else if (prova_.ciclo[i].esito == 0) ignorati++;
    }
    porta_.sblocca();
    evento(Gnss::Ev::PROVA_FINE, fatti, standby, ignorati, errore);
    // Torna come lo vuole power(): da ACCESO il passo normale lo rimette in standby, o lo risveglia
    // (cane da guardia) se tace.
    riproveDormi_ = 0;
    rinuncia_ = false;
    entra(Gnss::Stato::ACCESO, now);
}

void Pilota::nuovaSessione(uint64_t t) {
    sessioneDa_ = t;
    svegliatoDa_ = t;  // sveglia, riconoscimento, ripartenza da freddo: i comandi UBX aspettano 1 s
    primaOra_ = primoFix_ = false;
    ricercaDa_ = t;
    porta_.blocca();
    diag_.snrMaxAcceso = 0;
    diag_.accesoAtMs = t;
    porta_.sblocca();
}

// Storia della sessione (dalla prima frase dopo una sveglia): prima ora, prima posizione e, ogni minuto
// senza posizione, quanti satelliti sente e il segnale migliore.
void Pilota::sessione(uint64_t now) {
    if (stato() != Gnss::Stato::ACCESO || !sessioneDa_) return;
    porta_.blocca();
    const bool oraNuova = fix_.timeValid && fix_.timeAtMs >= sessioneDa_;
    const uint64_t oraDa = fix_.timeAtMs;
    // La prima posizione si scrive 0,7 s dopo la sua RMC: intanto sono arrivate la GGA (usati) e le GSV
    // (con segnale) della stessa epoca, anche a 9600 baud.
    const bool fixNuovo = fix_.valid && fix_.atMs >= sessioneDa_ && dopo(now, fix_.atMs) >= 700;
    const uint64_t fixDa = fix_.atMs;
    const bool valida = fix_.valid;
    const int usati = fix_.sats, con = fix_.conSegnale, snr = fix_.snrMax, vista = fix_.inVista;
    if (fix_.snrMax > diag_.snrMaxAcceso) diag_.snrMaxAcceso = fix_.snrMax;
    porta_.sblocca();
    if (!primaOra_ && oraNuova) {
        primaOra_ = true;
        evento(Gnss::Ev::PRIMA_ORA, ms32(oraDa - sessioneDa_));
    }
    if (!primoFix_ && fixNuovo) {
        primoFix_ = true;
        evento(Gnss::Ev::PRIMO_FIX, ms32(fixDa - sessioneDa_), usati, con);
    }
    if (valida) {
        ricercaDa_ = now;
    } else if (volutoVisto_ && parla(now, RISPONDE_MS) && dopo(now, ricercaDa_) >= RICERCA_OGNI_MS) {
        ricercaDa_ = now;
        evento(Gnss::Ev::RICERCA, con, snr, vista);
    }
}

void Pilota::mandaFF() {
    const uint8_t ff = 0xFF;
    porta_.scrivi(&ff, 1);
}

void Pilota::mandaPmreq(uint8_t flag) {
    // UBX-RXM-PMREQ (16 byte, M10 SPG 5.10): durata 0 = finche' non arriva un fronte, flag (backup, o
    // backup + force), sveglia da UART RX.
    const uint8_t pm[16] = {0, 0, 0, 0, 0, 0, 0, 0, flag, 0, 0, 0, 0x08, 0, 0, 0};
    mandaUbx(0x02, 0x41, pm, sizeof(pm));
}

void Pilota::mandaUbx(uint8_t cls, uint8_t id, const uint8_t* dati, uint16_t len) {
    uint8_t b[32];  // il frame intero, scritto in una volta sola
    const size_t n = gps::frameUbx(cls, id, dati, len, b, sizeof(b));
    if (n) porta_.scrivi(b, n);
}

void Pilota::datiARiposo() {
    fix_.valid = false;  // lat, lon e atMs restano: l'ultima posizione buona
    fix_.sats = 0;
    fix_.hdop = 99;
    fix_.inVista = 0;
    fix_.conSegnale = 0;
    fix_.snrMax = 0;
    nmea_.azzera();
}

void Pilota::ricevutoUbx() {
    const uint8_t* d = ubx_.dati();
    const uint16_t n = ubx_.lunghezza();
    if (ubx_.classe() == 0x0A && ubx_.id() == 0x04 && n >= 40) {  // UBX-MON-VER
        copiaTesto(ver_.sw, sizeof(ver_.sw), d, 30);
        copiaTesto(ver_.hw, sizeof(ver_.hw), d + 30, 10);
        const int quante = std::min<int>((n - 40) / 30, 8);
        ver_.nEstensioni = uint8_t(quante);
        for (int i = 0; i < quante; i++) copiaTesto(ver_.estensioni[i], sizeof(ver_.estensioni[i]), d + 40 + 30 * i, 30);
        ver_.risposteVer++;
    } else if (ubx_.classe() == 0x0A && ubx_.id() == 0x37 && n >= 22) {  // UBX-MON-HW3
        ver_.hw3Pin = d[1];
        ver_.hw3Flags = d[2];
        ver_.hw3Len = uint16_t(std::min<size_t>(n, sizeof(ver_.hw3Dati)));
        std::memcpy(ver_.hw3Dati, d, ver_.hw3Len);
        ver_.risposteHw3++;
    }
}

void Pilota::evento(Gnss::Ev tipo, int32_t a, int32_t b, int32_t c, const char* perche) {
    Gnss::Evento e;
    e.ms = uint32_t(porta_.ms());
    e.tipo = tipo;
    e.a = a;
    e.b = b;
    e.c = c;
    e.perche = perche;
    porta_.blocca();
    storia_[(storiaInizio_ + storiaN_) % Gnss::STORIA] = e;
    if (storiaN_ < Gnss::STORIA) storiaN_++;
    else storiaInizio_ = (storiaInizio_ + 1) % Gnss::STORIA;
    porta_.sblocca();
    if (tipo == Gnss::Ev::VOLUTO) return;  // acceso e spento li scrive nel registro chi li decide (main.cpp)
    char t[128];
    Gnss::testoEvento(e, t, sizeof(t));
    const bool avviso = tipo == Gnss::Ev::MUTO || tipo == Gnss::Ev::SILENZIO || tipo == Gnss::Ev::SVEGLIA_FALLITA ||
                        tipo == Gnss::Ev::PMREQ_IGNORATO || tipo == Gnss::Ev::RINUNCIA || tipo == Gnss::Ev::FREDDO ||
                        tipo == Gnss::Ev::FREDDO_SCADUTO;
    porta_.registro(avviso, t);
}

// ---------------------------------------------------------------- letture

bool Pilota::risponde() const {
    porta_.blocca();
    const uint64_t u = fix_.frasiAtMs;
    porta_.sblocca();
    const uint64_t now = porta_.ms();
    return u && (u >= now || now - u <= RISPONDE_MS);
}

uint32_t Pilota::silenzioMs() const {
    if (!voluto_.load()) return 0;
    const uint64_t acceso = accesoChiesto_.load();
    porta_.blocca();
    const uint64_t u = fix_.frasiAtMs;
    porta_.sblocca();
    const uint64_t now = porta_.ms();
    const uint64_t da = std::max(acceso, u);
    return now > da ? uint32_t(std::min<uint64_t>(now - da, UINT32_MAX)) : 0;
}

Gnss::Diagnosi Pilota::diagnosi() const {
    porta_.blocca();
    Gnss::Diagnosi d = diag_;
    porta_.sblocca();
    d.stato = stato();
    d.standby = inStandby();
    return d;
}

int Pilota::storia(Gnss::Evento* out, int max) const {
    porta_.blocca();
    const int n = std::max(0, std::min(max, storiaN_));
    const int salta = storiaN_ - n;  // con poco posto: gli ultimi n
    for (int i = 0; i < n; i++) out[i] = storia_[(storiaInizio_ + salta + i) % Gnss::STORIA];
    porta_.sblocca();
    return n;
}

Gnss::Versione Pilota::versione() const {
    porta_.blocca();
    const Gnss::Versione v = ver_;
    porta_.sblocca();
    return v;
}

Gnss::Prova Pilota::provaStato() const {
    porta_.blocca();
    const Gnss::Prova p = prova_;
    porta_.sblocca();
    return p;
}

}  // namespace gps
}  // namespace scheda
