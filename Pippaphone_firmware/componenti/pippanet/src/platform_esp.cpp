// Numeri casuali dall'hardware dell'ESP32 (radio accesa = entropia vera).
#include "esp_random.h"
#include "pippanet/crypto.h"

namespace pn {
void randomBytes(uint8_t* buf, size_t len) { esp_fill_random(buf, len); }
}  // namespace pn
