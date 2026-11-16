#include "pippa_radio/esp_hal.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pippa {

namespace {
constexpr uint32_t HAL_INPUT = 0x01, HAL_OUTPUT = 0x03, HAL_LOW = 0, HAL_HIGH = 1, HAL_RISING = 0x01,
                   HAL_FALLING = 0x02;
bool isrServiceInstalled = false;

void IRAM_ATTR trampoline(void* arg) { reinterpret_cast<void (*)(void)>(arg)(); }
}  // namespace

EspHal::EspHal(spi_host_device_t host, int sck, int miso, int mosi, bool initBus, int clockHz)
    : RadioLibHal(HAL_INPUT, HAL_OUTPUT, HAL_LOW, HAL_HIGH, HAL_RISING, HAL_FALLING),
      host_(host),
      sck_(sck),
      miso_(miso),
      mosi_(mosi),
      initBus_(initBus),
      clockHz_(clockHz) {}

void EspHal::init() {
    if (initBus_) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = mosi_;
        bus.miso_io_num = miso_;
        bus.sclk_io_num = sck_;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 512;
        ESP_ERROR_CHECK(spi_bus_initialize(host_, &bus, SPI_DMA_CH_AUTO));
    }
    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = clockHz_;
    dev.spics_io_num = -1;  // il chip select lo gestisce RadioLib
    dev.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(host_, &dev, &dev_));
}

void EspHal::term() {
    if (dev_) spi_bus_remove_device(dev_);
    dev_ = nullptr;
}

void EspHal::pinMode(uint32_t pin, uint32_t mode) {
    if (pin == RADIOLIB_NC) return;
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << pin;
    c.mode = mode == HAL_OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    c.pull_up_en = GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&c);
}

void EspHal::digitalWrite(uint32_t pin, uint32_t value) {
    if (pin == RADIOLIB_NC) return;
    gpio_set_level(gpio_num_t(pin), value);
}

uint32_t EspHal::digitalRead(uint32_t pin) {
    if (pin == RADIOLIB_NC) return 0;
    return gpio_get_level(gpio_num_t(pin));
}

void EspHal::attachInterrupt(uint32_t interruptNum, void (*cb)(void), uint32_t mode) {
    if (interruptNum == RADIOLIB_NC) return;
    if (!isrServiceInstalled) {
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        isrServiceInstalled = true;
    }
    gpio_num_t pin = gpio_num_t(interruptNum);
    gpio_set_intr_type(pin, mode == HAL_RISING ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(pin, trampoline, reinterpret_cast<void*>(cb));
    gpio_intr_enable(pin);
}

void EspHal::detachInterrupt(uint32_t interruptNum) {
    if (interruptNum == RADIOLIB_NC) return;
    gpio_num_t pin = gpio_num_t(interruptNum);
    gpio_isr_handler_remove(pin);
    gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
}

void EspHal::delay(RadioLibTime_t ms) {
    if (ms == 0) {
        taskYIELD();
        return;
    }
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t ? t : 1);
}

void EspHal::delayMicroseconds(RadioLibTime_t us) { esp_rom_delay_us(uint32_t(us)); }

RadioLibTime_t EspHal::millis() { return RadioLibTime_t(esp_timer_get_time() / 1000); }

RadioLibTime_t EspHal::micros() { return RadioLibTime_t(esp_timer_get_time()); }

long EspHal::pulseIn(uint32_t, uint32_t, RadioLibTime_t) { return 0; }

void EspHal::spiBeginTransaction() {
    if (busTake_) busTake_();
    spi_device_acquire_bus(dev_, portMAX_DELAY);
}

void EspHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;
    spi_device_polling_transmit(dev_, &t);
}

void EspHal::spiEndTransaction() {
    spi_device_release_bus(dev_);
    if (busGive_) busGive_();
}

}  // namespace pippa
