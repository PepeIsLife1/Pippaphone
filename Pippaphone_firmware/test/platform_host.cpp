// Numeri casuali per i test sul PC: deterministici, cosi' ogni esecuzione e' uguale.
#include <random>

#include "pippanet/crypto.h"
#include "testing.h"

static std::mt19937 g_rng(12345);

void seedRandom(uint32_t seed) { g_rng.seed(seed); }

namespace pn {
void randomBytes(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) buf[i] = uint8_t(g_rng());
}
}  // namespace pn
