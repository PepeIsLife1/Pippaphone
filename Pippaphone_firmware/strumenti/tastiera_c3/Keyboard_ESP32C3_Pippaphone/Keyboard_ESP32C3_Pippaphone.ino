/**
 * Tastiera del LilyGO T-Deck / T-Deck Plus (ESP32-C3) per il Pippaphone: lo stesso protocollo del firmware
 * LilyGO, ma consuma poco.
 *
 * Deriva da examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino del repository Xinyuan-LilyGO/T-Deck (revisione del
 * 12/06/2025, quella col modo raw): stessi piedini, stesse tabelle dei tasti e stessi comandi I2C (indirizzo
 * 0x55, 100 kHz). Va quindi anche con gli altri firmware dell'S3 (esempi LilyGO, Meshtastic...).
 *
 * Cosa cambia:
 *  - CPU a 40 MHz (il quarzo, senza PLL) invece di 80;
 *  - la matrice si legge in circa un millisecondo (pochi microsecondi per riga invece di 1 ms) ogni
 *    SCANSIONE_MS, con un filtro dei rimbalzi: un tasto cambia quando due letture di fila sono uguali;
 *  - 0x10 (saluto): la lettura successiva risponde A5 5A <versione> <funzioni> 00. Cosi' il telefono sa che
 *    c'e' questo firmware (quello di LilyGO ignora il comando);
 *  - 0x11 <1|0>: modo a basso consumo acceso o spento (all'accensione spento: senza il comando il C3 si
 *    comporta come quello di LilyGO). Acceso:
 *      - quando un tasto cambia, la linea C3_INT (GPIO 8, verso il GPIO 46 dell'S3) va alta finche' l'S3 non
 *        legge, al massimo INT_MAX_MS: il GPIO 46 e' uno strapping dell'S3, che alto col GPIO 0 basso al
 *        reset non si avvierebbe;
 *      - senza tasti premuti, luce spenta e niente I2C da RESTA_SVEGLIO_MS, il C3 va in light sleep con la
 *        matrice pronta a svegliarlo (colonne basse, righe con pull-up: un tasto porta una riga a 0). Lo
 *        sveglia anche l'S3 (SDA che scende all'inizio di una transazione): quella transazione non riceve
 *        risposta e l'S3 la ripete;
 *  - il comando 0x01 (luce) non legge piu' un byte in piu' (mancava un break).
 *
 * Copyright (c) 2023 Shenzhen Xin Yuan Electronic Technology Co., Ltd (LilyGO), autore Koby Hale.
 * Licenza MIT:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <Arduino.h>
#include <Wire.h>

#include "driver/gpio.h"
#include "esp_sleep.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3

#define I2C_DEV_ADDR                        0x55
#define keyboard_BL_PIN                     9
#define SDA                                 2
#define SCL                                 10
#define C3_INT_PIN                          8     // C3_INT dello schema LilyGO, verso il GPIO 46 dell'S3
#define KB_BRIGHTNESS_CH                    0
#define KB_BRIGHTNESS_FREQ                  1000
#define KB_BRIGHTNESS_RES                   8     //Resolution_bits
#define KB_BRIGHTNESS_BOOT_DUTY             0
#define KB_BRIGHTNESS_DEFAULT_DUTY          127   //Alt+B default duty , is duty is zero , use setting duty

#define LILYGO_KB_BRIGHTNESS_CMD            0x01
#define LILYGO_KB_ALT_B_BRIGHTNESS_CMD      0x02
#define LILYGO_KB_MODE_RAW_CMD              0x03
#define LILYGO_KB_MODE_KEY_CMD              0x04
#define PIPPA_SALUTO_CMD                    0x10
#define PIPPA_RISPARMIO_CMD                 0x11

static const uint8_t PIPPA_VERSIONE = 1;
static const uint8_t PIPPA_FUNZIONI = 0x01;   // bit 0: C3_INT e light sleep (comando 0x11)

static const uint32_t CPU_MHZ = 40;            // 80 come LilyGO, se la tastiera desse problemi
static const uint32_t SCANSIONE_MS = 8;        // una lettura della matrice ogni 8 ms
static const uint32_t ASSESTAMENTO_US = 10;    // dopo la pull-up su una riga, prima di leggerla
static const uint32_t INT_MAX_MS = 50;         // C3_INT alta al massimo cosi' (strapping dell'S3)
static const uint32_t RESTA_SVEGLIO_MS = 300;  // dopo l'ultimo tasto o l'ultima transazione I2C

uint8_t  rows[] = {0, 3, 19, 12, 18, 6, 7 };
const int rowCount = sizeof(rows) / sizeof(rows[0]);

uint8_t  cols[] = {1, 4, 5, 11, 13};
const int colCount = sizeof(cols) / sizeof(cols[0]);

bool keys[colCount][rowCount];          // stato accettato (dopo il filtro dei rimbalzi)
bool lastValue[colCount][rowCount];     // quello che manda il modo raw (uguale a keys)
bool changedValue[colCount][rowCount];  // cambiato nell'ultima scansione
bool grezzo[colCount][rowCount];        // lettura precedente, per il filtro dei rimbalzi
char keyboard[colCount][rowCount];
char keyboard_symbol[colCount][rowCount];
volatile bool rawMode = false;

bool BL_state = false;
volatile bool comdata_flag = false;
volatile char comdata;
uint8_t kb_brightness_duty = KB_BRIGHTNESS_BOOT_DUTY;
uint8_t kb_brightness_setting_duty = KB_BRIGHTNESS_DEFAULT_DUTY;

// Pippaphone: saluto, modo a basso consumo, C3_INT.
volatile bool g_saluto = false;           // la prossima lettura e' la risposta al saluto
volatile bool g_risparmio = false;        // 0x11 1: C3_INT e light sleep
volatile bool g_intAlto = false;
volatile uint32_t g_intDaMs = 0;
volatile uint32_t g_ultimaI2cMs = 0;
uint32_t g_ultimoCambioMs = 0;
volatile uint8_t g_dutyAdesso = 0;        // luce della tastiera in questo momento

void onRequest();
void onReceive(int len);
void readMatrix();
bool keyPressed(int colIndex, int rowIndex);
bool keyActive(int colIndex, int rowIndex);
bool isPrintableKey(int colIndex, int rowIndex);
void printMatrix();

// ---- luce della tastiera (Arduino 2.x e 3.x hanno due API diverse per il LEDC) ----
void luceInit()
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(keyboard_BL_PIN, KB_BRIGHTNESS_FREQ, KB_BRIGHTNESS_RES);
    ledcWrite(keyboard_BL_PIN, KB_BRIGHTNESS_BOOT_DUTY);
#else
    ledcSetup(KB_BRIGHTNESS_CH, KB_BRIGHTNESS_FREQ, KB_BRIGHTNESS_RES);
    ledcAttachPin(keyboard_BL_PIN, KB_BRIGHTNESS_CH);
    ledcWrite(KB_BRIGHTNESS_CH, KB_BRIGHTNESS_BOOT_DUTY);
#endif
    g_dutyAdesso = KB_BRIGHTNESS_BOOT_DUTY;
}

void luce(uint8_t duty)
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(keyboard_BL_PIN, duty);
#else
    ledcWrite(KB_BRIGHTNESS_CH, duty);
#endif
    g_dutyAdesso = duty;
}

// ---- C3_INT ----
void alzaInt()
{
    if (!g_risparmio) return;
    g_intDaMs = millis();
    g_intAlto = true;
    digitalWrite(C3_INT_PIN, HIGH);
}

void abbassaInt()
{
    if (!g_intAlto) return;
    g_intAlto = false;
    digitalWrite(C3_INT_PIN, LOW);
}

void risparmio(bool on)
{
    if (on) {
        pinMode(C3_INT_PIN, OUTPUT);
        digitalWrite(C3_INT_PIN, LOW);
        g_intAlto = false;
        g_risparmio = true;
    } else {
        g_risparmio = false;
        g_intAlto = false;
        pinMode(C3_INT_PIN, INPUT);  // linea libera, come col firmware LilyGO
    }
}

void onReceive(int len)
{
    g_ultimaI2cMs = millis();
    while (Wire.available()) {
        int cmd = Wire.read();
        switch (cmd) {
        case LILYGO_KB_BRIGHTNESS_CMD: {
            int duty = Wire.read();
            if (duty >= 0) {
                kb_brightness_duty = duty;
                luce(duty);
                BL_state = duty > 0;
            }
        }
        break;
        case LILYGO_KB_ALT_B_BRIGHTNESS_CMD: {
            int duty = Wire.read();
            if (duty > 30) {
                kb_brightness_setting_duty = duty;
            }
        }
        break;
        case LILYGO_KB_MODE_RAW_CMD:
            rawMode = true;
            break;
        case LILYGO_KB_MODE_KEY_CMD:
            rawMode = false;
            break;
        case PIPPA_SALUTO_CMD:
            g_saluto = true;
            break;
        case PIPPA_RISPARMIO_CMD: {
            int on = Wire.read();
            risparmio(on > 0);
        }
        break;
        default:
            break;
        }
    }
}

void setup()
{
    // 40 MHz: il quarzo, senza PLL. L'I2C (slave) e la scansione non hanno bisogno di piu'.
    setCpuFrequencyMhz(CPU_MHZ);

    keyboard[0][0] = 'q';
    keyboard[0][1] = 'w';
    keyboard[0][2] = 0; // symbol
    keyboard[0][3] = 'a';
    keyboard[0][4] = 0; // ALT
    keyboard[0][5] = ' ';
    keyboard[0][6] = 0; // Mic

    keyboard[1][0] = 'e';
    keyboard[1][1] = 's';
    keyboard[1][2] = 'd';
    keyboard[1][3] = 'p';
    keyboard[1][4] = 'x';
    keyboard[1][5] = 'z';
    keyboard[1][6] = 0; // Left Shift

    keyboard[2][0] = 'r';
    keyboard[2][1] = 'g';
    keyboard[2][2] = 't';
    keyboard[2][3] = 0; // Right Shit
    keyboard[2][4] = 'v';
    keyboard[2][5] = 'c';
    keyboard[2][6] = 'f';

    keyboard[3][0] = 'u';
    keyboard[3][1] = 'h';
    keyboard[3][2] = 'y';
    keyboard[3][3] = 0; // Enter
    keyboard[3][4] = 'b';
    keyboard[3][5] = 'n';
    keyboard[3][6] = 'j';

    keyboard[4][0] = 'o';
    keyboard[4][1] = 'l';
    keyboard[4][2] = 'i';
    keyboard[4][3] = 0; // Backspace
    keyboard[4][4] = '$';
    keyboard[4][5] = 'm';
    keyboard[4][6] = 'k';

    keyboard_symbol[0][0] = '#';
    keyboard_symbol[0][1] = '1';
    keyboard_symbol[0][2] = 0;
    keyboard_symbol[0][3] = '*';
    keyboard_symbol[0][4] = 0;
    keyboard_symbol[0][5] = 0;
    keyboard_symbol[0][6] = '0';

    keyboard_symbol[1][0] = '2';
    keyboard_symbol[1][1] = '4';
    keyboard_symbol[1][2] = '5';
    keyboard_symbol[1][3] = '@';
    keyboard_symbol[1][4] = '8';
    keyboard_symbol[1][5] = '7';
    keyboard_symbol[1][6] = 0;

    keyboard_symbol[2][0] = '3';
    keyboard_symbol[2][1] = '/';
    keyboard_symbol[2][2] = '(';
    keyboard_symbol[2][3] = 0;
    keyboard_symbol[2][4] = '?';
    keyboard_symbol[2][5] = '9';
    keyboard_symbol[2][6] = '6';

    keyboard_symbol[3][0] = '_';
    keyboard_symbol[3][1] = ':';
    keyboard_symbol[3][2] = ')';
    keyboard_symbol[3][3] = 0;
    keyboard_symbol[3][4] = '!';
    keyboard_symbol[3][5] = ',';
    keyboard_symbol[3][6] = ';';

    keyboard_symbol[4][0] = '+';
    keyboard_symbol[4][1] = '"';
    keyboard_symbol[4][2] = '-';
    keyboard_symbol[4][3] = 0;
    keyboard_symbol[4][4] = 0;
    keyboard_symbol[4][5] = '.';
    keyboard_symbol[4][6] = '\'';

    Serial.begin(115200);

    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);
    Wire.begin((uint8_t)I2C_DEV_ADDR, SDA, SCL, 100000UL);

    luceInit();

    for (int x = 0; x < rowCount; x++) {
        pinMode(rows[x], INPUT);
    }
    for (int x = 0; x < colCount; x++) {
        pinMode(cols[x], INPUT_PULLUP);
    }
    pinMode(C3_INT_PIN, INPUT);  // finche' l'S3 non chiede il modo a basso consumo la linea resta libera
    Serial.println("Tastiera Pippaphone pronta");
}

// Senza tasti premuti, luce spenta e niente I2C da un po': si puo' dormire (solo nel modo a basso consumo).
bool puoDormire()
{
    if (!g_risparmio || g_intAlto || g_dutyAdesso != 0) return false;
    const uint32_t ora = millis();
    if (ora - g_ultimaI2cMs < RESTA_SVEGLIO_MS || ora - g_ultimoCambioMs < RESTA_SVEGLIO_MS) return false;
    for (int c = 0; c < colCount; c++)
        for (int r = 0; r < rowCount; r++)
            if (keys[c][r] || grezzo[c][r]) return false;
    return true;
}

// Matrice libera (righe e colonne in ingresso), come la lascia readMatrix.
void matriceARiposo()
{
    for (int r = 0; r < rowCount; r++) pinMode(rows[r], INPUT);
    for (int c = 0; c < colCount; c++) pinMode(cols[c], INPUT);
}

// Light sleep finche' un tasto (una riga che scende) o l'S3 (SDA che scende) non svegliano il chip.
void dormi()
{
    for (int c = 0; c < colCount; c++) {
        pinMode(cols[c], OUTPUT);
        digitalWrite(cols[c], LOW);
    }
    for (int r = 0; r < rowCount; r++) pinMode(rows[r], INPUT_PULLUP);
    delayMicroseconds(ASSESTAMENTO_US);
    for (int r = 0; r < rowCount; r++) {
        if (digitalRead(rows[r]) == LOW) {  // un tasto appena premuto: niente sonno
            matriceARiposo();
            return;
        }
    }
    // I pin restano come sono durante il sonno (ESP-IDF potrebbe isolarli: colonne, luce e C3_INT per aria).
    for (int c = 0; c < colCount; c++) gpio_sleep_sel_dis((gpio_num_t)cols[c]);
    gpio_sleep_sel_dis((gpio_num_t)keyboard_BL_PIN);
    gpio_sleep_sel_dis((gpio_num_t)C3_INT_PIN);
    gpio_sleep_sel_dis((gpio_num_t)SCL);
    for (int r = 0; r < rowCount; r++) gpio_wakeup_enable((gpio_num_t)rows[r], GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)SDA, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    Serial.flush();
    esp_light_sleep_start();
    for (int r = 0; r < rowCount; r++) gpio_wakeup_disable((gpio_num_t)rows[r]);
    gpio_wakeup_disable((gpio_num_t)SDA);
    matriceARiposo();
    // Svegliato dall'S3: resta sveglio per la sua transazione (ripetuta) e quelle dopo.
    g_ultimaI2cMs = millis();
}

void loop()
{
    readMatrix();
    printMatrix();

    // key 3,3 is the enter key
    if (keyPressed(3, 3)) {
        Serial.println();
        comdata = (char)0x0D;
        comdata_flag = true;
    }
    if (keyPressed(4, 3)) {
        Serial.println("backspace");
        comdata = (char)0x08;
        comdata_flag = true;
    }
    if (keyActive(0, 4) && keyPressed(3, 4)) { //Alt+B
        Serial.println("Alt+B");
        // If the software sets the duty cycle to 0, then the value set
        // by the ATL+B register is used to ensure that ALT+B can normally light up the backlight.
        if (BL_state) {
            BL_state = false;
            luce(0); //turn off
        } else {
            BL_state = true;
            if (kb_brightness_duty == 0) {
                luce(kb_brightness_setting_duty);
            } else {
                luce(kb_brightness_duty);
            }
        }
        comdata_flag = false;   //Don't send char
    }
    if (keyActive(0, 4) && keyPressed(2, 5)) { //Alt+C
        Serial.println("Alt+C");
        comdata = (char)0x0C;
        comdata_flag = true;
    }

    // C3_INT alta al massimo INT_MAX_MS, anche se l'S3 non legge.
    if (g_intAlto && millis() - g_intDaMs > INT_MAX_MS) abbassaInt();

    if (puoDormire()) dormi();
    else delay(SCANSIONE_MS);
}

void onRequest()
{
    g_ultimaI2cMs = millis();
    if (g_saluto) {
        g_saluto = false;
        const uint8_t risposta[5] = {0xA5, 0x5A, PIPPA_VERSIONE, PIPPA_FUNZIONI, 0x00};
        Wire.write(risposta, sizeof(risposta));
        return;
    }
    if(rawMode) {
        for(uint8_t col = 0; col < colCount; col++) {
            uint8_t val = 0;
            for(uint8_t row = 0; row < rowCount; row++) {
                val |= (lastValue[col][row] << row);
            }
            Wire.write(val);
        }
    }
    else {
        if (comdata_flag) {
            Wire.print(comdata);
            comdata_flag = false;
            Serial.print("comdata :");
            Serial.println(comdata);
        } else {
            Wire.print((char)0x00);
        }
    }
    abbassaInt();  // l'S3 ha letto
}

// Una lettura della matrice (circa 1 ms) e il filtro dei rimbalzi: un tasto cambia quando due letture di fila
// (SCANSIONE_MS l'una dall'altra) lo vedono uguale. Un cambiamento alza C3_INT.
void readMatrix()
{
    bool cambiato = false;
    // iterate the columns
    for (int colIndex = 0; colIndex < colCount; colIndex++) {
        // col: set to output to low
        uint8_t  curCol = cols[colIndex];
        pinMode(curCol, OUTPUT);
        digitalWrite(curCol, LOW);

        // row: interate through the rows
        for (int rowIndex = 0; rowIndex < rowCount; rowIndex++) {
            uint8_t  rowCol = rows[rowIndex];
            pinMode(rowCol, INPUT_PULLUP);
            delayMicroseconds(ASSESTAMENTO_US);  // LilyGO: delay(1); la pull-up carica la riga in pochi us

            bool buttonPressed = (digitalRead(rowCol) == LOW);
            const bool stabile = buttonPressed == grezzo[colIndex][rowIndex];
            grezzo[colIndex][rowIndex] = buttonPressed;
            const bool nuovo = stabile ? buttonPressed : keys[colIndex][rowIndex];

            changedValue[colIndex][rowIndex] = nuovo != keys[colIndex][rowIndex];
            if (changedValue[colIndex][rowIndex]) cambiato = true;
            keys[colIndex][rowIndex] = nuovo;
            lastValue[colIndex][rowIndex] = nuovo;
            pinMode(rowCol, INPUT);
        }
        // disable the column
        pinMode(curCol, INPUT);
    }
    if (cambiato) {
        g_ultimoCambioMs = millis();
        alzaInt();
    }
}

bool keyPressed(int colIndex, int rowIndex)
{
    return changedValue[colIndex][rowIndex] && keys[colIndex][rowIndex] == true;
}

bool keyActive(int colIndex, int rowIndex)
{
    return keys[colIndex][rowIndex] == true;
}

bool isPrintableKey(int colIndex, int rowIndex)
{
    return keyboard_symbol[colIndex][rowIndex] != 0 || keyboard[colIndex][rowIndex] != 0;
}

void printMatrix()
{
    for (int rowIndex = 0; rowIndex < rowCount; rowIndex++) {
        for (int colIndex = 0; colIndex < colCount; colIndex++) {
            // we only want to print if the key is pressed and it is a printable character
            if (keyPressed(colIndex, rowIndex) && isPrintableKey(colIndex, rowIndex)) {
                char toPrint;

                if (keyActive(0, 2)) {
                    toPrint = char(keyboard_symbol[colIndex][rowIndex]);
                } else {
                    toPrint = char(keyboard[colIndex][rowIndex]);
                }

                // keys 1,6 and 2,3 are Shift keys, so we want to upper case
                if (keyActive(1, 6) || keyActive(2, 3)) {
                    toPrint = (char)((int)toPrint - 32);
                }

                Serial.print(toPrint);

                comdata = toPrint;
                comdata_flag = true;

            }
        }
    }
}
#else
#include <Arduino.h>
void setup()
{
    Serial.begin(115200);
}
void loop()
{
    Serial.println("SKETCH ONLY FOR LILYGO-KEYBOARD");
    delay(1000);
}
#endif
