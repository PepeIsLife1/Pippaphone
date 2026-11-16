#include "scheda/audio.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "scheda/pins.h"
#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: altoparlante su I2S0, microfono (codec ES7210) su I2S1 con MCLK; l'amplificatore
// non ha il pin di spegnimento.
#include "tdeck/es7210.h"
#else
#include "scheda/expander.h"
#endif

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
const std::vector<Note> SQUILLO_USCITA = {{425, 1000}, {0, 3000}};  // come il tono di chiamata italiano
// Sveglia: arpeggio che sale; timer: quattro bip. Suonano a ciclo finche' non si fermano.
const std::vector<Note> SVEGLIA = {{1047, 120}, {0, 40}, {1319, 120}, {0, 40}, {1568, 120}, {0, 40}, {2093, 260}, {0, 700}};
const std::vector<Note> TIMER = {{2637, 90}, {0, 70}, {2637, 90}, {0, 70}, {2637, 90}, {0, 70}, {2637, 90}, {0, 800}};
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

// Riproduzione continua (chiamate): coda circolare a 8 kHz, protetta da g_lock.
constexpr size_t STREAM_CAP = 8000 * 2;  // 2 secondi
int16_t* g_ring = nullptr;
size_t g_ringHead = 0, g_ringLen = 0;
std::atomic<bool> g_streaming{false};

// Stato della registrazione.
std::atomic<bool> g_recording{false};
std::atomic<bool> g_recStop{false};
uint32_t g_recMaxMs = 0;
std::function<void(const int16_t*, size_t)> g_onFrame;
std::function<void()> g_onEnd;

#if CONFIG_SCHEDA_TDECK_PLUS
void amp(bool) {}
constexpr int MIC_DIGITAL_GAIN = 2;  // in piu' dei 24 dB analogici dell'ES7210: da regolare
#else
void amp(bool on) { expander().set(Exp::AMP_SD, on); }
#endif

// Scrive campioni mono a 16 kHz come stereo a 32 bit (canale sinistro).
void writeMono16k(const int16_t* s, size_t n, int volume) {
    int32_t buf[256 * 2];
    const int32_t gain = volume * volume;  // 0..100
    while (n) {
        size_t k = n > 256 ? 256 : n;
        for (size_t i = 0; i < k; i++) {
            int32_t v = int32_t(s[i]) * gain / 100;
            buf[2 * i] = v << 16;
#if CONFIG_SCHEDA_TDECK_PLUS
            buf[2 * i + 1] = v << 16;  // T-Deck: non si sa quale canale ascolta il MAX98357A
#else
            buf[2 * i + 1] = 0;
#endif
        }
        size_t written = 0;
        i2s_channel_write(g_tx, buf, k * 8, &written, portMAX_DELAY);
        s += k;
        n -= k;
    }
}

#if CONFIG_SCHEDA_TDECK_PLUS
// Musica (T-Deck): coda mono alla frequenza del brano, protetta da g_lock. La suona playTask, che e'
// l'unico a scrivere sull'I2S: cambia lui la frequenza della cassa, tra un suono e l'altro.
constexpr size_t MUSIC_CAP = 48000;  // 1 secondo a 48 kHz
int16_t* g_mring = nullptr;
size_t g_mHead = 0, g_mLen = 0;
uint32_t g_musicRate = 44100;
std::atomic<bool> g_music{false};
std::atomic<bool> g_musicPaused{false};
// Cambia a ogni musicStart (brano nuovo o salto), musicStop e pausa: i campioni presi dalla coda prima del
// cambio non valgono piu' (con le cuffie USB-C il resto del blocco si butta, invece di suonare dopo).
std::atomic<uint32_t> g_musicGen{0};
// Solo playTask: l'ultimo blocco di musica e' andato all'uscita esterna. Sta fuori da musicLoop perche' deve
// restare anche quando un altro suono interrompe la musica: se intanto le cuffie se ne vanno, la musica riparte
// in pausa invece che dalla cassa.
bool g_musicaFuori = false;
int g_txRate = I2S_RATE;  // frequenza attuale della cassa (solo playTask)

void setTxRate(int rate) {
    if (rate == g_txRate) return;
    // Nel DMA (6 x 240 frame) c'e' ancora la musica appena scritta: disable non lo svuota ed enable
    // riparte dal primo, quindi si risentirebbe, alla frequenza nuova e fuori ordine. Prima la si fa
    // finire alla frequenza giusta spingendo dentro silenzio (1536 frame, piu' di tutto il DMA).
    static const int16_t zero[256] = {};
    for (int i = 0; i < 6; i++) writeMono16k(zero, 256, 0);
    i2s_channel_disable(g_tx);
    i2s_std_clk_config_t c = I2S_STD_CLK_DEFAULT_CONFIG(uint32_t(rate));
    i2s_channel_reconfig_std_clock(g_tx, &c);
    i2s_channel_enable(g_tx);
    g_txRate = rate;
}

// Musica piu' forte: la cassa del T-Deck e' piccola e col volume al massimo i brani restavano bassi.
// Si alzano di 6 dB e un limitatore abbassa solo le punte che supererebbero circa -1 dBFS (attacco
// sul campione stesso, quindi niente saturazione; rilascio in 150 ms). Solo la musica: suonerie,
// vocali e chiamate restano come prima.
struct Limitatore {
    float inv = 0;        // inviluppo delle punte, dopo il guadagno
    float rilascio = 0;   // per campione, dalla frequenza
    uint32_t hz = 0;
    int16_t passa(int16_t x, uint32_t rate) {
        constexpr float GUADAGNO = 2.0f;    // +6 dB
        constexpr float SOGLIA = 29000.0f;  // circa -1 dBFS
        if (rate != hz) {
            hz = rate;
            rilascio = expf(-1.0f / (0.15f * float(rate ? rate : 44100)));
        }
        const float y = float(x) * GUADAGNO;
        const float a = fabsf(y);
        // Prima cala, poi il massimo col campione: cosi' l'inviluppo non e' mai sotto il campione.
        inv *= rilascio;
        if (a > inv) inv = a;
        return int16_t(inv > SOGLIA ? y * (SOGLIA / inv) : y);
    }
};
Limitatore g_limitatore;  // solo playTask

// Altri suoni in attesa (melodia, vocale, chiamata): hanno la precedenza sulla musica.
bool otherPending() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const bool r = !g_notes.empty() || !g_pcm.empty() || g_streaming;
    xSemaphoreGive(g_lock);
    return r;
}

// Suona la musica finche' c'e' e nessun altro suono la interrompe. Con l'uscita esterna pronta (cuffie USB-C,
// app/cuffie.cpp) la musica va li', mono (le cuffie la mettono uguale a sinistra e a destra), senza
// setTxRate, Limitatore e I2S: la frequenza la converte l'uscita e il passo lo da' il clock della USB (spingi
// aspetta, come il DMA della cassa). Se le cuffie si staccano (o si spengono) mentre la musica andava li', anche
// durante un altro suono che l'ha interrotta, la musica va in pausa prima che un solo campione arrivi alla
// cassa (g_musicaFuori); l'interfaccia, con Ev::CUFFIE, mette in pausa anche il lettore. La decisione si prende
// a ogni blocco.
void musicLoop() {
    int16_t s[256];
    bool zittita = false;  // registrando: la coda dell'uscita esterna e' gia' stata svuotata
    while (g_music && !otherPending()) {
        UscitaEsterna* u = audio().uscitaEsterna();
        const bool fuori = u && u->pronta();
        if (g_musicaFuori && !fuori) {
            g_musicPaused = true;
            g_musicaFuori = false;
        }
        if (g_musicPaused || g_recording) {  // pausa, o si sta registrando col microfono
            // In pausa l'uscita esterna l'ha gia' svuotata musicPause; registrando tace subito anche li'.
            if (g_recording && fuori && !zittita) {
                u->svuota();
                zittita = true;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        zittita = false;
        if (!fuori) setTxRate(int(g_musicRate));
        size_t k = 0;
        xSemaphoreTake(g_lock, portMAX_DELAY);
        const uint32_t gen = g_musicGen;
        while (k < 256 && g_mLen) {
            s[k++] = g_mring[g_mHead];
            g_mHead = (g_mHead + 1) % MUSIC_CAP;
            g_mLen--;
        }
        xSemaphoreGive(g_lock);
        if (!k) {
            vTaskDelay(pdMS_TO_TICKS(5));  // coda vuota: l'I2S manda silenzio da solo (auto_clear)
            continue;
        }
        if (fuori) {
            // Fino a quando l'uscita prende tutto il blocco; se smette di essere pronta o se intanto la musica
            // e' cambiata (pausa, salto, stop) il resto del blocco si butta.
            g_musicaFuori = true;
            for (size_t fatti = 0; fatti < k && u->pronta() && g_musicGen == gen;)
                fatti += u->spingi(s + fatti, k - fatti, 1, g_musicRate, true);
            continue;
        }
        for (size_t i = 0; i < k; i++) s[i] = g_limitatore.passa(s[i], g_musicRate);
        writeMono16k(s, k, audio().volume());  // a qualunque frequenza: scrive e basta
    }
    // Un altro suono (sulla cassa) ferma la musica: quella in coda nelle cuffie sfuma invece di finire di colpo.
    if (g_musicaFuori) {
        if (UscitaEsterna* u = audio().uscitaEsterna()) u->svuota();
    }
    if (!g_music) g_musicaFuori = false;  // musica ferma (musicStop): il prossimo brano va dove si trova
}
#endif

void playTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#if CONFIG_SCHEDA_TDECK_PLUS
        musicLoop();
        if (!otherPending()) {
            setTxRate(I2S_RATE);  // musica finita: la cassa torna a 16 kHz
            continue;
        }
        setTxRate(I2S_RATE);
#endif
        g_playing = true;
        amp(true);
        vTaskDelay(pdMS_TO_TICKS(5));
        for (;;) {
            if (g_streaming) {
                // Chiamata: 20 ms alla volta dalla coda, silenzio se e' vuota. La scrittura sul DMA
                // aspetta, quindi il passo lo da' l'I2S.
                int16_t s8[160], up[320];
                int16_t last = 0;
                while (g_streaming) {
                    size_t k = 0;
                    xSemaphoreTake(g_lock, portMAX_DELAY);
                    while (k < 160 && g_ringLen) {
                        s8[k++] = g_ring[g_ringHead];
                        g_ringHead = (g_ringHead + 1) % STREAM_CAP;
                        g_ringLen--;
                    }
                    xSemaphoreGive(g_lock);
                    for (size_t j = k; j < 160; j++) s8[j] = 0;
                    for (size_t j = 0; j < 160; j++) {
                        // 8 kHz -> 16 kHz: il campione di mezzo e' la media con il precedente.
                        up[2 * j] = int16_t((int32_t(last) + s8[j]) / 2);
                        up[2 * j + 1] = s8[j];
                        last = s8[j];
                    }
                    writeMono16k(up, 320, audio().volume());
                }
                continue;
            }
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
            const bool more = !g_notes.empty() || !g_pcm.empty() || g_streaming;
            xSemaphoreGive(g_lock);
            if (!more) break;
        }
        // Silenzio per svuotare il DMA, poi amplificatore spento.
        int16_t zero[256] = {};
        for (int i = 0; i < 8; i++) writeMono16k(zero, 256, 0);
        amp(false);
        g_playing = false;
#if CONFIG_SCHEDA_TDECK_PLUS
        if (g_music) xTaskNotifyGive(g_playTask);  // finiti gli altri suoni: riparte la musica
#endif
    }
}

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck: 16 bit stereo dall'ES7210, MIC1 a sinistra e MIC2 a destra. Si usa il canale col segnale
// piu' forte (uno dei due potrebbe non essere montato), scelto con un po' di memoria.
void recTask(void*) {
    int16_t raw[FRAME_8K * 2 * 2];  // 640 campioni a 16 kHz, stereo
    int16_t frame[FRAME_8K];
    int ch = 0;
    int32_t levL = 0, levR = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        g_recording = true;
        i2s_channel_enable(g_rx);
        es7210::start();
        // Scarta i primi 100 ms (il codec e il microfono si assestano).
        for (int i = 0; i < 3; i++) {
            size_t got = 0;
            i2s_channel_read(g_rx, raw, sizeof(raw), &got, pdMS_TO_TICKS(200));
        }
        uint32_t elapsed = 0;
        int32_t dc = 0;
        while (!g_recStop && elapsed < g_recMaxMs) {
            size_t got = 0;
            if (i2s_channel_read(g_rx, raw, sizeof(raw), &got, pdMS_TO_TICKS(200)) != ESP_OK) continue;
            const size_t samples16k = got / 4;
            int64_t eL = 0, eR = 0;
            for (size_t i = 0; i < samples16k; i++) {
                eL += std::abs(raw[2 * i]);
                eR += std::abs(raw[2 * i + 1]);
            }
            if (samples16k) {
                levL += int32_t(eL / int64_t(samples16k) - levL) / 8;
                levR += int32_t(eR / int64_t(samples16k) - levR) / 8;
                if (levR > levL * 2 + 20) ch = 1;
                else if (levL > levR * 2 + 20) ch = 0;
            }
            size_t n = 0;
            int32_t peak = 0;
            for (size_t i = 0; i + 1 < samples16k && n < FRAME_8K; i += 2) {
                // Media di due campioni = filtro + dimezzamento a 8 kHz.
                int32_t v = (int32_t(raw[2 * i + ch]) + raw[2 * (i + 1) + ch]) / 2 * MIC_DIGITAL_GAIN;
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
        es7210::stop();
        i2s_channel_disable(g_rx);
        audio().level_ = 0;
        g_recording = false;
        if (g_onEnd) g_onEnd();
    }
}
#else
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
#endif
}  // namespace

Audio& audio() {
    static Audio a;
    return a;
}

#if CONFIG_SCHEDA_TDECK_PLUS
bool Audio::begin() {
    g_lock = xSemaphoreCreateMutex();
    // Altoparlante: I2S0, 16 kHz, stereo a 32 bit, senza MCLK.
    i2s_chan_config_t txc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    txc.auto_clear = true;
    if (i2s_new_channel(&txc, &g_tx, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "I2S dell'altoparlante non disponibile");
        return false;
    }
    i2s_std_config_t tx = {};
    tx.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE);
    tx.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    tx.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    tx.gpio_cfg.bclk = PIN_I2S_BCLK;
    tx.gpio_cfg.ws = PIN_I2S_WS;
    tx.gpio_cfg.dout = PIN_I2S_DOUT;
    tx.gpio_cfg.din = I2S_GPIO_UNUSED;
    i2s_channel_init_std_mode(g_tx, &tx);
    i2s_channel_enable(g_tx);

    // Microfono: I2S1, 16 kHz, stereo a 16 bit, MCLK = 256 x 16 kHz verso l'ES7210.
    i2s_chan_config_t rxc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    bool mic = i2s_new_channel(&rxc, nullptr, &g_rx) == ESP_OK;
    if (mic) {
        i2s_std_config_t rx = {};
        rx.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE);
        rx.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        rx.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        rx.gpio_cfg.mclk = PIN_MIC_MCLK;
        rx.gpio_cfg.bclk = PIN_MIC_SCK;
        rx.gpio_cfg.ws = PIN_MIC_LRCK;
        rx.gpio_cfg.dout = I2S_GPIO_UNUSED;
        rx.gpio_cfg.din = PIN_MIC_DIN;
        i2s_channel_init_std_mode(g_rx, &rx);
        // Il codec si configura con MCLK presente, poi si spegne fino alla prima registrazione.
        i2s_channel_enable(g_rx);
        mic = es7210::begin();
        es7210::stop();
        i2s_channel_disable(g_rx);
    } else {
        ESP_LOGE(TAG, "I2S del microfono non disponibile");
    }
    amp(false);
    xTaskCreatePinnedToCore(playTask, "audio_out", 6144, nullptr, 8, &g_playTask, 1);
    xTaskCreatePinnedToCore(recTask, "audio_in", 16384, nullptr, 9, &g_recTask, 1);
    return mic;
}
#else
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
    xTaskCreatePinnedToCore(playTask, "audio_out", 6144, nullptr, 8, &g_playTask, 1);
    xTaskCreatePinnedToCore(recTask, "audio_in", 16384, nullptr, 9, &g_recTask, 1);
    return true;
}
#endif

void Audio::play(const std::vector<Note>& notes, bool loop) {
    if (g_streaming) return;  // durante una chiamata si sente solo la chiamata
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_stop = true;  // interrompe cio' che sta suonando
    g_notes = notes;
    g_loop = loop;
    xSemaphoreGive(g_lock);
    xTaskNotifyGive(g_playTask);
}

void Audio::playPcm(std::vector<int16_t> pcm, std::function<void()> done) {
    if (g_streaming) {
        if (done) done();
        return;
    }
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

void Audio::streamStart() {
    if (!g_ring) {
        g_ring = static_cast<int16_t*>(heap_caps_malloc(STREAM_CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM));
        if (!g_ring) g_ring = static_cast<int16_t*>(malloc(STREAM_CAP * sizeof(int16_t)));
        if (!g_ring) {
            ESP_LOGE(TAG, "niente memoria per l'audio della chiamata");
            return;
        }
    }
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_ringHead = g_ringLen = 0;
    g_stop = true;  // interrompe squilli e melodie
    g_loop = false;
    g_notes.clear();
    g_pcm.clear();
    g_streaming = true;
    xSemaphoreGive(g_lock);
    xTaskNotifyGive(g_playTask);
}

void Audio::streamPush(const int16_t* pcm, size_t n) {
    if (!g_streaming || !g_ring) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    for (size_t i = 0; i < n && g_ringLen < STREAM_CAP; i++) {
        g_ring[(g_ringHead + g_ringLen) % STREAM_CAP] = pcm[i];
        g_ringLen++;
    }
    xSemaphoreGive(g_lock);
}

void Audio::streamStop() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_streaming = false;
    g_ringHead = g_ringLen = 0;
    xSemaphoreGive(g_lock);
}

bool Audio::streaming() const { return g_streaming; }

uint32_t Audio::streamQueuedMs() const { return uint32_t(g_ringLen / (RATE / 1000)); }

#if CONFIG_SCHEDA_TDECK_PLUS
bool Audio::musicStart(uint32_t rate) {
    if (rate < 8000 || rate > 48000) return false;
    if (!g_mring) {
        g_mring = static_cast<int16_t*>(heap_caps_malloc(MUSIC_CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM));
        if (!g_mring) {
            ESP_LOGE(TAG, "niente memoria per la musica");
            return false;
        }
    }
    // La pausa resta com'e' (la toglie chi fa partire un brano): un salto fatto in pausa passa di
    // qui e non deve far ripartire la musica.
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_mHead = g_mLen = 0;
    g_musicRate = rate;
    g_music = true;
    g_musicGen++;
    xSemaphoreGive(g_lock);
    // Brano nuovo o salto: anche quello gia' nelle cuffie USB-C se ne va (sfumando).
    if (UscitaEsterna* u = uscitaEsterna()) u->svuota();
    xTaskNotifyGive(g_playTask);
    return true;
}

size_t Audio::musicPush(const int16_t* mono, size_t n, uint32_t waitMs) {
    size_t done = 0;
    const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(waitMs);
    while (done < n && g_music) {
        xSemaphoreTake(g_lock, portMAX_DELAY);
        while (done < n && g_mLen < MUSIC_CAP) {
            g_mring[(g_mHead + g_mLen) % MUSIC_CAP] = mono[done++];
            g_mLen++;
        }
        xSemaphoreGive(g_lock);
        if (done < n) {
            if (int32_t(xTaskGetTickCount() - until) >= 0) break;
            vTaskDelay(pdMS_TO_TICKS(10));  // coda piena: si aspetta che la cassa ne consumi un po'
        }
    }
    return done;
}

void Audio::musicPause(bool paused) {
    g_musicPaused = paused;
    if (!paused) return;
    // In pausa la musica nelle cuffie USB-C si ferma subito (sfumando), non dopo la loro coda (circa 90 ms).
    g_musicGen++;
    if (UscitaEsterna* u = uscitaEsterna()) u->svuota();
}

void Audio::musicStop() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_music = false;
    g_mHead = g_mLen = 0;
    g_musicGen++;
    xSemaphoreGive(g_lock);
    if (UscitaEsterna* u = uscitaEsterna()) u->svuota();
    xTaskNotifyGive(g_playTask);
}

bool Audio::musicActive() const { return g_music; }

uint32_t Audio::musicQueuedMs() const { return g_musicRate ? uint32_t(uint64_t(g_mLen) * 1000 / g_musicRate) : 0; }
#else
// PCB: niente musica (manca la microSD nel firmware).
bool Audio::musicStart(uint32_t) { return false; }
size_t Audio::musicPush(const int16_t*, size_t, uint32_t) { return 0; }
void Audio::musicPause(bool) {}
void Audio::musicStop() {}
bool Audio::musicActive() const { return false; }
uint32_t Audio::musicQueuedMs() const { return 0; }
#endif

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
