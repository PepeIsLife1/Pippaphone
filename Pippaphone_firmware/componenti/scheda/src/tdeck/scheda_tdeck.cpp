// LilyGO T-Deck Plus: accensione delle periferiche, bus SPI comune, compito della tastiera,
// notifiche con la luce della tastiera e spegnimento (sonno profondo).
//
// Differenze dalla PCB Pippaphone:
//  - niente autoritenuta: il telefono si accende e si spegne con l'interruttore laterale.
//    "Spegni" (e la batteria scarica) spegne le periferiche (POWERON a 0) e mette l'ESP32-S3 in
//    sonno profondo: si riaccende premendo il trackball o per una sveglia (l'ora resta giusta),
//    oppure con l'interruttore o RST (l'ora si perde);
//  - niente vibrazione: vibrate() non fa nulla;
//  - niente LED RGB: ledBlink() fa lampeggiare la luce della tastiera, ma solo quando e' spenta
//    (a schermo spento), cosi' non disturba mentre si scrive.
#include <algorithm>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_private/esp_sleep_internal.h"  // modo RTC_USE_RC_FAST (vedi powerOff)
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "hal/usb_serial_jtag_ll.h"
#include "bus_spi.h"
#include "scheda/scheda.h"

namespace scheda {

namespace {
const char* TAG = "scheda";
bool g_spi = false;
// Turno sul bus SPI comune (binario e non mutex: il display lo restituisce dall'interruzione).
SemaphoreHandle_t g_bus = nullptr;
bool g_full = false;
TaskHandle_t g_inputTask = nullptr;
constexpr TickType_t INPUT_PERIOD = pdMS_TO_TICKS(12);

uint8_t g_kbLevel = 0;  // luce della tastiera voluta dall'interfaccia
uint32_t g_wakeAfterS = 0;  // "Spegni": risveglio per la sveglia (0 = nessuno)
struct Blink {
    uint8_t level;
    uint16_t on, off;
    uint8_t left;
    bool infinite;
    bool lit;
};
Blink g_blink{};
bool g_blinking = false;
TimerHandle_t g_blinkTimer = nullptr;
uint32_t g_blinkId = 0;  // lampeggio in corso (ledBlink), 0 = nessuno

// Luce della tastiera: quella voluta, oppure il lampeggio se la luce e' spenta.
void applyKeyboardLight() {
    if (!g_full) return;
    if (g_blinking && g_kbLevel == 0) tastiera().light(g_blink.lit ? g_blink.level : 0);
    else tastiera().light(g_kbLevel);
}

void blinkCb(TimerHandle_t) {
    if (!g_blinking) return;
    if (g_blink.lit) {
        g_blink.lit = false;
        if (!g_blink.infinite && --g_blink.left == 0) {
            g_blinking = false;
            applyKeyboardLight();
            return;
        }
        xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(g_blink.off), 0);
    } else {
        g_blink.lit = true;
        xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(g_blink.on), 0);
    }
    applyKeyboardLight();
}

void inputTask(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        tastiera().service();
        vTaskDelayUntil(&last, INPUT_PERIOD);
    }
}

void outputHigh(gpio_num_t pin) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(pin, 1);
}

// La PHY USB interna alla USB-Serial-JTAG (la console del computer), come fa il suo driver quando si installa.
// Con le cuffie USB-C accese la PHY e' del controller USB-OTG (app/cuffie.cpp) e un riavvio software, un crash o
// il sonno profondo non la rimettono a posto (il registro di RTC_CNTL resta): senza queste due righe il
// computer non vedrebbe piu' il telefono fino a RST.
void phyAlComputer() {
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
}

// Touch GT911: c'e' ma il telefono non lo usa. Lasciato com'e' continua a scandire lo schermo (qualche mA
// anche senza tocchi); il comando 0x05 nel registro 0x8040 lo mette a dormire. Si risveglierebbe solo con un
// impulso alto sul suo INT (GPIO 16): resta un ingresso col pull-down.
void touchADormire() {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_TOUCH_INT;
    io.mode = GPIO_MODE_INPUT;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&io);
    static I2cDev touch;
    for (const uint8_t addr : {ADDR_GT911_A, ADDR_GT911_B}) {
        if (!touch.present() && !touch.attach(i2cBus(), addr)) continue;
        const uint8_t sonno[3] = {0x80, 0x40, 0x05};  // registro 0x8040 (big endian) = 5: sonno
        const bool ok = touch.write(sonno, sizeof(sonno));
        ESP_LOGI(TAG, "touch GT911 (0x%02x): %s", addr, ok ? "a dormire" : "non prende il comando di sonno");
        return;
    }
    ESP_LOGI(TAG, "touch GT911 non trovato");
}

#if CONFIG_PM_ENABLE
// Motivi per tenere il chip sveglio (tieni()): un blocco del risparmio di ESP-IDF ciascuno, e se e' preso.
// Ogni motivo lo cambia un compito solo (CUFFIE: il controllo delle cuffie).
struct Blocco {
    const char* nome;
    esp_pm_lock_type_t tipo;
    esp_pm_lock_handle_t h;
    bool preso;
};
Blocco g_blocchi[] = {
    {"cuffie", ESP_PM_CPU_FREQ_MAX, nullptr, false},  // Tieni::CUFFIE
};
portMUX_TYPE g_bloccoMux = portMUX_INITIALIZER_UNLOCKED;
#endif

// Risparmio di energia (CONFIG_PM_ENABLE, sdkconfig.defaults.tdeck): la CPU va a 240 MHz quando un compito
// lavora e scende a 40 MHz (il quarzo) quando sono tutti fermi; i driver di ESP-IDF tengono la frequenza
// che serve finche' usano una periferica (SPI, I2C, I2S acceso, scrittura sulla UART).
void risparmioInit() {
#if CONFIG_PM_ENABLE
    for (Blocco& b : g_blocchi)
        if (esp_pm_lock_create(b.tipo, 0, b.nome, &b.h) != ESP_OK) b.h = nullptr;
    esp_pm_config_t pm = {};
    pm.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pm.min_freq_mhz = 40;
    pm.light_sleep_enable = false;
    const esp_err_t e = esp_pm_configure(&pm);
    if (e == ESP_OK) ESP_LOGI(TAG, "risparmio: CPU da %d a %d MHz", pm.min_freq_mhz, pm.max_freq_mhz);
    else ESP_LOGW(TAG, "risparmio non attivato (errore %d)", int(e));
#endif
}
}  // namespace

Avvio holdPower() {
    phyAlComputer();  // per prima cosa: anche il log dell'avvio torna sulla console
    // Periferiche accese (display, tastiera, LoRa, GPS, audio).
    gpio_hold_dis(PIN_POWERON);
    gpio_deep_sleep_hold_dis();
    outputHigh(PIN_POWERON);
    // I tre dispositivi del bus SPI comune non selezionati prima di inizializzarlo.
    outputHigh(PIN_LCD_CS);
    outputHigh(PIN_LORA_NSS);
    outputHigh(PIN_SD_CS);
    gpio_set_pull_mode(PIN_SPI_MISO, GPIO_PULLUP_ONLY);
    g_bus = xSemaphoreCreateBinary();
    if (g_bus) xSemaphoreGive(g_bus);
    spi_bus_config_t b = {};
    b.mosi_io_num = PIN_SPI_MOSI;
    b.miso_io_num = PIN_SPI_MISO;
    b.sclk_io_num = PIN_SPI_SCK;
    b.quadwp_io_num = -1;
    b.quadhd_io_num = -1;
    b.max_transfer_sz = Display::WIDTH * 48 * 2 + 64;  // una fascia di LVGL in un colpo solo
    g_spi = spi_bus_initialize(SPI_HOST_SCHEDA, &b, SPI_DMA_CH_AUTO) == ESP_OK;
    if (!g_spi) ESP_LOGE(TAG, "bus SPI non inizializzato");
    vTaskDelay(pdMS_TO_TICKS(100));  // il tempo di accendersi (tastiera, microSD, GPS)
    const esp_reset_reason_t why = esp_reset_reason();
    const Avvio a = why == ESP_RST_POWERON || why == ESP_RST_BROWNOUT ? Avvio::TASTO : Avvio::RIAVVIO;
    ESP_LOGI(TAG, "T-Deck Plus: avvio %s (reset %d)", a == Avvio::TASTO ? "dall'interruttore" : "riavvio",
             int(why));
    return a;
}

bool spiBusReady() { return g_spi; }

void tieni(Tieni motivo, bool on) {
#if CONFIG_PM_ENABLE
    const size_t i = size_t(motivo);
    if (i >= sizeof(g_blocchi) / sizeof(g_blocchi[0]) || !g_blocchi[i].h) return;
    Blocco& b = g_blocchi[i];
    portENTER_CRITICAL(&g_bloccoMux);
    const bool cambia = b.preso != on;
    b.preso = on;
    portEXIT_CRITICAL(&g_bloccoMux);
    if (!cambia) return;
    if (on) esp_pm_lock_acquire(b.h);
    else esp_pm_lock_release(b.h);
#else
    (void)motivo;
    (void)on;
#endif
}

void busSpiPrendi() {
    if (g_bus) xSemaphoreTake(g_bus, portMAX_DELAY);
}

void busSpiLascia() {
    if (g_bus) xSemaphoreGive(g_bus);
}

bool busSpiPrendiEntro(uint32_t ms) { return !g_bus || xSemaphoreTake(g_bus, pdMS_TO_TICKS(ms)) == pdTRUE; }

bool IRAM_ATTR busSpiLasciaDaIsr() {
    BaseType_t woken = pdFALSE;
    if (g_bus) xSemaphoreGiveFromISR(g_bus, &woken);
    return woken == pdTRUE;
}

InitResult init(bool full) {
    InitResult r;
    r.expander = true;  // non c'e': niente da fare
    r.batteria = batteria().begin();
    g_blinkTimer = xTimerCreate("led", pdMS_TO_TICKS(100), pdFALSE, nullptr, blinkCb);
    if (!full) return r;
    g_full = true;

    r.tastiera = tastiera().begin();
    xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 12, &g_inputTask, 1);
    r.display = display().begin();
    r.audio = audio().begin();
    r.gnss = gnss().begin();
    touchADormire();
    risparmioInit();
    ESP_LOGI(TAG, "tastiera %d (%s), batteria %d, display %d, audio %d, gnss %d", r.tastiera,
             !tastiera().modeKnown() ? "modo da capire al primo tasto" : tastiera().rawMode() ? "raw" : "caratteri",
             r.batteria, r.display, r.audio, r.gnss);
    return r;
}

void vibrate(uint16_t) {}

void vibratePattern(uint8_t, uint16_t, uint16_t) {}

uint32_t ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count) {
    if (!g_blinkTimer) return 0;
    xTimerStop(g_blinkTimer, 0);
    uint16_t level = std::max(r, std::max(g, b));
    level = std::min<uint16_t>(255, level * 2 + 20);  // la luce della tastiera e' piu' debole del LED
    g_blink = {uint8_t(level), onMs, offMs, count, count == 0, true};
    g_blinking = true;
    applyKeyboardLight();
    xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(onMs), 0);
    if (++g_blinkId == 0) g_blinkId = 1;
    return g_blinkId;
}

void ledOff() {
    if (g_blinkTimer) xTimerStop(g_blinkTimer, 0);
    g_blinking = false;
    g_blinkId = 0;
    applyKeyboardLight();
}

void ledOffSe(uint32_t id) {
    if (id && id == g_blinkId) ledOff();
}

void keyboardLight(uint8_t level) {
    g_kbLevel = level;
    applyKeyboardLight();
}

void spiLowDrive() {}  // sulla PCB rallenta i fronti; sul T-Deck il bus porta anche il display

void powerOff() {
    ESP_LOGI(TAG, "spegnimento: periferiche spente e sonno profondo (si riaccende col trackball)");
    if (g_full) {
        display().sleep();
        g_kbLevel = 0;
        ledOff();
        tastiera().light(0);
        audio().stop();
        gnss().power(false);
    }
    vTaskDelay(pdMS_TO_TICKS(300));  // comando di standby del GPS e audio svuotato
    gpio_set_level(PIN_LORA_RST, 0);
    gpio_set_level(PIN_POWERON, 0);
    gpio_hold_en(PIN_POWERON);
    gpio_deep_sleep_hold_en();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // Si riaccende premendo il trackball (GPIO 0): il chip resta in sonno profondo e il suo orologio
    // conta, cosi' l'ora resta giusta (l'interruttore laterale invece stacca la batteria e l'ora si
    // perde). Prima si aspetta che sia lasciato il clic con cui si e' scelto "Spegni", se no ci si
    // risveglierebbe subito.
    for (int i = 0; i < 300 && gpio_get_level(PIN_TB_CLICK) == 0; i++) vTaskDelay(pdMS_TO_TICKS(10));
    esp_sleep_enable_ext0_wakeup(PIN_TB_CLICK, 0);
    rtc_gpio_pullup_en(PIN_TB_CLICK);
    rtc_gpio_pulldown_dis(PIN_TB_CLICK);
    // Sveglia a telefono spento: ci si riaccende da soli (l'orologio del chip conta anche adesso).
    if (g_wakeAfterS) {
        ESP_LOGI(TAG, "risveglio per la sveglia tra %lu s", (unsigned long)g_wakeAfterS);
        esp_sleep_enable_timer_wakeup(uint64_t(g_wakeAfterS) * 1000000ULL);
    }
#if CONFIG_RTC_CLK_SRC_INT_8MD256
    // L'orologio lento del chip e' l'oscillatore da 17,5 MHz diviso 256 (sdkconfig.defaults.tdeck):
    // nel sonno profondo deve restare acceso. ESP-IDF 5.5 lo spegne sempre (sleep_modes.c, i flag
    // forzati del sonno profondo; esp_sleep_pd_config non basta) tranne se e' attivo il modo
    // RTC_USE_RC_FAST, che pero' attiva solo quando all'avvio ci passa da un'altra sorgente, cioe'
    // dopo un'accensione vera (il contatore resta dopo un risveglio, ma non dopo un riavvio). Dopo
    // un riavvio (aggiornamento, crash, caricamento via USB) l'oscillatore si spegneva: l'orologio
    // del chip si fermava e non ci si risvegliava piu' (ne' col trackball ne' per la sveglia), e
    // l'ora era persa. Visto sul T-Deck vero il 14/11/2026.
    esp_sleep_sub_mode_config(ESP_SLEEP_RTC_USE_RC_FAST_MODE, true);
#endif
    phyAlComputer();  // cuffie USB-C accese: al risveglio la porta e' di nuovo del computer
    esp_deep_sleep_start();
}

void wakeAfter(uint32_t seconds) { g_wakeAfterS = seconds; }

bool wokeForAlarm() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER; }

bool wokeFromSleep() { return esp_reset_reason() == ESP_RST_DEEPSLEEP; }

}  // namespace scheda
