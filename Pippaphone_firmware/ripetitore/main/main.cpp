// Pippaphone: firmware del ripetitore.
//
// Ripete i pacchetti della rete PippaNet (vedi componenti/pippanet/repeater.h). Si configura
// dalla console USB: numero del ripetitore, potenza, stato.
#include <cstdio>
#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pippa_radio/radio_link.h"
#include "pippanet/repeater.h"
#include "sdkconfig.h"

#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
#include "scheda/scheda.h"
#endif

namespace {

const char* TAG = "ripetitore";

// ---------------------------------------------------------------- scheda

#if CONFIG_RIPETITORE_SCHEDA_E22
// Scheda ripetitore proposta (HARDWARE.md): ESP32-S3-WROOM-1 + Ebyte E22-900M30S.
constexpr int PIN_SCK = 12, PIN_MOSI = 11, PIN_MISO = 13, PIN_NSS = 10, PIN_BUSY = 9, PIN_DIO1 = 8, PIN_NRST = 7,
              PIN_TXEN = 6, PIN_RXEN = 5, PIN_LED = 2;
#endif

void ledSet(bool on) {
#if CONFIG_RIPETITORE_SCHEDA_E22
    gpio_set_level(gpio_num_t(PIN_LED), on);
#else
    scheda::expander().rgb(0, on ? 40 : 0, 0);
#endif
}

pippa::RadioConfig radioConfig() {
    pippa::RadioConfig c;
#if CONFIG_RIPETITORE_SCHEDA_E22
    c.sck = PIN_SCK;
    c.mosi = PIN_MOSI;
    c.miso = PIN_MISO;
    c.nss = PIN_NSS;
    c.busy = PIN_BUSY;
    c.dio1 = PIN_DIO1;
    c.reset = PIN_NRST;
    c.txEn = PIN_TXEN;
    c.rxEn = PIN_RXEN;
    c.dio2AsRfSwitch = false;
    c.tcxoVoltage = 1.8f;
#else
    c.sck = scheda::PIN_SPI_SCK;
    c.mosi = scheda::PIN_SPI_MOSI;
    c.miso = scheda::PIN_SPI_MISO;
    c.nss = scheda::PIN_LORA_NSS;
    c.busy = scheda::PIN_LORA_BUSY;
    c.dio1 = scheda::PIN_LORA_DIO1;
    c.resetFn = [](bool level) { scheda::expander().set(scheda::Exp::LORA_RST_N, level); };
    c.dio2AsRfSwitch = true;
    c.tcxoVoltage = 1.8f;
#endif
    c.maxPowerDbm = CONFIG_RIPETITORE_POTENZA_MAX;
    return c;
}

uint8_t batteryPercent() {
#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
    return scheda::batteria().percent();
#else
    return 255;
#endif
}

// ---------------------------------------------------------------- memoria

nvs_handle_t g_nvs = 0;

uint16_t loadAddress() {
    uint16_t a = 0;
    if (nvs_get_u16(g_nvs, "id", &a) == ESP_OK && pn::isRepeaterAddr(a)) return a;
    // Primo avvio: numero ricavato dal MAC (0xF001..0xFFFE), poi si cambia da console.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    a = uint16_t(0xF000 | ((mac[4] << 8 | mac[5]) & 0x0FFF));
    if (a == 0xF000) a = 0xF001;
    if (a == pn::ADDR_BROADCAST) a = 0xFFFE;
    return a;
}

// ---------------------------------------------------------------- stato

SemaphoreHandle_t g_lock;
pn::Repeater* g_node = nullptr;
pippa::RadioLink g_radio;

void printInfo() {
    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
    const pn::Millis now = pippa::RadioLink::nowMs();
    printf("Ripetitore R%u (0x%04X)\n", unsigned(g_node->address() - 0xF000), g_node->address());
    printf("  radio: %s, potenza %d dBm\n", g_radio.ok() ? "ok" : "NON RISPONDE", g_radio.power());
    printf("  canale: %s\n", g_node->activeChannel(now) == pn::Channel::VOICE ? "voce" : "messaggi");
    printf("  ricevuti %lu (scartati %lu, errori radio %lu), trasmessi %lu, ripetuti %lu, soppressi %lu, "
           "recuperati %lu\n",
           (unsigned long)g_node->rxCount, (unsigned long)g_node->rxDropped, (unsigned long)g_radio.rxErrors(),
           (unsigned long)g_node->txCount, (unsigned long)g_node->relayed, (unsigned long)g_node->suppressed,
           (unsigned long)g_node->rescued);
    printf("  tempo in aria totale: %llu s\n", (unsigned long long)(g_node->txAirtimeMs / 1000));
    printf("  ultimo segnale: %d dBm, SNR %.1f dB\n", g_radio.lastRssi(), double(g_radio.lastSnr()));
    if (g_node->timeValid()) printf("  ora: %lu (Unix)\n", (unsigned long)g_node->unixTime(now));
    const uint8_t b = batteryPercent();
    if (b <= 100) printf("  batteria: %u%%\n", b);
    xSemaphoreGiveRecursive(g_lock);
}

void printRoutes() {
    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
    const pn::Millis now = pippa::RadioLink::nowMs();
    printf("destinazione  via      salti  SNR    eta'\n");
    for (const auto& r : g_node->routes()) {
        if (now - r.second.seen > g_node->routeTtlMs) continue;
        printf("%-12u  %-7u  %-5u  %-5.1f  %lus\n", r.first, r.second.next, r.second.hops, double(r.second.snr),
               (unsigned long)((now - r.second.seen) / 1000));
    }
    xSemaphoreGiveRecursive(g_lock);
}

void console() {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, nullptr, _IONBF, 0);
    printf("\nComandi: info, strade, numero <1..4094>, potenza <dBm>, riavvia\n");
    char line[80];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        char cmd[16] = {};
        int arg = 0;
        const int n = sscanf(line, "%15s %d", cmd, &arg);
        if (n < 1) continue;
        if (!strcmp(cmd, "info")) {
            printInfo();
        } else if (!strcmp(cmd, "strade")) {
            printRoutes();
        } else if (!strcmp(cmd, "numero") && n == 2 && arg >= 1 && arg <= 4094) {
            const uint16_t a = uint16_t(0xF000 + arg);
            nvs_set_u16(g_nvs, "id", a);
            nvs_commit(g_nvs);
            xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
            g_node->setAddress(a);
            xSemaphoreGiveRecursive(g_lock);
            printf("ora sono R%d\n", arg);
        } else if (!strcmp(cmd, "potenza") && n == 2) {
            g_radio.setPower(int8_t(arg));
            nvs_set_i8(g_nvs, "potenza", g_radio.power());
            nvs_commit(g_nvs);
            printf("potenza %d dBm\n", g_radio.power());
        } else if (!strcmp(cmd, "riavvia")) {
            esp_restart();
        } else {
            printf("comando sconosciuto\n");
        }
    }
}

}  // namespace

extern "C" void app_main() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open("ripetitore", NVS_READWRITE, &g_nvs);

#if CONFIG_RIPETITORE_SCHEDA_E22
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_LED;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
#else
    scheda::init(false);
    // Sulla scheda del telefono: niente GNSS ne' amplificatore, solo la radio.
    scheda::expander().set(scheda::Exp::GNSS_EN, false);
#endif

    g_lock = xSemaphoreCreateRecursiveMutex();
    g_node = new pn::Repeater(loadAddress());
    uint32_t seq = 1;
    nvs_get_u32(g_nvs, "seq", &seq);
    g_node->setSeq(seq);
    g_node->onSeqReserve = [](uint32_t next) {
        nvs_set_u32(g_nvs, "seq", next);
        nvs_commit(g_nvs);
    };
    g_node->battery = batteryPercent();

    if (!g_radio.begin(radioConfig(), g_node, g_lock)) {
        ESP_LOGE(TAG, "radio non disponibile: controlla l'SX1262");
    } else {
        int8_t p = CONFIG_RIPETITORE_POTENZA_MAX;
        nvs_get_i8(g_nvs, "potenza", &p);
        g_radio.setPower(p);
        g_radio.start(0, 10);
    }
    ESP_LOGI(TAG, "ripetitore R%u pronto", unsigned(g_node->address() - 0xF000));

    // Lampeggio di "vivo" e aggiornamento della batteria.
    xTaskCreate(
        [](void*) {
            for (int i = 0;; i++) {
                ledSet(true);
                vTaskDelay(pdMS_TO_TICKS(g_radio.ok() ? 40 : 400));
                ledSet(false);
                vTaskDelay(pdMS_TO_TICKS(g_radio.ok() ? 4960 : 600));
                if (i % 12 == 0) {
                    const uint8_t b = batteryPercent();
                    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
                    g_node->battery = b;
                    xSemaphoreGiveRecursive(g_lock);
                }
            }
        },
        "led", 3072, nullptr, 2, nullptr);

    console();
}
