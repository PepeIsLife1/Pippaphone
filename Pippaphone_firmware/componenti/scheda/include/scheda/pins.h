// Piedinatura della PCB 1 rev. 1.1 (Pippaphone), ricavata dal file KiCad.
//
// Rev. 1.1: il 3,3 V si accende col tasto PWR e resta acceso finche' PWR_HOLD (P1_7) e' a 1;
// il tasto arriva a P1_3 attraverso D8 (con R60 a +3,3 V); CHG_N del BQ24074 accende solo il
// LED rosso e non e' piu' leggibile (la carica si ricava dal MAX17048).
#pragma once

#include "driver/gpio.h"

namespace scheda {

// ---- LoRa SX1262 (bus SPI condiviso con la microSD) ----
constexpr gpio_num_t PIN_SPI_MOSI = GPIO_NUM_11;
constexpr gpio_num_t PIN_SPI_SCK = GPIO_NUM_12;
constexpr gpio_num_t PIN_SPI_MISO = GPIO_NUM_13;
constexpr gpio_num_t PIN_LORA_NSS = GPIO_NUM_8;
constexpr gpio_num_t PIN_LORA_BUSY = GPIO_NUM_7;
constexpr gpio_num_t PIN_LORA_DIO1 = GPIO_NUM_6;
// Reset LoRa: expander P0_7. TCXO a 1,8 V da DIO3, switch RF da DIO2.

// ---- I2C: AW9523B 0x58, MAX17048 0x36, TCA8418 0x34 (PCB 2) ----
constexpr gpio_num_t PIN_I2C_SDA = GPIO_NUM_15;
constexpr gpio_num_t PIN_I2C_SCL = GPIO_NUM_16;
constexpr gpio_num_t PIN_INT_N = GPIO_NUM_5;  // interruzione condivisa, open drain, sveglia dal deep sleep
constexpr uint8_t ADDR_AW9523 = 0x58;
constexpr uint8_t ADDR_MAX17048 = 0x36;
constexpr uint8_t ADDR_TCA8418 = 0x34;

// ---- Display RGB 480x480 (ST7701S), 12 bit: R3-R5, G0-G5, B3-B5 ----
constexpr gpio_num_t PIN_LCD_VSYNC = GPIO_NUM_0;  // anche strap di boot (pull-up)
constexpr gpio_num_t PIN_LCD_HSYNC = GPIO_NUM_1;
constexpr gpio_num_t PIN_LCD_DE = GPIO_NUM_9;
constexpr gpio_num_t PIN_LCD_PCLK = GPIO_NUM_43;
constexpr gpio_num_t PIN_LCD_R3 = GPIO_NUM_2;
constexpr gpio_num_t PIN_LCD_R4 = GPIO_NUM_42;
constexpr gpio_num_t PIN_LCD_R5 = GPIO_NUM_41;
constexpr gpio_num_t PIN_LCD_G0 = GPIO_NUM_40;
constexpr gpio_num_t PIN_LCD_G1 = GPIO_NUM_39;
constexpr gpio_num_t PIN_LCD_G2 = GPIO_NUM_38;
constexpr gpio_num_t PIN_LCD_G3 = GPIO_NUM_21;
constexpr gpio_num_t PIN_LCD_G4 = GPIO_NUM_47;  // anche SCK della SPI a 3 fili di inizializzazione
constexpr gpio_num_t PIN_LCD_G5 = GPIO_NUM_48;  // anche SDA della SPI a 3 fili
constexpr gpio_num_t PIN_LCD_B3 = GPIO_NUM_3;
constexpr gpio_num_t PIN_LCD_B4 = GPIO_NUM_45;  // strap VDD_SPI (pull-down)
constexpr gpio_num_t PIN_LCD_B5 = GPIO_NUM_46;  // strap (pull-down)
constexpr gpio_num_t PIN_LCD_BL = GPIO_NUM_10;  // PWM verso il TPS61169
// CS e RESET del pannello: expander P1_1 e P1_0.

// ---- Audio I2S: MAX98357A (altoparlante) e ICS-43434 (microfono) sullo stesso bus ----
constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_17;
constexpr gpio_num_t PIN_I2S_WS = GPIO_NUM_18;
constexpr gpio_num_t PIN_I2S_DOUT = GPIO_NUM_14;
constexpr gpio_num_t PIN_I2S_DIN = GPIO_NUM_4;

// ---- GNSS AT6558R: solo uscita NMEA verso l'ESP32 ----
constexpr gpio_num_t PIN_GNSS_RX = GPIO_NUM_44;

// ---- Uscite e ingressi dell'expander AW9523B ----
enum class Exp : uint8_t {
    LED_R = 0,       // P0_0 (modo LED)
    LED_G = 1,       // P0_1 (modo LED)
    LED_B = 2,       // P0_2 (modo LED)
    KB_BL = 3,       // P0_3 retroilluminazione tastiera (modo LED)
    VIB_EN = 4,      // P0_4 motore vibrazione
    AMP_SD = 5,      // P0_5 amplificatore: 1 = acceso (canale sinistro), 0 = spento
    GNSS_EN = 6,     // P0_6 GNSS: 1 = acceso, 0 = standby
    LORA_RST_N = 7,  // P0_7
    LCD_RST_N = 8,   // P1_0
    LCD_CS_N = 9,    // P1_1
    SD_CS_N = 10,    // P1_2
    BTN_PWR_N = 11,  // P1_3 ingresso: BTN_PWR_SENSE (tasto PWR attraverso D8), 0 = premuto
    BTN_PTT_N = 12,  // P1_4 ingresso
    BTN_VOLUP_N = 13,  // P1_5 ingresso
    BTN_VOLDN_N = 14,  // P1_6 ingresso
    PWR_HOLD = 15,   // P1_7 uscita: 1 = tiene acceso il TPS63001 (autoritenuta), 0 = spegne tutto
};

}  // namespace scheda
