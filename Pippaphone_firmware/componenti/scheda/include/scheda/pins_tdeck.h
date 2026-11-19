// Piedinatura del LilyGO T-Deck Plus (ESP32-S3FN16R8), dai sorgenti ufficiali LilyGO
// (examples/UnitTest/utilities.h del repository Xinyuan-LilyGO/T-Deck).
//
// Display, LoRa e microSD stanno sullo stesso bus SPI: i tre CS vanno tenuti alti finche' ogni
// dispositivo non e' inizializzato. Le periferiche si accendono con POWERON (GPIO 10) a 1.
#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"

namespace scheda {

// ---- alimentazione delle periferiche (display, tastiera, LoRa, GPS, audio) ----
constexpr gpio_num_t PIN_POWERON = GPIO_NUM_10;

// ---- bus SPI condiviso: display ST7789, LoRa SX1262, microSD ----
constexpr spi_host_device_t SPI_HOST_SCHEDA = SPI2_HOST;
constexpr gpio_num_t PIN_SPI_MOSI = GPIO_NUM_41;
constexpr gpio_num_t PIN_SPI_SCK = GPIO_NUM_40;
constexpr gpio_num_t PIN_SPI_MISO = GPIO_NUM_38;

// ---- LoRa SX1262 (TCXO a 1,8 V su DIO3, commutatore d'antenna su DIO2) ----
constexpr gpio_num_t PIN_LORA_NSS = GPIO_NUM_9;
constexpr gpio_num_t PIN_LORA_BUSY = GPIO_NUM_13;
constexpr gpio_num_t PIN_LORA_DIO1 = GPIO_NUM_45;
constexpr gpio_num_t PIN_LORA_RST = GPIO_NUM_17;

// ---- display ST7789 320x240 (SPI), senza pin di reset ----
constexpr gpio_num_t PIN_LCD_CS = GPIO_NUM_12;
constexpr gpio_num_t PIN_LCD_DC = GPIO_NUM_11;
constexpr gpio_num_t PIN_LCD_BL = GPIO_NUM_42;  // driver della retroilluminazione a impulsi (16 livelli)

// ---- microSD ----
constexpr gpio_num_t PIN_SD_CS = GPIO_NUM_39;

// ---- I2C: tastiera (ESP32-C3) 0x55, codec del microfono ES7210 0x40, touch GT911 (non usato) ----
constexpr gpio_num_t PIN_I2C_SDA = GPIO_NUM_18;
constexpr gpio_num_t PIN_I2C_SCL = GPIO_NUM_8;
constexpr bool I2C_PULLUP_INTERNE = true;  // in aggiunta a quelle della scheda, non fanno danni
constexpr uint8_t ADDR_TASTIERA = 0x55;
constexpr uint8_t ADDR_ES7210 = 0x40;
// Touch GT911: il telefono non lo usa, ma all'avvio va messo a dormire. L'indirizzo dipende dal suo INT
// all'accensione (0x5D o 0x14).
constexpr uint8_t ADDR_GT911_A = 0x5D, ADDR_GT911_B = 0x14;
constexpr gpio_num_t PIN_TOUCH_INT = GPIO_NUM_16;
// Linea C3_INT dello schema LilyGO: GPIO 8 dell'ESP32-C3 della tastiera -> GPIO 46 dell'ESP32-S3. Il firmware
// di fabbrica del C3 non la usa; quello del Pippaphone (strumenti/tastiera_c3) la alza quando cambia un tasto.
// GPIO 46 e' anche uno strapping: alto con GPIO 0 basso all'accensione non va bene, quindi il C3 la tiene bassa
// e la alza solo per pochi millisecondi.
constexpr gpio_num_t PIN_KB_INT = GPIO_NUM_46;

// ---- altoparlante: MAX98357A su I2S0 ----
constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_7;
constexpr gpio_num_t PIN_I2S_WS = GPIO_NUM_5;
constexpr gpio_num_t PIN_I2S_DOUT = GPIO_NUM_6;

// ---- microfono: ES7210 su I2S1 (l'ESP32 da' MCLK, BCLK e LRCK) ----
constexpr gpio_num_t PIN_MIC_MCLK = GPIO_NUM_48;
constexpr gpio_num_t PIN_MIC_SCK = GPIO_NUM_47;
constexpr gpio_num_t PIN_MIC_LRCK = GPIO_NUM_21;
constexpr gpio_num_t PIN_MIC_DIN = GPIO_NUM_14;

// ---- GPS (u-blox M10Q a 38400 baud oppure Quectel L76K a 9600): seriale ----
constexpr gpio_num_t PIN_GNSS_RX = GPIO_NUM_44;  // ingresso dell'ESP32 (TX del GPS)
constexpr gpio_num_t PIN_GNSS_TX = GPIO_NUM_43;  // uscita dell'ESP32 (RX del GPS)

// ---- batteria: tensione sul partitore 1:2 ----
constexpr gpio_num_t PIN_BATT_ADC = GPIO_NUM_4;

// ---- trackball: un impulso per ogni scatto, il clic e' il GPIO 0 (anche BOOT) ----
constexpr gpio_num_t PIN_TB_UP = GPIO_NUM_3;
constexpr gpio_num_t PIN_TB_DOWN = GPIO_NUM_15;
constexpr gpio_num_t PIN_TB_LEFT = GPIO_NUM_1;
constexpr gpio_num_t PIN_TB_RIGHT = GPIO_NUM_2;
constexpr gpio_num_t PIN_TB_CLICK = GPIO_NUM_0;

}  // namespace scheda
