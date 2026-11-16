// Aiuti per i test di uac_desc sul PC: memoria con pagine proibite intorno (per accorgersi di ogni lettura
// fuori dal buffer), un costruttore di descrittori finti per i casi che nessun dispositivo vero ha, e il
// generatore casuale (seminato da test/main.cpp con seedRandom, come gli altri test).
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

#include "usb_audio/uac_desc.h"

namespace aiuti {

// Il generatore casuale dei test (deterministico: PN_SEED o 12345, rimesso a ogni test da test/main.cpp).
std::mt19937& caso();

// Memoria con una pagina senza accesso prima e una dopo. metti() copia n byte o in fondo (l'ultimo byte e'
// subito prima della pagina proibita) o in testa (il primo e' subito dopo): leggere anche un solo byte fuori
// fa cadere il programma all'istante. Serve al posto di AddressSanitizer, che il g++ di MSYS2 non ha.
class Protetta {
public:
    explicit Protetta(size_t massimo);
    ~Protetta();
    Protetta(const Protetta&) = delete;
    Protetta& operator=(const Protetta&) = delete;
    uint8_t* metti(const uint8_t* dati, size_t n, bool inFondo);
    uint8_t* metti(const std::vector<uint8_t>& v, bool inFondo) { return metti(v.data(), v.size(), inFondo); }
    // n byte scrivibili subito prima della pagina proibita (per controllare chi scrive, come testoStringa).
    uint8_t* spazioInFondo(size_t n);
    size_t massimo() const { return utile_; }

private:
    uint8_t* base_ = nullptr;
    size_t pagina_ = 0;
    size_t utile_ = 0;  // byte fra le due pagine proibite
};

// Costruisce un descrittore di configurazione a pezzi. fine() mette wTotalLength (e bNumInterfaces, contando
// le interfacce diverse) e restituisce i byte.
class Costruttore {
public:
    Costruttore();
    Costruttore& d(std::initializer_list<uint8_t> byte);  // un descrittore intero, bLength compreso
    Costruttore& d(const std::vector<uint8_t>& byte);
    Costruttore& interfaccia(uint8_t numero, uint8_t alt, uint8_t nEndpoint, uint8_t classe, uint8_t sottoclasse,
                             uint8_t protocollo);
    Costruttore& iad(uint8_t primo, uint8_t quante, uint8_t classe = 0x01, uint8_t protocollo = 0x20);
    // UAC1
    Costruttore& header1(std::initializer_list<uint8_t> interfacce);
    Costruttore& it1(uint8_t id, uint16_t tipo, uint8_t canali = 2);
    Costruttore& ot1(uint8_t id, uint16_t tipo, uint8_t sorgente);
    Costruttore& fu1(uint8_t id, uint8_t sorgente, std::initializer_list<uint8_t> controlli);
    Costruttore& general1(uint8_t terminale, uint16_t formato = 0x0001);
    Costruttore& formato1(uint8_t canali, uint8_t byte, uint8_t bit, std::initializer_list<uint32_t> hz);
    Costruttore& ep1(uint8_t indirizzo, uint8_t attributi, uint16_t mps, uint8_t intervallo = 1, uint8_t refresh = 0,
                     uint8_t sincro = 0);
    Costruttore& csep1(uint8_t attributi);
    // UAC2
    Costruttore& header2();
    Costruttore& it2(uint8_t id, uint16_t tipo, uint8_t clock, uint8_t canali = 2);
    Costruttore& ot2(uint8_t id, uint16_t tipo, uint8_t sorgente, uint8_t clock);
    Costruttore& fu2(uint8_t id, uint8_t sorgente, std::initializer_list<uint32_t> controlli);
    Costruttore& clock2(uint8_t id, uint8_t attributi = 0x03, uint8_t controlli = 0x07);
    Costruttore& selettore2(uint8_t id, std::initializer_list<uint8_t> ingressi);
    Costruttore& moltiplicatore2(uint8_t id, uint8_t sorgente);
    Costruttore& general2(uint8_t terminale, uint8_t canali, uint32_t formati = 1, uint8_t tipoFormato = 1);
    Costruttore& formato2(uint8_t byte, uint8_t bit);
    Costruttore& ep(uint8_t indirizzo, uint8_t attributi, uint16_t mps, uint8_t intervallo = 1);
    Costruttore& csep2();
    std::vector<uint8_t> fine() const;

private:
    std::vector<uint8_t> b_;
};

// Risposta a GET RANGE della frequenza: n x {min, max, passo}.
std::vector<uint8_t> rangeHz(std::initializer_list<uint32_t> triple);
// Risposta a GET RANGE del volume: n x {min, max, passo}.
std::vector<uint8_t> rangeVolume(std::initializer_list<int16_t> triple);
// Descrittore stringa da una sequenza di unita' UTF-16.
std::vector<uint8_t> stringa(std::initializer_list<uint16_t> unita);
std::vector<uint8_t> stringa(const char* ascii);

// Le righe di descrivi(), una per elemento.
std::vector<std::string> righe(const uac::Configurazione& c);

// Come fa uac_host::apri() per UAC2: per ogni terminale delle alternative in uscita segue il clock (pin come
// sorgenteClock) e gli da' le frequenze della risposta range (se vuota: solo 48000, come uac_host quando GET
// RANGE non va). Poi scegli(). Restituisce l'esito di scegli().
uac::Esito simulaApri(uac::Configurazione& c, uint32_t quirk, const std::vector<uint8_t>& range,
                      uac::Scelta& scelta, uac::PinSelettore pin = nullptr, void* ctx = nullptr);

// La catena dei bLength di v arriva esattamente in fondo, e wTotalLength e' v.size()?
bool catenaEsatta(const uint8_t* v, size_t n);

// Le parti valide (fino ai contatori) di due Configurazione sono uguali?
bool stessaConfigurazione(const uac::Configurazione& a, const uac::Configurazione& b);

// Controlla gli invarianti di una Configurazione uscita da analizza(): contatori nei limiti, alternative
// buone. Restituisce "" se va tutto bene, se no cosa non va.
std::string controllaConfigurazione(const uac::Configurazione& c, uac::Esito e);

}  // namespace aiuti
