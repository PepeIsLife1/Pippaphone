#include "scheda/display.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scheda/expander.h"
#include "scheda/pins.h"

namespace scheda {

namespace {
const char* TAG = "display";

// Sequenza di inizializzazione del pannello TL034WVS05-B1477A (dal codice del produttore,
// come in Arduino_GFX / Adafruit Qualia). Formato: comando, n. parametri, parametri...;
// 0xFE = attesa in ms.
const uint8_t INIT[] = {
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    0xEF, 1, 0x08,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    0xC0, 2, 0x3B, 0x00,
    0xC1, 2, 0x12, 0x0A,
    0xC2, 2, 0x07, 0x03,
    0xC3, 1, 0x02,
    0xCC, 1, 0x10,
    0xCD, 1, 0x08,
    0xB0, 16, 0x0F, 0x11, 0x17, 0x15, 0x15, 0x09, 0x0C, 0x08, 0x08, 0x26, 0x04, 0x59, 0x16, 0x66, 0x2D, 0x1F,
    0xB1, 16, 0x0F, 0x11, 0x17, 0x15, 0x15, 0x09, 0x0C, 0x08, 0x08, 0x26, 0x04, 0x59, 0x16, 0x66, 0x2D, 0x1F,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
    0xB0, 1, 0x6D,
    0xB1, 1, 0x3A,
    0xB2, 1, 0x01,
    0xB3, 1, 0x80,
    0xB5, 1, 0x49,
    0xB7, 1, 0x85,
    0xB8, 1, 0x20,
    0xC1, 1, 0x78,
    0xC2, 1, 0x78,
    0xD0, 1, 0x88,
    0xE0, 3, 0x00, 0x00, 0x02,
    0xE1, 11, 0x07, 0x00, 0x09, 0x00, 0x06, 0x00, 0x08, 0x00, 0x00, 0x33, 0x33,
    0xE2, 13, 0x11, 0x11, 0x33, 0x33, 0xF6, 0x00, 0xF6, 0x00, 0xF6, 0x00, 0xF6, 0x00, 0x00,
    0xE3, 4, 0x00, 0x00, 0x11, 0x11,
    0xE4, 2, 0x44, 0x44,
    0xE5, 16, 0x0F, 0xF3, 0x3D, 0xFF, 0x11, 0xF5, 0x3D, 0xFF, 0x0B, 0xEF, 0x3D, 0xFF, 0x0D, 0xF1, 0x3D, 0xFF,
    0xE6, 4, 0x00, 0x00, 0x11, 0x11,
    0xE7, 2, 0x44, 0x44,
    0xE8, 16, 0x0E, 0xF2, 0x3D, 0xFF, 0x10, 0xF4, 0x3D, 0xFF, 0x0A, 0xEE, 0x3D, 0xFF, 0x0C, 0xF0, 0x3D, 0xFF,
    0xE9, 2, 0x36, 0x00,
    0xEB, 7, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0xAA, 0x10,
    0xEC, 2, 0x3C, 0x00,
    0xED, 16, 0xFF, 0x45, 0x67, 0xFA, 0x01, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x10, 0xAF, 0x76, 0x54, 0xFF,
    0xEF, 6, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    0x35, 1, 0x00,
    0x3A, 1, 0x66,  // 18 bit (i bit bassi sono collegati ai piu' alti sulla scheda)
    0x11, 0,        // esce dal sonno
    0xFE, 120,
    0x29, 0,        // display acceso
    0xFE, 50,
};

// SPI a 3 fili a 9 bit: primo bit 0 = comando, 1 = dato.
void bitbangByte(bool isData, uint8_t v) {
    gpio_set_level(PIN_LCD_G4, 0);
    gpio_set_level(PIN_LCD_G5, isData);
    esp_rom_delay_us(1);
    gpio_set_level(PIN_LCD_G4, 1);
    esp_rom_delay_us(1);
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_LCD_G4, 0);
        gpio_set_level(PIN_LCD_G5, (v >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_LCD_G4, 1);
        esp_rom_delay_us(1);
    }
    gpio_set_level(PIN_LCD_G4, 0);
}
}  // namespace

Display& display() {
    static Display d;
    return d;
}

void Display::panelInit() {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PIN_LCD_G4) | (1ULL << PIN_LCD_G5);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(PIN_LCD_G4, 0);
    gpio_set_level(PIN_LCD_G5, 0);

    Expander& ex = expander();
    ex.set(Exp::LCD_CS_N, true);
    ex.set(Exp::LCD_RST_N, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    ex.set(Exp::LCD_RST_N, true);
    vTaskDelay(pdMS_TO_TICKS(120));

    size_t i = 0;
    while (i < sizeof(INIT)) {
        const uint8_t cmd = INIT[i++];
        const uint8_t n = INIT[i++];
        if (cmd == 0xFE) {
            vTaskDelay(pdMS_TO_TICKS(n));
            continue;
        }
        ex.set(Exp::LCD_CS_N, false);
        bitbangByte(false, cmd);
        for (uint8_t k = 0; k < n; k++) bitbangByte(true, INIT[i + k]);
        ex.set(Exp::LCD_CS_N, true);
        i += n;
    }
}

bool Display::begin() {
    // Retroilluminazione spenta durante l'avvio.
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = 20000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t c = {};
    c.gpio_num = PIN_LCD_BL;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_0;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    ledc_channel_config(&c);

    panelInit();

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = 14 * 1000 * 1000;
    cfg.timings.h_res = WIDTH;
    cfg.timings.v_res = HEIGHT;
    cfg.timings.hsync_pulse_width = 2;
    cfg.timings.hsync_back_porch = 44;
    cfg.timings.hsync_front_porch = 50;
    cfg.timings.vsync_pulse_width = 2;
    cfg.timings.vsync_back_porch = 18;
    cfg.timings.vsync_front_porch = 16;
    cfg.timings.flags.pclk_active_neg = false;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = WIDTH * 10;  // frame buffer in PSRAM, DMA da buffer interni
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = PIN_LCD_HSYNC;
    cfg.vsync_gpio_num = PIN_LCD_VSYNC;
    cfg.de_gpio_num = PIN_LCD_DE;
    cfg.pclk_gpio_num = PIN_LCD_PCLK;
    cfg.disp_gpio_num = GPIO_NUM_NC;
    // RGB565 sul bus a 16 bit: D0-D4 blu, D5-D10 verde, D11-D15 rosso.
    // Sulla scheda arrivano solo i 3 bit alti di rosso e blu e tutti i 6 del verde.
    const int data[16] = {
        -1, -1, PIN_LCD_B3, PIN_LCD_B4, PIN_LCD_B5,                            // B0..B4
        PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,  // G0..G5
        -1, -1, PIN_LCD_R3, PIN_LCD_R4, PIN_LCD_R5,                            // R0..R4
    };
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.flags.fb_in_psram = true;

    if (esp_lcd_new_rgb_panel(&cfg, &panel_) != ESP_OK) {
        ESP_LOGE(TAG, "pannello RGB non creato");
        return false;
    }
    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);
    return true;
}

void Display::draw(int x1, int y1, int x2, int y2, const void* pixels) {
    if (panel_) esp_lcd_panel_draw_bitmap(panel_, x1, y1, x2, y2, pixels);
}

void Display::backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    bl_ = percent;
    // Curva quadratica: a bassa luminosita' i passi sono piu' fini.
    const uint32_t duty = uint32_t(percent) * percent * 1023 / 10000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

}  // namespace scheda
