// PippaNet: alfabeto ridotto a 7 bit (come gli SMS), con le lettere accentate italiane.
//
// 160 caratteri stanno in 140 byte. Il codice 0 e' riempimento, non un carattere.
// 0x20..0x7E sono i caratteri ASCII stampabili, 0x0A e' "a capo", gli altri codici
// (0x01..0x09, 0x0B..0x1F, 0x7F) sono i 31 caratteri extra della tabella in charset.cpp.
#pragma once

#include <string>

#include "pippanet/types.h"

namespace pn {

// Converte un testo UTF-8 nei codici a 7 bit. I caratteri non previsti diventano '?'.
// Restituisce il numero di codici scritti (al massimo maxChars).
size_t utf8ToCodes(const std::string& utf8, uint8_t* codes, size_t maxChars);
// Converte i codici a 7 bit in UTF-8.
std::string codesToUtf8(const uint8_t* codes, size_t n);

// Impacchetta n codici da 7 bit in ceil(n*7/8) byte. Restituisce i byte scritti.
size_t pack7(const uint8_t* codes, size_t n, uint8_t* out, size_t outCap);
// Spacchetta; ignora il riempimento finale. Restituisce il numero di codici.
size_t unpack7(const uint8_t* in, size_t len, uint8_t* codes, size_t maxCodes);

// Scorciatoie testo <-> byte impacchettati.
size_t encodeText(const std::string& utf8, uint8_t* out, size_t outCap, size_t maxChars = MAX_TEXT_CHARS);
std::string decodeText(const uint8_t* in, size_t len);

// true se il carattere Unicode e' rappresentabile.
bool isEncodable(uint32_t codepoint);
// Numero di caratteri (codici) che occuperebbe il testo.
size_t textLength(const std::string& utf8);

}  // namespace pn
