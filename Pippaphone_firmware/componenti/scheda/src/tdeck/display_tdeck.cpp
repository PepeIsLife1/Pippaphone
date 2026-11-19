// Display del LilyGO T-Deck Plus: ST7789 2,8" 240x320 usato in orizzontale (320x240), sul bus
// SPI comune con LoRa e microSD (il bus lo crea holdPower()).
//
// Da provare sull'hardware: orientamento (scambio x/y e specchio), inversione dei colori e
// ordine RGB/BGR, presi dalla configurazione di fabbrica (TFT_eSPI, rotazione 1, colori invertiti).
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bus_spi.h"
#include "scheda/display.h"
#include "scheda/pins.h"
#include "scheda/scheda.h"

namespace scheda {

namespace {
const char* TAG = "display";
constexpr int PCLK_HZ = 40 * 1000 * 1000;
constexpr bool SWAP_XY = true, MIRROR_X = true, MIRROR_Y = false, INVERT = true;
constexpr int BL_STEPS = 16;

// Registri del pannello come nel codice di fabbrica LilyGO ("T-Deck-Plus and T-Deck display panels
// are different"): porch, tensioni, frequenza, gamma. Il resto (sonno, formato, orientamento,
// inversione, accensione) lo fa esp_lcd.
struct Cmd {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[14];
};
const Cmd INIT[] = {
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},
    {0xB7, 1, {0x75}},
    {0xBB, 1, {0x1A}},
    {0xC0, 1, {0x2C}},
    {0xC2, 1, {0x01}},
    {0xC3, 1, {0x13}},
    {0xC4, 1, {0x20}},
    {0xC6, 1, {0x0F}},
    {0xD0, 2, {0xA4, 0xA1}},
    {0xD6, 1, {0xA1}},
    {0xE0, 14, {0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}},
    {0xE1, 14, {0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x3E}},
};

portMUX_TYPE g_blMux = portMUX_INITIALIZER_UNLOCKED;

// Fine del rettangolo: il bus torna libero (il turno l'ha preso draw()).
bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    return busSpiLasciaDaIsr();
}
}  // namespace

Display& display() {
    static Display d;
    return d;
}

bool Display::begin() {
    // Retroilluminazione spenta durante l'avvio.
    gpio_config_t bl = {};
    bl.pin_bit_mask = 1ULL << PIN_LCD_BL;
    bl.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl);
    gpio_set_level(PIN_LCD_BL, 0);
    level_ = 0;

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = PIN_LCD_CS;
    io.dc_gpio_num = PIN_LCD_DC;
    io.spi_mode = 0;
    io.pclk_hz = PCLK_HZ;
    io.trans_queue_depth = 4;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.on_color_trans_done = onColorDone;
    if (esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t(SPI_HOST_SCHEDA), &io, &io_) != ESP_OK) {
        ESP_LOGE(TAG, "SPI del display non creata");
        return false;
    }
    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num = GPIO_NUM_NC;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    pc.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7789(io_, &pc, &panel_) != ESP_OK) {
        ESP_LOGE(TAG, "pannello ST7789 non creato");
        return false;
    }
    busSpiPrendi();
    esp_lcd_panel_reset(panel_);  // reset software (0x01): il pannello non ha il pin di reset
    esp_lcd_panel_init(panel_);
    for (const Cmd& c : INIT) esp_lcd_panel_io_tx_param(io_, c.cmd, c.data, c.len);
    esp_lcd_panel_invert_color(panel_, INVERT);
    esp_lcd_panel_swap_xy(panel_, SWAP_XY);
    esp_lcd_panel_mirror(panel_, MIRROR_X, MIRROR_Y);
    esp_lcd_panel_disp_on_off(panel_, true);
    busSpiLascia();
    ESP_LOGI(TAG, "ST7789 %dx%d pronto", WIDTH, HEIGHT);
    return true;
}

void Display::draw(int x1, int y1, int x2, int y2, const void* pixels) {
    if (!panel_ || asleep_) return;  // a schermo spento LVGL non disegna (ui.cpp): per sicurezza
    // Il turno sul bus: libero quando il rettangolo precedente e' arrivato (il suo buffer torna a
    // LVGL) e LoRa e microSD non stanno trasmettendo. Lo restituisce onColorDone a invio finito.
    if (!busSpiPrendiEntro(1000)) {
        ESP_LOGW(TAG, "bus SPI occupato da 1 s: rettangolo saltato");
        return;
    }
    // RGB565 di LVGL (little endian) -> big endian per l'ST7789.
    uint16_t* p = static_cast<uint16_t*>(const_cast<void*>(pixels));
    const size_t n = size_t(x2 - x1) * size_t(y2 - y1);
    for (size_t i = 0; i < n; i++) p[i] = uint16_t((p[i] >> 8) | (p[i] << 8));
    if (esp_lcd_panel_draw_bitmap(panel_, x1, y1, x2, y2, pixels) != ESP_OK) busSpiLascia();
}

// Driver della retroilluminazione a impulsi (come setBrightness() di LilyGO): da spento si accende
// al livello massimo, poi ogni impulso basso scende di un livello, ciclicamente su 16.
void Display::setLevel(uint8_t level) {
    if (level > BL_STEPS) level = BL_STEPS;
    if (level == level_) return;
    if (level == 0) {
        gpio_set_level(PIN_LCD_BL, 0);
        vTaskDelay(pdMS_TO_TICKS(3));  // basso per piu' di 2,5 ms: spento
        level_ = 0;
        return;
    }
    if (level_ == 0) {
        gpio_set_level(PIN_LCD_BL, 1);
        esp_rom_delay_us(30);
        level_ = BL_STEPS;
    }
    const int from = BL_STEPS - level_, to = BL_STEPS - level;
    const int pulses = (BL_STEPS + to - from) % BL_STEPS;
    portENTER_CRITICAL(&g_blMux);  // un impulso troppo lungo spegnerebbe il driver
    for (int i = 0; i < pulses; i++) {
        gpio_set_level(PIN_LCD_BL, 0);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_LCD_BL, 1);
        esp_rom_delay_us(1);
    }
    portEXIT_CRITICAL(&g_blMux);
    level_ = level;
}

void Display::backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    bl_ = percent;
    // Curva quadratica come sulla PCB: a bassa luminosita' i passi sono piu' fini.
    uint32_t level = 0;
    if (percent) {
        level = (uint32_t(percent) * percent * BL_STEPS + 9999) / 10000;
        if (level < 1) level = 1;
    }
    setLevel(uint8_t(level));
}

void Display::sleep() {
    setLevel(0);
    if (!panel_ || asleep_) return;
    // SLPIN almeno 120 ms dopo l'ultimo SLPOUT (datasheet ST7789): schermo spento e riacceso di fila.
    const int64_t da = (esp_timer_get_time() - wokeUs_) / 1000;
    if (wokeUs_ && da < 120) vTaskDelay(pdMS_TO_TICKS(120 - da));
    busSpiPrendi();
    esp_lcd_panel_disp_on_off(panel_, false);
    esp_lcd_panel_disp_sleep(panel_, true);  // esp_lcd aspetta 100 ms
    busSpiLascia();
    asleep_ = true;
}

void Display::wake() {
    if (!panel_ || !asleep_) return;
    busSpiPrendi();
    esp_lcd_panel_disp_sleep(panel_, false);  // SLPOUT, poi esp_lcd aspetta 100 ms
    esp_lcd_panel_disp_on_off(panel_, true);
    busSpiLascia();
    wokeUs_ = esp_timer_get_time();
    asleep_ = false;
}

}  // namespace scheda
