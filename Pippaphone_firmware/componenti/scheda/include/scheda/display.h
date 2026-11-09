// Display 3,4" 480x480 (TL034WVS05-B1477A, controller ST7701S) in RGB a 12 bit.
//
// Il pannello si inizializza con una SPI a 3 fili fatta a mano sui pin G4 (SCK) e G5 (SDA),
// con CS e RESET dall'expander; poi quei pin diventano dati RGB.
// Il frame buffer (RGB565, 450 KB) sta in PSRAM.
#pragma once

#include <cstdint>

#include "esp_lcd_panel_ops.h"
#include "sdkconfig.h"

namespace scheda {

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: ST7789 2,8" 320x240 orizzontale sul bus SPI comune (con LoRa e microSD).
// draw() scambia i byte dei pixel (l'ST7789 li vuole big endian) e manda il rettangolo in DMA
// senza aspettare: aspetta solo che sia finito il rettangolo precedente.
class Display {
public:
    static constexpr int WIDTH = 320;
    static constexpr int HEIGHT = 240;

    bool begin();
    esp_lcd_panel_handle_t panel() const { return panel_; }
    // Copia un rettangolo di pixel RGB565 (x2, y2 esclusi). I pixel vengono modificati.
    void draw(int x1, int y1, int x2, int y2, const void* pixels);
    // Retroilluminazione 0..100 % (il driver ha 16 livelli).
    void backlight(uint8_t percent);
    uint8_t brightness() const { return bl_; }
    // Pannello in sonno (prima dello spegnimento).
    void sleep();

private:
    void setLevel(uint8_t level);
    esp_lcd_panel_handle_t panel_ = nullptr;
    esp_lcd_panel_io_handle_t io_ = nullptr;
    uint8_t bl_ = 0;
    uint8_t level_ = 0;  // livello attuale del driver della retroilluminazione (0..16)
};
#else
class Display {
public:
    static constexpr int WIDTH = 480;
    static constexpr int HEIGHT = 480;

    bool begin();
    esp_lcd_panel_handle_t panel() const { return panel_; }
    // Copia un rettangolo di pixel RGB565 (x2, y2 esclusi).
    void draw(int x1, int y1, int x2, int y2, const void* pixels);
    // Retroilluminazione 0..100 %.
    void backlight(uint8_t percent);
    uint8_t brightness() const { return bl_; }

private:
    void panelInit();
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint8_t bl_ = 0;
};
#endif

Display& display();

}  // namespace scheda
