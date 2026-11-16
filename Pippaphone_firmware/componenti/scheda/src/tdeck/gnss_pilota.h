// T-Deck Plus: il pilota del ricevitore GPS (u-blox M10, o Quectel L76K) e i frame UBX.
// Senza ESP-IDF: seriale, orologio, lucchetto e registro arrivano da una Porta (sul T-Deck la UART1 di
// gnss.cpp; nei test sul PC un ricevitore finto, componenti/scheda/test_host).
//
// Un solo compito (quello del GPS) chiama avvio() e poi giro() di continuo: solo lui scrive sulla
// seriale. Gli altri compiti lasciano richieste (atomiche) e leggono lo stato sotto il lucchetto.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "scheda/gnss.h"

#if !CONFIG_SCHEDA_TDECK_PLUS
#error "gnss_pilota e' solo per il T-Deck Plus: sulla PCB il GPS e' tutto in gnss.cpp"
#endif

namespace scheda {
namespace gps {

class Porta {
public:
    virtual uint64_t ms() = 0;  // ms dall'avvio
    // Al massimo n byte arrivati, aspettando fino ad attesaMs se non ce n'e' nessuno (0: niente).
    virtual int leggi(uint8_t* buf, size_t n, uint32_t attesaMs) = 0;
    // Un frame intero con una scrittura sola; torna quando e' uscito tutto (o dopo un tempo massimo).
    virtual void scrivi(const uint8_t* p, size_t n) = 0;
    virtual void velocita(uint32_t baud) = 0;
    // Lucchetto del fix e dello stato condiviso (non rientrante: mai preso due volte).
    virtual void blocca() = 0;
    virtual void sblocca() = 0;
    virtual void registro(bool avviso, const char* testo) = 0;

protected:
    ~Porta() = default;
};

// Frame UBX completo (B5 62, classe, id, lunghezza, dati, checksum) in out: i byte scritti, 0 se non ci
// sta in cap.
size_t frameUbx(uint8_t cls, uint8_t id, const uint8_t* dati, uint16_t len, uint8_t* out, size_t cap);

// Frase NMEA proprietaria completa ("$" + corpo + "*" + checksum + "\r\n") in out: i byte, 0 se non ci sta.
size_t fraseNmea(const char* corpo, char* out, size_t cap);

// I frame UBX in arrivo, mescolati alle frasi NMEA. byte() dice se il carattere appartiene a un frame
// (allora non va all'analizzatore NMEA); quando un frame e' arrivato intero col checksum giusto,
// fatto() e' true fino al byte dopo.
class LettoreUbx {
public:
    static constexpr int MAX = 512;  // dati piu' lunghi: il frame si scarta
    bool byte(uint8_t c);
    bool fatto() const { return fatto_; }
    uint8_t classe() const { return cls_; }
    uint8_t id() const { return id_; }
    const uint8_t* dati() const { return dati_; }
    uint16_t lunghezza() const { return len_; }
    uint32_t scartati() const { return scartati_; }  // frame col checksum sbagliato o troppo lunghi

private:
    uint8_t stato_ = 0;  // 0 cerca B5, 1 aspetta 62, 2 classe, 3 id, 4-5 lunghezza, 6 dati, 7-8 checksum
    uint8_t cls_ = 0, id_ = 0, ca_ = 0, cb_ = 0, ckA_ = 0;
    uint16_t len_ = 0, pos_ = 0;
    bool fatto_ = false;
    uint32_t scartati_ = 0;
    uint8_t dati_[MAX];
};

class Pilota {
public:
    // Tempi (ms): quelli del progetto (gps_progetto.md, 2026-11-15).
    static constexpr uint32_t RISPONDE_MS = 3000;       // una frase negli ultimi 3 s: il ricevitore parla
    static constexpr uint32_t SILENZIO_MS = 5000;       // voluto acceso e muto da 5 s: lo si risveglia
    static constexpr uint32_t SVEGLIA_ATTESA_MS = 2000; // dopo ogni 0xFF si aspetta una frase fino a 2 s
    static constexpr int SVEGLIA_TENTATIVI = 5;
    static constexpr uint32_t RILEVA_OGNI_MS = 30000;   // ricevitore non trovato: si riprova ogni 30 s
    static constexpr uint32_t RILEVA_ATTESA_MS = 1500;  // per ogni velocita' provata
    static constexpr uint32_t TACE_PRIMA_MS = 1500;     // prima del PMREQ: se tace da 1,5 s, 0xFF e 500 ms
    static constexpr uint32_t DOPO_FF_MS = 500;
    static constexpr uint32_t PMREQ_ENTRO_MS = 3000;    // le frasi devono fermarsi entro 3 s dal PMREQ
    static constexpr uint32_t FERME_MS = 1500;          // 1,5 s senza frasi: si sono fermate
    static constexpr uint32_t SONNO_PROVA_MS = 5000;    // prova al banco: 5 s di sonno
    static constexpr uint32_t PRONTO_MS = 1000;         // comandi UBX solo con una frase nell'ultimo secondo...
    static constexpr uint32_t SVEGLIO_DA_MS = 1000;     // ...e sveglio da almeno 1 s (PMREQ, CFG-RST, MON-*)
    static constexpr uint32_t FREDDO_VALE_MS = 60000;   // una richiesta di freddo che aspetta da piu' cade
    static constexpr uint32_t VERSIONE_VALE_MS = 30000;
    static constexpr uint32_t RICERCA_OGNI_MS = 60000;  // nella storia, ogni minuto senza posizione
    static constexpr int TENTATIVI_DORMI = 4;           // standby tentati (si risveglia da solo) prima di lasciarlo acceso
    static constexpr uint32_t GIRO_MS = 50;             // attesa massima di giro() sulla seriale
    // Flag del PMREQ: backup + force. Prova al banco sul T-Deck del 15/11/2026 (MIA-M10Q, ROM SPG 5.10):
    // col solo backup (0x02) le frasi si fermavano, ma per svegliarlo servivano da 1 a 5 byte 0xFF (fino a
    // 8 s); con force (0x06) si e' svegliato sempre al primo byte, in 0,3 s, 5 volte su 5. Anche il manuale
    // d'integrazione del SAM-M10Q chiede force per lo standby software.
    static constexpr uint8_t PMREQ_NORMALE = 0x06;
    static constexpr uint8_t PMREQ_FORZA = 0x06;        // "@GPS!PROVA ... forza": ormai uguale al normale

    Pilota(Porta& porta, Fix& fix);

    // ---- richieste (qualunque compito) ----
    void vuoi(bool acceso, const char* perche);
    bool voluto() const { return voluto_.load(); }
    void freddo();
    void chiediVersione();
    void prova(int cicli, bool forza);

    // ---- compito del GPS ----
    void avvio();  // cerca il ricevitore (blocca qualche secondo)
    void giro();   // legge la seriale (al massimo GIRO_MS) e fa un passo

    // ---- letture (prendono il lucchetto) ----
    Gnss::Stato stato() const { return Gnss::Stato(stato_.load()); }
    const char* modulo() const { return modulo_.load(); }
    bool ublox() const { return ublox_.load(); }
    bool risponde() const;
    bool inStandby() const { return dorme_.load(); }
    uint32_t silenzioMs() const;
    Gnss::Diagnosi diagnosi() const;  // senza stackLibero (lo mette gnss.cpp)
    int storia(Gnss::Evento* out, int max) const;
    Gnss::Versione versione() const;
    Gnss::Prova provaStato() const;

private:
    enum Fase : uint8_t {  // prova al banco
        P_PRONTO,     // si assicura che parli (sveglia se serve)
        P_PMREQ,      // manda il PMREQ
        P_FERMA,      // aspetta che le frasi si fermino
        P_SONNO,      // 5 s di sonno
        P_SVEGLIA,    // 0xFF e attesa della prima frase
    };

    // passi della macchina a stati
    void passo(uint64_t now);
    void entra(Gnss::Stato s, uint64_t now);
    void rileva();
    void trovato(uint8_t tipo, uint32_t baud, uint64_t t0);
    bool nmeaEntro(uint32_t ms);
    void vaiSpento(uint64_t now);
    void entraSveglia(uint64_t now);
    void passoSveglia(uint64_t now);
    void entraAddormenta(uint64_t now);
    void passoAddormenta(uint64_t now);
    void entraStandby(uint64_t now);
    void richieste(uint64_t now);
    void iniziaProva(uint32_t richiesta, uint64_t now);
    void passoProva(uint64_t now);
    void chiudiCiclo(uint64_t now);
    void fineProva(uint64_t now, const char* errore);
    void sessione(uint64_t now);
    void nuovaSessione(uint64_t t);

    // azioni
    void mandaFF();
    void mandaPmreq(uint8_t flag);
    void mandaUbx(uint8_t cls, uint8_t id, const uint8_t* dati, uint16_t len);
    void datiARiposo();           // standby: niente posizione valida ne' satelliti (sotto il lucchetto)
    void ricevutoUbx();           // un frame UBX arrivato intero (sotto il lucchetto)
    void evento(Gnss::Ev tipo, int32_t a = 0, int32_t b = 0, int32_t c = 0, const char* perche = nullptr);
    // Una frase valida entro entroMs (un istante dopo now conta come adesso).
    bool parla(uint64_t now, uint32_t entroMs) const { return ultima_ && (ultima_ >= now || now - ultima_ <= entroMs); }
    // Sveglio e pronto per un comando UBX: parla, ed e' sveglio da almeno 1 s (da qualunque sveglia, anche
    // quelle della prova al banco).
    bool pronto(uint64_t now) const { return parla(now, PRONTO_MS) && now >= svegliatoDa_ + SVEGLIO_DA_MS; }

    Porta& porta_;
    Fix& fix_;
    NmeaParser nmea_;
    LettoreUbx ubx_;

    // richieste (scritte da qualunque compito)
    std::atomic<bool> voluto_{false};
    std::atomic<const char*> perche_{nullptr};
    std::atomic<uint32_t> volutoGen_{0};      // cresce a ogni vuoi(): il compito vede ogni cambio
    std::atomic<uint64_t> accesoChiesto_{0};  // ms dell'ultimo passaggio a voluto acceso
    std::atomic<uint32_t> freddoChiesto_{0};  // ms (32 bit, | 1) della richiesta, 0 = nessuna
    std::atomic<uint32_t> versioneChiesta_{0};
    std::atomic<uint32_t> provaChiesta_{0};   // cicli | forza << 8 | 1 << 16, 0 = nessuna

    // stato pubblico (letture senza lucchetto)
    std::atomic<uint8_t> stato_{uint8_t(Gnss::Stato::RILEVA)};
    std::atomic<const char*> modulo_{"?"};
    std::atomic<bool> ublox_{false};
    std::atomic<bool> dorme_{false};

    // solo il compito del GPS
    enum Modulo : uint8_t { M_NESSUNO, M_UBLOX, M_L76K };
    Modulo tipo_ = M_NESSUNO;
    bool rilevato_ = false;        // un ricevitore trovato almeno una volta
    uint64_t ultima_ = 0;          // ultima frase valida (copia di fix_.frasiAtMs)
    uint64_t svegliatoDa_ = 0;     // prima frase dopo almeno 1,5 s di silenzio, o nuova sessione: sveglio da li'
    uint64_t statoDa_ = 0;         // quando si e' entrati nello stato
    bool volutoVisto_ = false;
    uint32_t genVista_ = 0;
    uint64_t volutoDa_ = 0;        // quando il compito ha visto l'ultimo power(true)
    uint64_t rilevaDopo_ = 0;      // MUTO: prossimo tentativo
    // SVEGLIA
    int tentativi_ = 0;
    uint64_t svegliaDa_ = 0, tentativoDa_ = 0;
    // ADDORMENTA
    uint8_t faseDormi_ = 0;        // 0 aspetta che sia sveglio da 1 s, 1 0xFF mandato (attesa 500 ms), 2 PMREQ mandato
    uint64_t dormiDa_ = 0, pmreqDa_ = 0;
    bool parlavaPrima_ = false;    // al PMREQ il ricevitore parlava (frase entro 1,5 s)
    bool prontoPrima_ = false;     // ...ed era sveglio da almeno 1 s: solo allora un PMREQ perso e' "ignorato"
    int riproveDormi_ = 0;         // standby tentati da quando lo si vuole spento
    bool rinuncia_ = false;        // si risveglia da solo: resta acceso fino al prossimo power(true)
    uint64_t standbyDa_ = 0;
    int8_t pmreq_ = 0;             // 1 rispettato, -1 ignorato, 0 mai (flag normali)
    // sessione: dalla prima frase dopo una sveglia o un'accensione
    uint64_t sessioneDa_ = 0;
    bool primaOra_ = false, primoFix_ = false;
    uint64_t ricercaDa_ = 0;
    // prova al banco
    Fase fase_ = P_PRONTO;
    int provaCicli_ = 0;
    bool provaForza_ = false;
    Gnss::CicloProva ciclo_;       // ciclo in corso
    uint64_t faseDa_ = 0, provaPmreqDa_ = 0, provaSvegliaDa_ = 0, provaTentativoDa_ = 0;
    uint32_t frasiOkSonno_ = 0;
    int provaTentativi_ = 0;
    bool provaPronta_ = false;     // P_PRONTO: il primo 0xFF e' gia' partito

    // condivisi (sotto il lucchetto)
    Gnss::Diagnosi diag_;
    Gnss::Evento storia_[Gnss::STORIA];
    int storiaInizio_ = 0, storiaN_ = 0;
    Gnss::Versione ver_;
    Gnss::Prova prova_;
};

}  // namespace gps
}  // namespace scheda
