// Cuffie USB-C: lettura dei descrittori USB Audio 1.0 e 2.0, strada del suono, clock e scelta del formato
// (vedi uac_desc.h). Niente ESP-IDF: si compila anche sul PC (componenti/usb_audio/test_host e test/).
//
// I descrittori vengono dal dispositivo e possono essere sbagliati in ogni modo, quindi:
//  - prima si controlla tutta la catena dei bLength (ognuno >= 2 e dentro min(n, wTotalLength)); se si rompe,
//    MALFORMATO e non si guarda altro;
//  - poi ogni campo si legge solo se sta dentro il bLength del suo descrittore;
//  - i limiti MAX_* non si superano mai: il resto si perde e Configurazione::troncata diventa true.
// Non alloca niente e usa poco stack (qualche centinaio di byte): analizza() gira nel compito di controllo
// delle cuffie, che ha 6 KB di stack in tutto.
//
// Le posizioni dei campi (contando da bLength = 0) sono quelle delle specifiche: USB 2.0 capitolo 9, USB Audio
// 1.0 e 2.0 capitolo 4. Sono scritte accanto a ogni lettura.
#include "usb_audio/uac_desc.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace uac {

namespace {

// ---- costanti (gli stessi valori di Linux include/uapi/linux/usb/audio.h e include/linux/usb/audio-v2.h) ----

// bDescriptorType
constexpr uint8_t DT_CONFIGURAZIONE = 0x02;
constexpr uint8_t DT_STRINGA = 0x03;
constexpr uint8_t DT_INTERFACCIA = 0x04;
constexpr uint8_t DT_ENDPOINT = 0x05;
constexpr uint8_t DT_IAD = 0x0B;             // Interface Association Descriptor
constexpr uint8_t DT_CS_INTERFACCIA = 0x24;  // di classe, dell'interfaccia
constexpr uint8_t DT_CS_ENDPOINT = 0x25;     // di classe, dell'endpoint

// Interfacce audio.
constexpr uint8_t CLASSE_AUDIO = 0x01;
constexpr uint8_t SOTTOCLASSE_CONTROLLO = 0x01;  // AudioControl
constexpr uint8_t SOTTOCLASSE_STREAMING = 0x02;  // AudioStreaming
constexpr uint8_t PROTOCOLLO_UAC2 = 0x20;        // IP_VERSION_02_00 (UAC1 e' 0x00)
constexpr uint8_t PROTOCOLLO_UAC3 = 0x30;        // IP_VERSION_03_00

// bDescriptorSubtype dell'interfaccia AudioControl. Attenzione: 0x07 e 0x08 sono unita' diverse in UAC1 e UAC2.
constexpr uint8_t AC_HEADER = 0x01;
constexpr uint8_t AC_INPUT_TERMINAL = 0x02;
constexpr uint8_t AC_OUTPUT_TERMINAL = 0x03;
constexpr uint8_t AC_MIXER_UNIT = 0x04;
constexpr uint8_t AC_SELECTOR_UNIT = 0x05;
constexpr uint8_t AC_FEATURE_UNIT = 0x06;
constexpr uint8_t AC1_PROCESSING_UNIT = 0x07;
constexpr uint8_t AC1_EXTENSION_UNIT = 0x08;
constexpr uint8_t AC2_EFFECT_UNIT = 0x07;
constexpr uint8_t AC2_PROCESSING_UNIT = 0x08;
constexpr uint8_t AC2_EXTENSION_UNIT = 0x09;
constexpr uint8_t AC2_CLOCK_SOURCE = 0x0A;
constexpr uint8_t AC2_CLOCK_SELECTOR = 0x0B;
constexpr uint8_t AC2_CLOCK_MULTIPLIER = 0x0C;
constexpr uint8_t AC2_SAMPLE_RATE_CONVERTER = 0x0D;

// bDescriptorSubtype dell'interfaccia AudioStreaming e del suo endpoint.
constexpr uint8_t AS_GENERAL = 0x01;
constexpr uint8_t AS_FORMAT_TYPE = 0x02;
constexpr uint8_t EP_GENERAL = 0x01;

constexpr uint8_t FORMAT_TYPE_I = 0x01;
constexpr uint16_t UAC1_PCM = 0x0001;   // wFormatTag
constexpr uint32_t UAC2_PCM = 1u << 0;  // bmFormats

// bmAttributes dell'endpoint standard: tipo nei bit 0-1, sincronizzazione nei bit 2-3, uso nei bit 4-5.
constexpr uint8_t EP_ISOCRONO = 0x01;
constexpr uint8_t EP_USO_FEEDBACK = 0x01;

constexpr uint32_t HZ_48K = 48000;
constexpr uint32_t HZ_44K1 = 44100;

// ---- letture ----

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le24(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }
uint32_t le32(const uint8_t* p) { return le24(p) | (uint32_t(p[3]) << 24); }

template <typename T>
T minimo(T a, T b) {
    return a < b ? a : b;
}

// Insieme di numeri da 0 a 255 (id delle entita', numeri delle interfacce).
struct Insieme256 {
    uint8_t bit[32] = {};
    void metti(uint8_t i) { bit[i >> 3] = uint8_t(bit[i >> 3] | (1u << (i & 7))); }
    bool ha(uint8_t i) const { return ((bit[i >> 3] >> (i & 7)) & 1u) != 0; }
};

// Scorre i descrittori uno dopo l'altro, per bLength, dentro [0, fine).
class Scorri {
public:
    Scorri(const uint8_t* d, size_t fine) : d_(d), fine_(fine) {}
    // Il prossimo descrittore: p al suo bLength, len = bLength. false alla fine o se la catena si rompe
    // (bLength < 2 o oltre la fine); allora rotto() e' true.
    bool prossimo(const uint8_t*& p, size_t& len) {
        if (pos_ >= fine_) return false;
        const size_t resto = fine_ - pos_;
        if (resto < 2 || size_t(d_[pos_]) < 2 || size_t(d_[pos_]) > resto) {
            rotto_ = true;
            pos_ = fine_;
            return false;
        }
        p = d_ + pos_;
        len = d_[pos_];
        pos_ += len;
        return true;
    }
    bool rotto() const { return rotto_; }

private:
    const uint8_t* d_;
    size_t fine_;
    size_t pos_ = 0;
    bool rotto_ = false;
};

// ---- modello ----

void azzera(Configurazione& c) {
    c.versione = Versione::UAC1;
    c.bcdADC = 0;
    c.interfacciaControllo = 0;
    c.nEntita = 0;
    c.nStreaming = 0;
    c.troncata = false;
}

uint8_t nEntita(const Configurazione& c) { return minimo<uint8_t>(c.nEntita, uint8_t(MAX_ENTITA)); }
uint8_t nStreaming(const Configurazione& c) { return minimo<uint8_t>(c.nStreaming, uint8_t(MAX_STREAMING)); }
uint8_t nAlt(const Streaming& s) { return minimo<uint8_t>(s.nAlt, uint8_t(MAX_ALT)); }
uint8_t nSorgenti(const Entita& e) { return minimo<uint8_t>(e.nSorgenti, uint8_t(MAX_SORGENTI)); }

bool eClock(TipoEntita t) {
    return t == TipoEntita::CLOCK_SORGENTE || t == TipoEntita::CLOCK_SELETTORE ||
           t == TipoEntita::CLOCK_MOLTIPLICATORE;
}

// L'indice in c.entita dell'entita' con quell'id (la prima, se l'id si ripete), -1 se non c'e'.
int indiceEntita(const Configurazione& c, uint8_t id) {
    if (id == 0) return -1;
    const uint8_t n = nEntita(c);
    for (uint8_t i = 0; i < n; i++)
        if (c.entita[i].id == id) return i;
    return -1;
}

// Una nuova entita' (azzerata) in c. nullptr con l'id 0 (non valido), con un id gia' visto (vale il primo, come
// in Linux: cosi' ogni id indica una sola entita') o se non c'e' piu' posto.
Entita* nuovaEntita(Configurazione& c, uint8_t id, TipoEntita tipo) {
    if (id == 0 || indiceEntita(c, id) >= 0) return nullptr;
    if (c.nEntita >= MAX_ENTITA) {
        c.troncata = true;
        return nullptr;
    }
    Entita& e = c.entita[c.nEntita++];
    e = Entita{};
    e.id = id;
    e.tipo = tipo;
    return &e;
}

// Aggiunge a e le n sorgenti che partono da s (fino a MAX_SORGENTI).
void mettiSorgenti(Configurazione& c, Entita& e, const uint8_t* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (e.nSorgenti >= MAX_SORGENTI) {
            c.troncata = true;
            return;
        }
        e.sorgenti[e.nSorgenti++] = s[i];
    }
}

// Quanti elementi da 1 byte di un elenco che parte da `inizio` ci stanno davvero nel descrittore (len >= inizio):
// un elenco piu' lungo del suo descrittore si legge fin dove arriva.
size_t nelDescrittore(uint8_t dichiarati, size_t len, size_t inizio) {
    return minimo<size_t>(dichiarati, len - inizio);
}

// ---- frequenze ----

bool contieneHz(uint32_t min, uint32_t max, uint32_t passo, uint32_t hz) {
    if (hz < min || hz > max) return false;
    return passo == 0 || (hz - min) % passo == 0;
}

bool contieneUtile(const IntervalloHz& v) {
    return contieneHz(v.min, v.max, v.passo, HZ_48K) || contieneHz(v.min, v.max, v.passo, HZ_44K1);
}

// Aggiunge una voce a f. Se f e' piena la voce si perde, a meno che porti 48000 o 44100 (le sole frequenze che
// si usano, se f non le ha gia') e ci sia una voce che non contiene nessuna delle due: allora ne prende il
// posto. false se si e' perso qualcosa.
bool aggiungiFrequenza(Frequenze& f, uint32_t min, uint32_t max, uint32_t passo) {
    if (f.aggiungi(min, max, passo)) return true;
    const bool serve = (contieneHz(min, max, passo, HZ_48K) && !f.contiene(HZ_48K)) ||
                       (contieneHz(min, max, passo, HZ_44K1) && !f.contiene(HZ_44K1));
    if (!serve) return false;
    for (int i = int(MAX_FREQ) - 1; i >= 0; i--) {
        IntervalloHz& v = f.voci[i];
        if (contieneUtile(v)) continue;
        v.min = min;
        v.max = max;
        v.passo = passo;
        return false;
    }
    return false;
}

// ---- AudioControl ----

void leggiFeature(const uint8_t* p, size_t len, bool uac2, Configurazione& out) {
    // bUnitID 3, bSourceID 4, poi i controlli per canale (0 = master), e in fondo iFeature (1 byte).
    // UAC1: bControlSize 5, bmaControls da 6, bControlSize byte ciascuno; bit 0 muto, bit 1 volume.
    // UAC2: bmaControls da 5, 4 byte ciascuno; bit 0-1 muto, bit 2-3 volume, impostabili se 0b11.
    if (len < 5) return;
    Entita* e = nuovaEntita(out, p[3], TipoEntita::FEATURE);
    if (!e) return;
    mettiSorgenti(out, *e, p + 4, 1);
    size_t inizio = 5, passo = 4, canali = 0;  // canali contando anche il master
    if (uac2) {
        canali = len >= 6 ? (len - 6) / 4 : 0;
    } else {
        if (len < 7 || p[5] == 0) return;  // bControlSize 0: nessun controllo (e niente divisione per zero)
        inizio = 6;
        passo = p[5];
        canali = (len - 7) / passo;
    }
    if (canali == 0) return;
    if (canali - 1 > MAX_CANALI_FU) {
        out.troncata = true;
        canali = MAX_CANALI_FU + 1;
    }
    e->canali = uint8_t(canali - 1);
    for (size_t c = 0; c < canali; c++) {
        const uint8_t* m = p + inizio + c * passo;  // l'ultimo byte letto resta prima di iFeature
        bool muto, volume;
        if (uac2) {
            const uint32_t b = le32(m);
            muto = (b & 0x3u) == 0x3u;
            volume = ((b >> 2) & 0x3u) == 0x3u;
        } else {
            muto = (m[0] & 0x01) != 0;
            volume = (m[0] & 0x02) != 0;
        }
        if (muto) e->muto = uint16_t(e->muto | (1u << c));
        if (volume) e->volume = uint16_t(e->volume | (1u << c));
    }
}

// Un'entita' (terminale, unita' o clock) dell'interfaccia AudioControl.
void leggiEntita(const uint8_t* p, size_t len, bool uac2, Configurazione& out) {
    const uint8_t sottotipo = p[2];
    Entita* e = nullptr;
    switch (sottotipo) {
    case AC_INPUT_TERMINAL:
        // bTerminalID 3, wTerminalType 4-5; UAC2: bCSourceID 7 (UAC1 ha bNrChannels in 7).
        if (len < (uac2 ? 8u : 6u)) return;
        if ((e = nuovaEntita(out, p[3], TipoEntita::TERMINALE_INGRESSO)) == nullptr) return;
        e->tipoTerminale = le16(p + 4);
        if (uac2) e->clock = p[7];
        return;
    case AC_OUTPUT_TERMINAL:
        // bTerminalID 3, wTerminalType 4-5, bSourceID 7; UAC2: bCSourceID 8.
        if (len < (uac2 ? 9u : 8u)) return;
        if ((e = nuovaEntita(out, p[3], TipoEntita::TERMINALE_USCITA)) == nullptr) return;
        e->tipoTerminale = le16(p + 4);
        mettiSorgenti(out, *e, p + 7, 1);
        if (uac2) e->clock = p[8];
        return;
    case AC_MIXER_UNIT:
    case AC_SELECTOR_UNIT:
        // bUnitID 3, bNrInPins 4, baSourceID da 5.
        if (len < 5) return;
        e = nuovaEntita(out, p[3], sottotipo == AC_MIXER_UNIT ? TipoEntita::MIXER : TipoEntita::SELETTORE);
        if (e) mettiSorgenti(out, *e, p + 5, nelDescrittore(p[4], len, 5));
        return;
    case AC_FEATURE_UNIT:
        leggiFeature(p, len, uac2, out);
        return;
    default:
        break;
    }
    if (!uac2) {
        // UAC1 PROCESSING_UNIT e EXTENSION_UNIT: bUnitID 3, bNrInPins 6, baSourceID da 7.
        if ((sottotipo == AC1_PROCESSING_UNIT || sottotipo == AC1_EXTENSION_UNIT) && len >= 7) {
            e = nuovaEntita(out, p[3], TipoEntita::ALTRA_UNITA);
            if (e) mettiSorgenti(out, *e, p + 7, nelDescrittore(p[6], len, 7));
        }
        return;
    }
    switch (sottotipo) {
    case AC2_EFFECT_UNIT:
        // bUnitID 3, wEffectType 4-5, bSourceID 6.
        if (len < 7) return;
        e = nuovaEntita(out, p[3], TipoEntita::ALTRA_UNITA);
        if (e) mettiSorgenti(out, *e, p + 6, 1);
        return;
    case AC2_PROCESSING_UNIT:
    case AC2_EXTENSION_UNIT:
        // bUnitID 3, wProcessType/wExtensionCode 4-5, bNrInPins 6, baSourceID da 7.
        if (len < 7) return;
        e = nuovaEntita(out, p[3], TipoEntita::ALTRA_UNITA);
        if (e) mettiSorgenti(out, *e, p + 7, nelDescrittore(p[6], len, 7));
        return;
    case AC2_SAMPLE_RATE_CONVERTER:
        // bUnitID 3, bSourceID 4 (poi i due clock, che qui non servono).
        if (len < 5) return;
        e = nuovaEntita(out, p[3], TipoEntita::ALTRA_UNITA);
        if (e) mettiSorgenti(out, *e, p + 4, 1);
        return;
    case AC2_CLOCK_SOURCE:
        // bClockID 3, bmAttributes 4, bmControls 5.
        if (len < 6) return;
        if ((e = nuovaEntita(out, p[3], TipoEntita::CLOCK_SORGENTE)) == nullptr) return;
        e->attributiClock = p[4];
        e->controlliClock = p[5];
        return;
    case AC2_CLOCK_SELECTOR: {
        // bClockID 3, bNrInPins 4, baCSourceID da 5 a 4 + p, bmControls 5 + p.
        if (len < 5) return;
        if ((e = nuovaEntita(out, p[3], TipoEntita::CLOCK_SELETTORE)) == nullptr) return;
        const size_t n = nelDescrittore(p[4], len, 5);
        mettiSorgenti(out, *e, p + 5, n);
        if (n == size_t(p[4]) && 5 + n < len) e->controlliClock = p[5 + n];
        return;
    }
    case AC2_CLOCK_MULTIPLIER:
        // bClockID 3, bCSourceID 4.
        if (len < 5) return;
        if ((e = nuovaEntita(out, p[3], TipoEntita::CLOCK_MOLTIPLICATORE)) == nullptr) return;
        e->clock = p[4];
        return;
    default:
        return;
    }
}

// La funzione audio che si sta leggendo: la sua interfaccia AudioControl (alt 0) e la versione.
struct Funzione {
    const uint8_t* desc;
    size_t fine;            // min(n, wTotalLength); la catena dei bLength e' gia' stata controllata
    size_t inizioAC;        // dove sta il descrittore d'interfaccia dell'AudioControl
    uint8_t interfacciaAC;  // il suo bInterfaceNumber
    Versione versione;
};

// Prima lettura: le entita' dell'AudioControl della funzione e quali interfacce sono sue. daElenco dice se le
// interfacce sono in membri (UAC1: baInterfaceNr dell'header, sempre; UAC2: l'IAD che contiene l'AudioControl,
// se c'e') o se vanno prese per posizione (UAC2 senza IAD).
void leggiControllo(const Funzione& f, Configurazione& out, Insieme256& membri, bool& daElenco) {
    const bool uac2 = f.versione == Versione::UAC2;
    daElenco = !uac2;
    bool header = false;
    bool dentro = false;  // si stanno leggendo i descrittori dell'AudioControl della funzione
    Scorri s(f.desc, f.fine);
    const uint8_t* p = nullptr;
    size_t len = 0;
    while (s.prossimo(p, len)) {
        switch (p[1]) {
        case DT_INTERFACCIA:
            dentro = p == f.desc + f.inizioAC;
            break;
        case DT_IAD:
            // bFirstInterface 2, bInterfaceCount 3: vale il primo IAD che contiene l'AudioControl.
            if (uac2 && !daElenco && len >= 4) {
                const unsigned primo = p[2], quante = p[3], ac = f.interfacciaAC;
                if (ac >= primo && ac < primo + quante) {
                    daElenco = true;
                    for (unsigned i = primo; i < primo + quante && i < 256; i++) membri.metti(uint8_t(i));
                }
            }
            break;
        case DT_CS_INTERFACCIA:
            if (!dentro || len < 3) break;
            if (p[2] == AC_HEADER) {
                // bcdADC 3-4. UAC1: bInCollection 7, baInterfaceNr da 8.
                if (!header && len >= 5) {
                    header = true;
                    out.bcdADC = le16(p + 3);
                }
                if (!uac2 && len >= 8) {
                    const size_t n = nelDescrittore(p[7], len, 8);
                    for (size_t i = 0; i < n; i++) membri.metti(p[8 + i]);
                }
            } else {
                leggiEntita(p, len, uac2, out);
            }
            break;
        default:
            break;
        }
    }
}

// ---- AudioStreaming ----

// Un'alternativa che si sta leggendo. Si copia nella Configurazione quando finisce (al prossimo descrittore
// d'interfaccia o alla fine), e solo se ha l'endpoint dei dati.
struct InCorso {
    bool attiva = false;
    uint8_t interfaccia = 0;
    bool generale = false;         // AS_GENERAL letto
    bool pcmGenerale = false;      // ... e dice PCM
    bool formato = false;          // FORMAT_TYPE letto
    bool tipoI = false;            // ... ed e' di tipo I
    bool dati = false;             // c'e' l'endpoint dei dati
    bool ultimoDati = false;       // l'ultimo endpoint letto e' quello dei dati (per il suo CS endpoint)
    bool frequenzePerse = false;   // piu' di MAX_FREQ frequenze
    Alternativa a;
};

void inizia(InCorso& c, uint8_t interfaccia, uint8_t alt) {
    c.attiva = true;
    c.interfaccia = interfaccia;
    c.generale = c.pcmGenerale = c.formato = c.tipoI = c.dati = c.ultimoDati = c.frequenzePerse = false;
    c.a = Alternativa{};
    c.a.numero = alt;
}

// Lo streaming con quel numero d'interfaccia (nuovo se non c'e' ancora); nullptr se non c'e' piu' posto. Da
// pieno, uno streaming in uscita prende il posto dell'ultimo che va solo in ingresso: sono quelli che servono.
Streaming* streamingPer(Configurazione& c, uint8_t interfaccia, bool uscita) {
    const uint8_t n = nStreaming(c);
    for (uint8_t i = 0; i < n; i++)
        if (c.streaming[i].interfaccia == interfaccia) return &c.streaming[i];
    Streaming* s = nullptr;
    if (c.nStreaming < MAX_STREAMING) {
        s = &c.streaming[c.nStreaming++];
    } else {
        c.troncata = true;
        if (!uscita) return nullptr;
        for (int i = int(MAX_STREAMING) - 1; i >= 0 && s == nullptr; i--)
            if (!c.streaming[i].uscita) s = &c.streaming[i];
        if (s == nullptr) return nullptr;
    }
    s->interfaccia = interfaccia;
    s->uscita = false;
    s->nAlt = 0;
    return s;
}

void finisci(InCorso& c, Configurazione& out) {
    if (!c.attiva) return;
    c.attiva = false;
    if (!c.dati) return;  // senza endpoint dei dati (alt solo di controllo, endpoint bulk...) non si suona
    c.a.pcm = c.generale && c.pcmGenerale && c.formato && c.tipoI;
    const bool uscita = (c.a.endpoint & 0x80) == 0;
    Streaming* s = streamingPer(out, c.interfaccia, uscita);
    if (s == nullptr) return;
    if (s->nAlt >= MAX_ALT) {
        out.troncata = true;
        return;
    }
    s->alt[s->nAlt++] = c.a;
    if (uscita) s->uscita = true;
    if (c.frequenzePerse) out.troncata = true;
}

void leggiAS(InCorso& c, const uint8_t* p, size_t len, bool uac2) {
    if (len < 3) return;
    if (p[2] == AS_GENERAL && !c.generale) {
        if (!uac2 && len >= 7) {
            // UAC1: bTerminalLink 3, bDelay 4, wFormatTag 5-6.
            c.generale = true;
            c.a.terminale = p[3];
            c.pcmGenerale = le16(p + 5) == UAC1_PCM;
        } else if (uac2 && len >= 11) {
            // UAC2: bTerminalLink 3, bmControls 4, bFormatType 5, bmFormats 6-9, bNrChannels 10.
            c.generale = true;
            c.a.terminale = p[3];
            c.pcmGenerale = p[5] == FORMAT_TYPE_I && (le32(p + 6) & UAC2_PCM) != 0;
            c.a.canali = p[10];
        }
        return;
    }
    if (p[2] != AS_FORMAT_TYPE || c.formato) return;
    if (uac2) {
        // UAC2: bFormatType 3, bSubslotSize 4, bBitResolution 5.
        if (len < 6) return;
        c.formato = true;
        c.tipoI = p[3] == FORMAT_TYPE_I;
        if (!c.tipoI) return;
        c.a.byteCampione = p[4];
        c.a.bit = p[5];
        return;
    }
    // UAC1: bFormatType 3, bNrChannels 4, bSubframeSize 5, bBitResolution 6, bSamFreqType 7, poi le frequenze:
    // con bSamFreqType 0 tLowerSamFreq 8-10 e tUpperSamFreq 11-13, se no bSamFreqType frequenze da 3 byte.
    // Il tipo II ha un'altra disposizione: si guarda solo il tipo I.
    if (len < 8) return;
    c.formato = true;
    c.tipoI = p[3] == FORMAT_TYPE_I;
    if (!c.tipoI) return;
    c.a.canali = p[4];
    c.a.byteCampione = p[5];
    c.a.bit = p[6];
    const size_t nf = p[7];
    if (nf == 0) {
        if (len >= 14 && !aggiungiFrequenza(c.a.frequenze, le24(p + 8), le24(p + 11), 0)) c.frequenzePerse = true;
        return;
    }
    for (size_t i = 0; i < nf && 8 + 3 * i + 3 <= len; i++) {
        const uint32_t hz = le24(p + 8 + 3 * i);
        if (!aggiungiFrequenza(c.a.frequenze, hz, hz, 0)) c.frequenzePerse = true;
    }
}

void leggiEndpoint(InCorso& c, const uint8_t* p, size_t len, bool uac2) {
    c.ultimoDati = false;
    // bEndpointAddress 2, bmAttributes 3, wMaxPacketSize 4-5, bInterval 6; UAC1 (9 byte): bRefresh 7,
    // bSynchAddress 8.
    if (len < 7) return;
    const uint8_t indirizzo = p[2];
    const uint8_t attributi = p[3];
    if ((attributi & 0x03) != EP_ISOCRONO || (indirizzo & 0x0F) == 0) return;
    const uint8_t uso = uint8_t((attributi >> 4) & 0x03);
    if (!c.dati && uso != EP_USO_FEEDBACK) {
        // Il primo endpoint isocrono che non e' un feedback: quello dei dati (come prende Linux).
        c.dati = true;
        c.ultimoDati = true;
        c.a.endpoint = indirizzo;
        c.a.maxPacchetto = uint16_t(le16(p + 4) & 0x7FF);
        c.a.intervallo = p[6];
        c.a.sync = Sync((attributi >> 2) & 0x03);
        if (!uac2 && len >= 9) c.a.endpointFeedback = p[8];
        return;
    }
    if (uac2) {
        // UAC2: il feedback e' l'endpoint IN isocrono con uso "feedback" nella stessa alternativa.
        if ((indirizzo & 0x80) != 0 && uso == EP_USO_FEEDBACK && c.a.endpointFeedback == 0) {
            c.a.endpointFeedback = indirizzo;
            c.a.intervalloFeedback = p[6];
        }
    } else if (c.dati && c.a.endpointFeedback != 0 && indirizzo == c.a.endpointFeedback && len >= 8) {
        // UAC1: l'endpoint di sincronizzazione di bSynchAddress; il suo bRefresh dice ogni quanto risponde.
        c.a.intervalloFeedback = p[7];
    }
}

// Seconda lettura: le alternative degli streaming della funzione.
void leggiStreaming(const Funzione& f, const Insieme256& membri, bool daElenco, Configurazione& out) {
    const bool uac2 = f.versione == Versione::UAC2;
    InCorso c;
    bool dopoAC = false;  // UAC2 senza IAD: dopo l'AudioControl della funzione e prima della prossima
    Scorri s(f.desc, f.fine);
    const uint8_t* p = nullptr;
    size_t len = 0;
    while (s.prossimo(p, len)) {
        const uint8_t tipo = p[1];
        if (tipo == DT_INTERFACCIA) {
            finisci(c, out);
            // bInterfaceNumber 2, bAlternateSetting 3, bInterfaceClass 5, bInterfaceSubClass 6. Un descrittore
            // d'interfaccia troppo corto fa saltare tutto quello che lo segue, fino alla prossima interfaccia.
            if (len < 9) continue;
            const uint8_t numero = p[2], alt = p[3], classe = p[5], sottoclasse = p[6];
            if (p == f.desc + f.inizioAC)
                dopoAC = true;
            else if (classe == CLASSE_AUDIO && sottoclasse == SOTTOCLASSE_CONTROLLO && numero != f.interfacciaAC)
                dopoAC = false;
            const bool sua = daElenco ? membri.ha(numero) : dopoAC;
            if (sua && numero != f.interfacciaAC && classe == CLASSE_AUDIO && sottoclasse == SOTTOCLASSE_STREAMING &&
                alt >= 1)
                inizia(c, numero, alt);
            continue;
        }
        if (!c.attiva) continue;
        if (tipo == DT_CS_INTERFACCIA) {
            leggiAS(c, p, len, uac2);
        } else if (tipo == DT_ENDPOINT) {
            leggiEndpoint(c, p, len, uac2);
        } else if (tipo == DT_CS_ENDPOINT) {
            // UAC1 EP_GENERAL dell'endpoint dei dati: bmAttributes 3, bit 0 = frequenza impostabile.
            if (!uac2 && c.ultimoDati && len >= 4 && p[2] == EP_GENERAL) c.a.frequenzaSuEndpoint = (p[3] & 0x01) != 0;
            c.ultimoDati = false;
        }
    }
    finisci(c, out);
}

// Legge una funzione audio in out: true se ha almeno uno streaming in uscita.
bool leggiFunzione(const Funzione& f, Configurazione& out) {
    azzera(out);
    out.versione = f.versione;
    out.interfacciaControllo = f.interfacciaAC;
    Insieme256 membri;
    bool daElenco = false;
    leggiControllo(f, out, membri, daElenco);
    leggiStreaming(f, membri, daElenco, out);
    const uint8_t n = nStreaming(out);
    for (uint8_t i = 0; i < n; i++)
        if (out.streaming[i].uscita) return true;
    return false;
}

// Strada da c.entita[da] (un terminale d'uscita) indietro, per le sorgenti, fino all'entita' `a`: in
// profondita', senza ripassare da un'entita' gia' vista. true se c'e'; allora fu = la Feature Unit di quella
// strada piu' vicina ad `a` (0 se non ce n'e'). Niente ricorsione: la pila sta in 64 byte.
bool strada(const Configurazione& c, int da, uint8_t a, uint8_t& fu) {
    struct Passo {
        uint8_t entita;    // indice in c.entita
        uint8_t prossima;  // la prossima sorgente da provare
    };
    Passo pila[MAX_ENTITA];
    Insieme256 viste;
    int cima = 0;
    pila[0].entita = uint8_t(da);
    pila[0].prossima = 0;
    viste.metti(c.entita[da].id);
    while (cima >= 0) {
        Passo& passo = pila[cima];
        const Entita& e = c.entita[passo.entita];
        if (e.id == a) {
            fu = 0;
            for (int i = cima; i >= 0; i--) {
                const Entita& x = c.entita[pila[i].entita];
                if (x.tipo == TipoEntita::FEATURE) {
                    fu = x.id;
                    break;
                }
            }
            return true;
        }
        if (passo.prossima >= nSorgenti(e)) {
            cima--;
            continue;
        }
        const uint8_t id = e.sorgenti[passo.prossima++];
        if (id == 0 || viste.ha(id)) continue;
        viste.metti(id);
        const int j = indiceEntita(c, id);
        if (j < 0 || eClock(c.entita[j].tipo) || cima + 1 >= int(MAX_ENTITA)) continue;
        cima++;
        pila[cima].entita = uint8_t(j);
        pila[cima].prossima = 0;
    }
    return false;
}

const char* testoSync(Sync s) {
    switch (s) {
    case Sync::NESSUNA: return "senza sincronizzazione";
    case Sync::ASINCRONA: return "asincrono";
    case Sync::ADATTIVA: return "adattivo";
    case Sync::SINCRONA: return "sincrono";
    }
    return "?";
}

// Una riga di testo che si allunga senza uscire dal buffer (quello che non ci sta si perde).
class Riga {
public:
    void metti(const char* formato, ...) __attribute__((format(printf, 2, 3)));
    const char* testo() const { return b_; }

private:
    char b_[200] = {};
    size_t n_ = 0;
};

void Riga::metti(const char* formato, ...) {
    if (n_ + 1 >= sizeof(b_)) return;
    va_list ap;
    va_start(ap, formato);
    const int k = vsnprintf(b_ + n_, sizeof(b_) - n_, formato, ap);
    va_end(ap);
    if (k > 0) n_ = minimo(sizeof(b_) - 1, n_ + size_t(k));
}

using UL = unsigned long;  // uint32_t e' unsigned long sull'ESP32-S3 e unsigned int sul PC

}  // namespace

// ---- funzioni pubbliche ----

const char* testoEsito(Esito e) {
    switch (e) {
    case Esito::OK: return "ok";
    case Esito::MALFORMATO: return "descrittore non valido";
    case Esito::NON_AUDIO: return "non audio";
    case Esito::SOLO_UAC3: return "solo USB Audio 3";
    case Esito::NESSUNA_USCITA: return "senza uscita audio";
    case Esito::PACCHETTI_GRANDI: return "pacchetti troppo grandi";
    case Esito::FORMATO: return "formato non adatto";
    }
    return "?";
}

Esito analizza(const uint8_t* desc, size_t n, Configurazione& out) {
    // Niente "out = Configurazione{}": sarebbero piu' di 5 KB di temporaneo sullo stack. Si azzerano i campi.
    azzera(out);
    // Il primo descrittore e' la configurazione: bLength 0 (9), bDescriptorType 1, wTotalLength 2-3.
    if (desc == nullptr || n < 9 || desc[0] < 9 || desc[1] != DT_CONFIGURAZIONE) return Esito::MALFORMATO;
    const size_t fine = minimo<size_t>(n, le16(desc + 2));
    if (fine < size_t(desc[0])) return Esito::MALFORMATO;
    const uint8_t* p = nullptr;
    size_t len = 0;
    {
        Scorri s(desc, fine);
        while (s.prossimo(p, len)) {
        }
        if (s.rotto()) return Esito::MALFORMATO;
    }
    // Le funzioni audio, nell'ordine: vale la prima UAC1 o UAC2 con uno streaming in uscita.
    bool audio = false, supportata = false;
    Scorri s(desc, fine);
    while (s.prossimo(p, len)) {
        // bInterfaceNumber 2, bAlternateSetting 3, bInterfaceClass 5, bInterfaceSubClass 6, bInterfaceProtocol 7.
        if (p[1] != DT_INTERFACCIA || len < 9) continue;
        if (p[5] != CLASSE_AUDIO || p[6] != SOTTOCLASSE_CONTROLLO || p[3] != 0) continue;
        audio = true;
        if (p[7] == PROTOCOLLO_UAC3) continue;
        supportata = true;
        const Funzione f{desc, fine, size_t(p - desc), p[2],
                         p[7] == PROTOCOLLO_UAC2 ? Versione::UAC2 : Versione::UAC1};
        if (leggiFunzione(f, out)) return Esito::OK;
    }
    azzera(out);
    if (!audio) return Esito::NON_AUDIO;
    return supportata ? Esito::NESSUNA_USCITA : Esito::SOLO_UAC3;
}

const Entita* trovaEntita(const Configurazione& c, uint8_t id) {
    const int i = indiceEntita(c, id);
    return i < 0 ? nullptr : &c.entita[i];
}

bool percorso(const Configurazione& c, uint8_t terminale, Percorso& out) {
    out = Percorso{};
    const int t = indiceEntita(c, terminale);
    if (t < 0 || c.entita[t].tipo != TipoEntita::TERMINALE_INGRESSO) return false;
    if (c.versione == Versione::UAC2) out.clock = c.entita[t].clock;
    bool riserva = false;  // un terminale d'uscita che arriva al terminale, ma senza Feature Unit sulla strada
    Percorso r = out;
    const uint8_t n = nEntita(c);
    for (uint8_t i = 0; i < n; i++) {
        const Entita& e = c.entita[i];
        if (e.tipo != TipoEntita::TERMINALE_USCITA || e.tipoTerminale == TERMINALE_STREAMING_USB) continue;
        uint8_t fu = 0;
        if (!strada(c, i, terminale, fu)) continue;
        if (fu != 0) {
            out.terminaleUscita = e.id;
            out.tipoUscita = e.tipoTerminale;
            out.featureUnit = fu;
            return true;
        }
        if (!riserva) {
            riserva = true;
            r.terminaleUscita = e.id;
            r.tipoUscita = e.tipoTerminale;
        }
    }
    if (!riserva) return false;
    out = r;
    return true;
}

uint8_t sorgenteClock(const Configurazione& c, uint8_t id, PinSelettore pin, void* ctx) {
    Insieme256 visti;  // un giro (selettori o moltiplicatori che tornano indietro) finisce qui
    while (id != 0 && !visti.ha(id)) {
        visti.metti(id);
        const int i = indiceEntita(c, id);
        if (i < 0) return 0;
        const Entita& e = c.entita[i];
        switch (e.tipo) {
        case TipoEntita::CLOCK_SORGENTE:
            return e.id;
        case TipoEntita::CLOCK_SELETTORE: {
            const uint8_t n = nSorgenti(e);
            if (n == 0) return 0;
            uint8_t k = pin ? pin(ctx, e.id) : 0;
            if (k == 0 || k > n) k = 1;
            id = e.sorgenti[k - 1];
            break;
        }
        case TipoEntita::CLOCK_MOLTIPLICATORE:
            id = e.clock;
            break;
        default:
            return 0;
        }
    }
    return 0;
}

void impostaFrequenzeClock(Configurazione& c, uint8_t terminale, const Frequenze& f) {
    const uint8_t ns = nStreaming(c);
    for (uint8_t i = 0; i < ns; i++) {
        Streaming& s = c.streaming[i];
        const uint8_t na = nAlt(s);
        for (uint8_t j = 0; j < na; j++)
            if (s.alt[j].terminale == terminale && &s.alt[j].frequenze != &f) s.alt[j].frequenze = f;
    }
}

bool analizzaRangeFrequenze(const uint8_t* dati, size_t n, Frequenze& out) {
    out.n = 0;
    if (dati == nullptr || n < 2) return false;
    const size_t voci = le16(dati);
    if (voci == 0 || n < 2 + voci * 12) return false;
    for (size_t i = 0; i < voci; i++) {
        const uint8_t* v = dati + 2 + i * 12;  // dMIN, dMAX, dRES
        const uint32_t min = le32(v), max = le32(v + 4), passo = le32(v + 8);
        if (min > max) continue;
        aggiungiFrequenza(out, min, max, passo);
    }
    return out.n > 0;
}

bool analizzaRangeVolume(const uint8_t* dati, size_t n, RangeVolume& out) {
    out = RangeVolume{};
    if (dati == nullptr || n < 2) return false;
    const size_t voci = le16(dati);
    if (voci == 0 || n < 2 + voci * 6) return false;
    for (size_t i = 0; i < voci; i++) {
        const uint8_t* v = dati + 2 + i * 6;  // wMIN, wMAX, wRES
        const int16_t min = int16_t(le16(v)), max = int16_t(le16(v + 2)), passo = int16_t(le16(v + 4));
        if (i == 0) {
            out.min = min;
            out.max = max;
            out.passo = passo;
            continue;
        }
        if (min < out.min) out.min = min;
        if (max > out.max) out.max = max;
    }
    return true;
}

int16_t volumeZeroDb(const RangeVolume& r) {
    if (r.max < 0) return r.max;
    if (r.min > 0) return r.min;
    if (r.passo <= 0) return 0;
    // Il piu' alto di min, min + passo, min + 2 passo, ... che non supera 0 dB (0 - min sta in 0..32768).
    const int32_t sopra = -int32_t(r.min);
    return int16_t(int32_t(r.min) + (sopra / r.passo) * r.passo);
}

namespace {

// Va bene a quella frequenza? Tutti i requisiti di scegli() tranne wMaxPacketSize <= MAX_PACCHETTO.
bool adatta(const Alternativa& a, uint32_t hz) {
    if (!a.pcm || a.canali < 1 || a.canali > 2) return false;
    if (a.byteCampione < 2 || a.byteCampione > 4) return false;
    if (a.bit < 16 || a.bit > 8 * a.byteCampione) return false;
    if (a.intervallo != 1 || !a.frequenze.contiene(hz)) return false;
    const uint32_t serve = (hz + 999) / 1000 * a.canali * a.byteCampione;  // il pacchetto piu' lungo
    return serve <= a.maxPacchetto;
}

// (a, hz) e' meglio della scelta s? L'ordine e' quello di uac_desc.h.
bool meglio(const Alternativa& a, uint32_t hz, const Scelta& s) {
    const bool stereoA = a.canali == 2, stereoS = s.canali == 2;
    if (stereoA != stereoS) return stereoA;
    if (a.byteCampione != s.byteCampione) return a.byteCampione < s.byteCampione;
    if (hz != s.hz) return hz == HZ_48K;
    const bool asincronaA = a.sync == Sync::ASINCRONA, asincronaS = s.sync == Sync::ASINCRONA;
    if (asincronaA != asincronaS) return !asincronaA;
    return a.maxPacchetto < s.maxPacchetto;  // a pari merito resta la prima nel descrittore
}

}  // namespace

Esito scegli(const Configurazione& c, uint32_t quirk, Scelta& out) {
    out = Scelta{};
    static constexpr uint32_t FREQUENZE[] = {HZ_48K, HZ_44K1};
    bool trovata = false, grandi = false, uscita = false;
    const uint8_t ns = nStreaming(c);
    for (uint8_t i = 0; i < ns; i++) {
        const Streaming& s = c.streaming[i];
        const uint8_t na = nAlt(s);
        for (uint8_t j = 0; j < na; j++) {
            const Alternativa& a = s.alt[j];
            if ((a.endpoint & 0x80) != 0 || (a.endpoint & 0x0F) == 0) continue;  // non va verso il dispositivo
            uscita = true;
            for (uint32_t hz : FREQUENZE) {
                if (hz == HZ_44K1 && (quirk & QUIRK_SOLO_48K)) continue;
                if (!adatta(a, hz)) continue;
                if (a.maxPacchetto > MAX_PACCHETTO) {
                    grandi = true;
                    continue;
                }
                if (trovata && !meglio(a, hz, out)) continue;
                trovata = true;
                out.indiceStreaming = i;
                out.indiceAlt = j;
                out.interfaccia = s.interfaccia;
                out.alt = a.numero;
                out.terminale = a.terminale;
                out.endpoint = a.endpoint;
                out.maxPacchetto = a.maxPacchetto;
                out.canali = a.canali;
                out.byteCampione = a.byteCampione;
                out.bit = a.bit;
                out.hz = hz;
                out.sync = a.sync;
                out.frequenzaSuEndpoint = a.frequenzaSuEndpoint;
            }
        }
    }
    if (trovata) return Esito::OK;
    if (grandi) return Esito::PACCHETTI_GRANDI;
    return uscita ? Esito::FORMATO : Esito::NESSUNA_USCITA;
}

size_t testoStringa(const uint8_t* desc, size_t n, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return 0;
    out[0] = '\0';
    // bLength 0, bDescriptorType 1 (0x03), poi bString in UTF-16LE.
    if (desc == nullptr || n < 2 || desc[0] < 2 || desc[1] != DT_STRINGA) return 0;
    const size_t unita = (minimo<size_t>(n, desc[0]) - 2) / 2;
    size_t scritti = 0;
    for (size_t i = 0; i < unita; i++) {
        const uint32_t u = le16(desc + 2 + 2 * i);
        char b[3] = {0, 0, 0};
        size_t k = 1;
        if (u >= 0xD800 && u <= 0xDFFF) {
            // Fuori dal piano base: una coppia surrogata (o un surrogato da solo, che non vale) diventa '?'.
            if (u <= 0xDBFF && i + 1 < unita) {
                const uint32_t v = le16(desc + 2 + 2 * (i + 1));
                if (v >= 0xDC00 && v <= 0xDFFF) i++;
            }
            b[0] = '?';
        } else if (u < 0x20 || (u >= 0x7F && u < 0xA0)) {
            b[0] = ' ';  // caratteri di controllo (anche lo zero che qualcuno mette in fondo)
        } else if (u < 0x80) {
            b[0] = char(u);
        } else if (u < 0x800) {
            b[0] = char(0xC0 | (u >> 6));
            b[1] = char(0x80 | (u & 0x3F));
            k = 2;
        } else {
            b[0] = char(0xE0 | (u >> 12));
            b[1] = char(0x80 | ((u >> 6) & 0x3F));
            b[2] = char(0x80 | (u & 0x3F));
            k = 3;
        }
        if (scritti + k > cap - 1) break;  // un carattere non si spezza
        std::memcpy(out + scritti, b, k);
        scritti += k;
    }
    while (scritti > 0 && out[scritti - 1] == ' ') scritti--;
    out[scritti] = '\0';
    return scritti;
}

void descrivi(const Configurazione& c, void (*riga)(void* ctx, const char* testo), void* ctx) {
    if (riga == nullptr) return;
    {
        Riga r;
        r.metti("%s, bcdADC %x.%02x, AudioControl if %u, %u entita', %u streaming",
                c.versione == Versione::UAC2 ? "UAC2" : "UAC1", unsigned(c.bcdADC >> 8), unsigned(c.bcdADC & 0xFF),
                unsigned(c.interfacciaControllo), unsigned(c.nEntita), unsigned(c.nStreaming));
        if (c.troncata) r.metti(", troncata");
        riga(ctx, r.testo());
    }
    const uint8_t ns = nStreaming(c);
    for (uint8_t i = 0; i < ns; i++) {
        const Streaming& s = c.streaming[i];
        const uint8_t na = nAlt(s);
        for (uint8_t j = 0; j < na; j++) {
            const Alternativa& a = s.alt[j];
            Riga r;
            r.metti("if %u alt %u%s: %s %u ch %u/%u byte, ", unsigned(s.interfaccia), unsigned(a.numero),
                    (a.endpoint & 0x80) ? " (ingresso)" : "", a.pcm ? "PCM" : "non PCM", unsigned(a.canali),
                    unsigned(a.bit), unsigned(a.byteCampione));
            const uint8_t nf = minimo<uint8_t>(a.frequenze.n, uint8_t(MAX_FREQ));
            if (nf == 0) r.metti("%s", c.versione == Versione::UAC2 ? "frequenze dal clock" : "nessuna frequenza");
            for (uint8_t k = 0; k < nf; k++) {
                const IntervalloHz& v = a.frequenze.voci[k];
                r.metti("%s%lu", k ? ", " : "", UL(v.min));
                if (v.max != v.min) r.metti("-%lu", UL(v.max));
                if (v.max != v.min && v.passo != 0) r.metti("/%lu", UL(v.passo));
            }
            if (nf != 0) r.metti(" Hz");
            r.metti(", ep 0x%02x %u B ", unsigned(a.endpoint), unsigned(a.maxPacchetto));
            if (a.intervallo >= 1 && a.intervallo <= 16)
                r.metti("ogni %lu ms", UL(1) << (a.intervallo - 1));
            else
                r.metti("bInterval %u", unsigned(a.intervallo));
            r.metti(", %s", testoSync(a.sync));
            if (a.endpointFeedback != 0) r.metti(", feedback 0x%02x", unsigned(a.endpointFeedback));
            if (a.frequenzaSuEndpoint) r.metti(", frequenza sull'endpoint");
            riga(ctx, r.testo());
        }
    }
}

}  // namespace uac
