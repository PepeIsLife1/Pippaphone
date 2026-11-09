// Simulatore: code, tempo, compiti e schermo in memoria.
#include <cstring>
#include <deque>
#include <vector>

#include "sim.h"
#include "sim_rtos.h"

struct SimQueue {
    size_t itemSize;
    size_t len;
    std::deque<std::vector<uint8_t>> items;
};

namespace {
int64_t g_nowUs = 0;
bool g_inRun = false;
}  // namespace

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize) { return new SimQueue{itemSize, len, {}}; }

BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
    if (!q || q->items.size() >= q->len) return pdFALSE;
    const uint8_t* p = static_cast<const uint8_t*>(item);
    q->items.emplace_back(p, p + q->itemSize);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t) {
    if (!q || q->items.empty()) return pdFALSE;
    std::memcpy(item, q->items.front().data(), q->itemSize);
    q->items.pop_front();
    return pdTRUE;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t, void* arg, UBaseType_t,
                                   TaskHandle_t* handle, BaseType_t) {
    if (handle) *handle = nullptr;
    // Solo il compito dell'interfaccia: gira qui e il simulatore esce dal copione.
    if (name && std::strcmp(name, "ui") == 0) fn(arg);
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    g_nowUs += int64_t(ticks ? ticks : 1) * 1000;
    if (!g_inRun) {
        g_inRun = true;
        sim::run();
        g_inRun = false;
    }
}

TickType_t xTaskGetTickCount() { return TickType_t(g_nowUs / 1000); }

int64_t esp_timer_get_time() { return g_nowUs; }

void esp_restart() {
    std::fprintf(stderr, "riavvio chiesto dall'interfaccia\n");
    std::exit(0);
}

namespace sim {

int64_t nowMs() { return g_nowUs / 1000; }

// ---------------------------------------------------------------- schermo

namespace {
std::vector<uint16_t> g_fb;
int g_w = 0, g_h = 0;
}  // namespace

void setSize(int w, int h) {
    g_w = w;
    g_h = h;
    g_fb.assign(size_t(w) * size_t(h), 0);
}

int width() { return g_w; }
int height() { return g_h; }
const uint16_t* pixels() { return g_fb.data(); }

void draw(int x1, int y1, int x2, int y2, const void* px) {
    const uint16_t* p = static_cast<const uint16_t*>(px);
    const int w = x2 - x1;
    for (int y = y1; y < y2; y++)
        for (int x = x1; x < x2; x++)
            if (x >= 0 && y >= 0 && x < g_w && y < g_h) g_fb[size_t(y) * g_w + x] = p[size_t(y - y1) * w + (x - x1)];
}

}  // namespace sim
