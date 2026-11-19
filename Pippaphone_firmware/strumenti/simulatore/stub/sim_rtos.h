// Simulatore: il minimo di FreeRTOS ed ESP-IDF che serve per compilare l'interfaccia sul PC.
// Le code funzionano davvero (un solo filo); i mutex non fanno nulla; vTaskDelay fa avanzare il
// tempo simulato e chiama il copione del simulatore.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void* TaskHandle_t;
typedef struct SimQueue* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* TimerHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portTICK_PERIOD_MS 1
#define configTICK_RATE_HZ 1000
#define portYIELD_FROM_ISR(x) (void)(x)
#define IRAM_ATTR

// Code (copiano gli elementi come FreeRTOS).
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
// Come FreeRTOS: guarda il primo elemento senza toglierlo; se la coda e' vuota il tempo passa come con
// vTaskDelay (l'interfaccia del T-Deck aspetta cosi' i tasti).
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait);
#define xQueueSendToBack xQueueSend

// Semafori: un solo filo, non servono.
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<SemaphoreHandle_t>(1); }
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return reinterpret_cast<SemaphoreHandle_t>(1); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return reinterpret_cast<SemaphoreHandle_t>(1); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return pdTRUE; }

// Compiti: il compito dell'interfaccia gira subito (e non torna); gli altri non partono.
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* handle, BaseType_t core);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount();
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdTRUE; }
inline void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t*) {}
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
inline void vTaskDelete(TaskHandle_t) {}

// Timer software: non scattano nel simulatore.
inline TimerHandle_t xTimerCreate(const char*, TickType_t, UBaseType_t, void*, TimerCallbackFunction_t) {
    return reinterpret_cast<TimerHandle_t>(1);
}
inline BaseType_t xTimerStop(TimerHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xTimerChangePeriod(TimerHandle_t, TickType_t, TickType_t) { return pdTRUE; }

// esp_timer, log, memoria, sistema.
int64_t esp_timer_get_time();
#define ESP_LOGE(tag, fmt, ...) std::fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) std::fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ((void)(tag))
#define ESP_LOGD(tag, fmt, ...) ((void)(tag))
#define MALLOC_CAP_INTERNAL (1 << 0)
#define MALLOC_CAP_DMA (1 << 1)
#define MALLOC_CAP_SPIRAM (1 << 2)
#define MALLOC_CAP_DEFAULT (1 << 3)
#define MALLOC_CAP_8BIT (1 << 4)
inline void* heap_caps_malloc(size_t n, uint32_t) { return std::malloc(n); }
inline void* heap_caps_calloc(size_t n, size_t s, uint32_t) { return std::calloc(n, s); }
inline void heap_caps_free(void* p) { std::free(p); }
inline size_t heap_caps_get_free_size(uint32_t caps) { return caps & MALLOC_CAP_SPIRAM ? 7900u * 1024 : 140u * 1024; }
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef enum { ESP_MAC_WIFI_STA = 0 } esp_mac_type_t;
inline esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
    const uint8_t m[6] = {0xF4, 0x12, 0xFA, 0x5C, 0x3B, 0x60};
    for (int i = 0; i < 6; i++) mac[i] = m[i];
    return ESP_OK;
}
[[noreturn]] void esp_restart();
