#include "scheda/audio.h"

#include <atomic>
#include <cmath>
#include <cstring>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "scheda/expander.h"
#include "scheda/pins.h"

namespace scheda {

namespace suoni {
const std::vector<Note> SQUILLO = {{1568, 90}, {0, 40}, {2093, 140}};
const std::vector<Note> CLASSICA = {{1319, 120}, {1175, 120}, {740, 240}, {831, 240}, {1109, 120}, {988, 120},
                                    {587, 240}, {659, 240}, {988, 120}, {880, 120}, {554, 240}, {659, 240},
                                    {880, 480}, {0, 800}};
const std::vector<Note> SOS = {{2000, 150}, {0, 60}, {2000, 150}, {0, 60}, {2000, 150}, {0, 200},
                               {2000, 400}, {0, 60}, {2000, 400}, {0, 60}, {2000, 400}, {0, 200},
                               {2000, 150}, {0, 60}, {2000, 150}, {0, 60}, {2000, 150}, {0, 700}};
const std::vector<Note> CLIC = {{3000, 8}};
const std::vector<Note> INVIO_OK = {{1760, 60}, {2349, 90}};
const std::vector<Note> ERRORE = {{440, 180}, {0, 40}, {330, 260}};
const std::vector<Note> PTT_INIZIO = {{1200, 60}, {1800, 60}};
const std::vector<Note> PTT_FINE = {{1800, 60}, {1200, 60}};
}  // namespace suoni

namespace {
const char* TAG = "audio";
constexpr int I2S_RATE = 16000;  // il microfono non scende sotto ~7 kHz: si lavora a 16 e si divide per 2
constexpr size_t FRAME_8K = 320;

i2s_chan_handle_t g_tx = nullptr, g_rx = nullptr;
SemaphoreHandle_t g_lock = nullptr;
TaskHandle_t g_playTask = nullptr, g_recTask = nullptr;

// Stato della riproduzione (protetto da g_lock).
std::vector<Note> g_notes;
bool g_loop = false;
std::vector<int16_t> g_pcm;
std::function<void()> g_pcmDone;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_playing{false};

// Stato della registrazione.
std::atomic<bool> g_recording{false};
std::atomic<bool> g_recStop{false};
uint32_t g_recMaxMs = 0;
std::function<void(const int16_t*, size_t)> g_onFrame;
std::function<void()> g_onEnd;

void amp(bool on) { expander().set(Exp::AMP_SD, on); }

// Scrive campioni mono a 16 kHz come stereo a 32 bit (canale sinistro).
void writeMono16k(const int16_t* s, size_t n, int volume) {
    int32_t buf[256 * 2];
    const int32_t gain = volume * volume;  // 0..100
    while (n) {
        size_t k = n > 256 ? 256 : n;
        for (size_t i = 0; i < k; i++) {
            int32_t v = int32_t(s[i]) * gain / 100;
            buf[2 * i] = v << 16;
            buf[2 * i + 1] = 0;
        }
        size_t written = 0;
        i2s_channel_write(g_tx, buf, k * 8, &written, portMAX_DELAY);
        s += k;
        n -= k;
    }
}

void playTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_playing = true;
        amp(true);
        vTaskDelay(pdMS_TO_TICKS(5));
        for (;;) {
            xSemaphoreTake(g_lock, portMAX_DELAY);
            std::vector<Note> notes = g_notes;
            std::vector<int16_t> pcm;
            pcm.swap(g_pcm);
            auto done = g_pcmDone;
            g_pcmDone = nullptr;
            bool loop = g_loop;
            g_notes.clear();
            g_stop = false;
            xSemaphoreGive(g_lock);
            const int vol = audio().volume();
            if (!pcm.empty()) {
                // 8 kHz -> 16 kHz con interpolazione lineare.
                int16_t up[512];
                for (size_t i = 0; i < pcm.size() && !g_stop; i += 256) {
                    size_t k = std::min<size_t>(256, pcm.size() - i);
                    for (size_t j = 0; j < k; j++) {
                        int16_t a = pcm[i + j];
                        int16_t b = (i + j + 1 < pcm.size()) ? pcm[i + j + 1] : a;
                        up[2 * j] = a;
                        up[2 * j + 1] = int16_t((int32_t(a) + b) / 2);
                    }
                    writeMono16k(up, 2 * k, vol);
                }
                if (done) done();
            }
            do {
                for (const Note& n : notes) {
                    if (g_stop) break;
                    const size_t total = size_t(I2S_RATE) * n.ms / 1000;
                    int16_t buf[256];
                    float phase = 0;
                    const float step = 2.0f * float(M_PI) * n.hz / I2S_RATE;
                    for (size_t i = 0; i < total && !g_stop; i += 256) {
                        size_t k = std::min<size_t>(256, total - i);
                        for (size_t j = 0; j < k; j++) {
                            // Onda quasi quadra (piu' udibile sull'altoparlante piccolo), con attacco morbido.
                            float s = n.hz ? std::sin(phase) : 0.0f;
                            s = s > 0.3f ? 0.8f : (s < -0.3f ? -0.8f : s * 2.6f);
                            const size_t pos = i + j;
                            const float env = pos < 80 ? pos / 80.0f : (total - pos < 80 ? (total - pos) / 80.0f : 1.0f);
                            buf[j] = int16_t(s * env * 12000);
                            phase += step;
                            if (phase > 2 * float(M_PI)) phase -= 2 * float(M_PI);
                        }
                        writeMono16k(buf, k, vol);
                    }
                }
            } while (loop && !g_stop && !notes.empty());
            xSemaphoreTake(g_lock, portMAX_DELAY);
            const bool more = !g_notes.empty() || !g_pcm.empty();
            xSemaphoreGive(g_lock);
            if (!more) break;
        }
        // Silenzio per svuotare il DMA, poi amplificatore spento.
        int16_t zero[256] = {};
        for (int i = 0; i < 8; i++) writeMono16k(zero, 256, 0);
        amp(false);
        g_playing = false;
    }
}

void recTask(void*) {
    int32_t raw[FRAME_8K * 2 * 2];  // 640 campioni a 16 kHz, stereo
    int16_t frame[FRAME_8K];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_recording = true;
        i2s_channel_enable(g_rx);
        // Scarta i primi 100 ms (il microfono si assesta).
        for (int i = 0; i < 3; i++) {
            size_t got = 0;
            i2s_channel_read(g_rx, raw, sizeof(raw), &got, pdMS_TO_TICKS(200));
        }
        uint32_t elapsed = 0;
        int32_t dc = 0;
        while (!g_recStop && elapsed < g_recMaxMs) {
            size_t got = 0;
            if (i2s_channel_read(g_rx, raw, sizeof(raw), &got, pdMS_TO_TICKS(200)) != ESP_OK) continue;
            const size_t samples16k = got / 8;
            size_t n = 0;
            int32_t peak = 0;
            for (size_t i = 0; i + 1 < samples16k && n < FRAME_8K; i += 2) {
                // 24 bit a sinistra in 32 bit; media di due campioni = filtro + dimezzamento.
                int32_t a = raw[2 * i] >> 14, b = raw[2 * (i + 1)] >> 14;
                int32_t v = (a + b) / 2;
                dc += (v - dc) / 64;  // toglie la componente continua
                v -= dc;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                frame[n++] = int16_t(v);
                peak = std::max(peak, std::abs(v));
            }
            audio().level_ = uint8_t(std::min<int32_t>(100, peak * 100 / 12000));
            if (g_onFrame && n == FRAME_8K) g_onFrame(frame, n);
            elapsed += 40;
        }
        i2s_channel_disable(g_rx);
        audio().level_ = 0;
        g_recording = false;
        if (g_onEnd) g_onEnd();
    }
}
}  // namespace

Audio& audio() {
    static Audio a;
    return a;
}

bool Audio::begin() {
    g_lock = xSemaphoreCreateMutex();
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    if (i2s_new_channel(&chan, &g_tx, &g_rx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S non disponibile");
        return false;
    }
    i2s_std_config_t std = {};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE);
    std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    std.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std.gpio_cfg.bclk = PIN_I2S_BCLK;
    std.gpio_cfg.ws = PIN_I2S_WS;
    std.gpio_cfg.dout = PIN_I2S_DOUT;
    std.gpio_cfg.din = PIN_I2S_DIN;
    i2s_channel_init_std_mode(g_tx, &std);
    i2s_channel_init_std_mode(g_rx, &std);
    i2s_channel_enable(g_tx);
    amp(false);
    xTaskCreatePinnedToCore(playTask, "audio_out", 4096, nullptr, 8, &g_playTask, 1);
    xTaskCreatePinnedToCore(recTask, "audio_in", 16384, nullptr, 9, &g_recTask, 1);
    return true;
}

void Audio::play(const std::vector<Note>& notes, bool loop) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_stop = true;  // interrompe cio' che sta suonando
    g_notes = notes;
    g_loop = loop;
    xSemaphoreGive(g_lock);
    xTaskNotifyGive(g_playTask);
}

void Audio::playPcm(std::vector<int16_t> pcm, std::function<void()> done) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_stop = true;
    g_pcm = std::move(pcm);
    g_pcmDone = std::move(done);
    g_notes.clear();
    g_loop = false;
    xSemaphoreGive(g_lock);
    xTaskNotifyGive(g_playTask);
}

void Audio::stop() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_stop = true;
    g_loop = false;
    g_notes.clear();
    g_pcm.clear();
    xSemaphoreGive(g_lock);
}

bool Audio::busy() const { return g_playing; }

void Audio::startRecording(uint32_t maxMs, std::function<void(const int16_t*, size_t)> onFrame,
                           std::function<void()> onEnd) {
    if (g_recording) return;
    g_recMaxMs = maxMs;
    g_onFrame = std::move(onFrame);
    g_onEnd = std::move(onEnd);
    g_recStop = false;
    xTaskNotifyGive(g_recTask);
}

void Audio::stopRecording() { g_recStop = true; }

bool Audio::recording() const { return g_recording; }

}  // namespace scheda
