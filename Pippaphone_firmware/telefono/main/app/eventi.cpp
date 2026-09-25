#include "app/eventi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace app {

namespace {
QueueHandle_t g_q = nullptr;
}

void eventsInit() {
    if (!g_q) g_q = xQueueCreate(32, sizeof(Event));
}

void post(const Event& e) {
    if (g_q) xQueueSend(g_q, &e, 0);
}

bool poll(Event& e) { return g_q && xQueueReceive(g_q, &e, 0) == pdTRUE; }

}  // namespace app
