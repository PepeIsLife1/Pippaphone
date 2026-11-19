// Simulatore: code, tempo, compiti e schermo in memoria.
//
// Sul PC il tempo e' simulato: vTaskDelay lo fa avanzare e manda avanti il copione. Nel browser
// (SIM_WEB) il tempo e' quello vero e vTaskDelay cede il controllo alla pagina (web/sim_web.cpp).
#include <cstring>
#include <deque>
#include <vector>

#include "sim.h"
#include "sim_rtos.h"
#ifdef SIM_WEB
#include "web/web.h"
#endif

struct SimQueue {
    size_t itemSize;
    size_t len;
    std::deque<std::vector<uint8_t>> items;
};

#ifndef SIM_WEB
namespace {
int64_t g_nowUs = 0;
bool g_inRun = false;
}  // namespace
#endif

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

BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait) {
    if ((!q || q->items.empty()) && wait) vTaskDelay(wait == portMAX_DELAY ? 1000 : wait);
    if (!q || q->items.empty()) return pdFALSE;
    std::memcpy(item, q->items.front().data(), q->itemSize);
    return pdTRUE;
}

// Elementi in coda senza toglierli (il simulatore nel browser mostra i tasti che arrivano).
size_t simQueueSize(QueueHandle_t q) { return q ? q->items.size() : 0; }

bool simQueuePeek(QueueHandle_t q, size_t i, void* item) {
    if (!q || i >= q->items.size()) return false;
    std::memcpy(item, q->items[i].data(), q->itemSize);
    return true;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t, void* arg, UBaseType_t,
                                   TaskHandle_t* handle, BaseType_t) {
    if (handle) *handle = nullptr;
    // Solo il compito dell'interfaccia: gira qui e il simulatore esce dal copione.
    if (name && std::strcmp(name, "ui") == 0) fn(arg);
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
#ifdef SIM_WEB
    web::sleep(ticks ? ticks : 1);
#else
    g_nowUs += int64_t(ticks ? ticks : 1) * 1000;
    if (!g_inRun) {
        g_inRun = true;
        sim::run();
        g_inRun = false;
    }
#endif
}

TickType_t xTaskGetTickCount() { return TickType_t(esp_timer_get_time() / 1000); }

int64_t esp_timer_get_time() {
#ifdef SIM_WEB
    return web::nowUs();
#else
    return g_nowUs;
#endif
}

void esp_restart() {
#ifdef SIM_WEB
    web::restart();
#else
    std::fprintf(stderr, "riavvio chiesto dall'interfaccia\n");
    std::exit(0);
#endif
}

namespace sim {

int64_t nowMs() { return esp_timer_get_time() / 1000; }

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

namespace {
uint64_t g_drawnPixels = 0;
}  // namespace

uint64_t drawnPixels() { return g_drawnPixels; }

void draw(int x1, int y1, int x2, int y2, const void* px) {
    const uint16_t* p = static_cast<const uint16_t*>(px);
    const int w = x2 - x1;
    g_drawnPixels += uint64_t(w) * uint64_t(y2 - y1);
    for (int y = y1; y < y2; y++)
        for (int x = x1; x < x2; x++)
            if (x >= 0 && y >= 0 && x < g_w && y < g_h) g_fb[size_t(y) * g_w + x] = p[size_t(y - y1) * w + (x - x1)];
#ifdef SIM_WEB
    web::frameChanged();
#endif
}

}  // namespace sim
