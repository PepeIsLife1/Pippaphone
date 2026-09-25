#include "app/voce.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include "app/memoria.h"
#include "codec2.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pippanet/payloads.h"
#include "scheda/audio.h"

namespace app {

namespace {
const char* TAG = "voce";
constexpr int FRAME_SAMPLES = 320;  // 40 ms a 8 kHz
constexpr int FRAME_BYTES = 4;      // 28 bit arrotondati

CODEC2* g_enc = nullptr;
std::mutex g_mu;
std::vector<uint8_t> g_rec;
std::atomic<uint32_t> g_recMs{0};
std::atomic<bool> g_recDone{false};

QueueHandle_t g_playQ = nullptr;
struct PlayReq {
    uint32_t file;
    std::function<void()>* done;
};
std::atomic<bool> g_playing{false};
std::atomic<uint32_t> g_playFile{0};
int64_t g_playStartUs = 0;

// Decodifica e riproduce in un compito a parte (1-2 s di calcolo per 15 s di voce).
void playTask(void*) {
    CODEC2* dec = codec2_create(CODEC2_MODE_700C);
    for (;;) {
        PlayReq req;
        if (xQueueReceive(g_playQ, &req, portMAX_DELAY) != pdTRUE) continue;
        std::vector<uint8_t> f;
        if (!readFile(std::string(DATI) + "/voce/" + std::to_string(req.file) + ".c2", f) || f.size() < 7) {
            if (req.done) {
                (*req.done)();
                delete req.done;
            }
            continue;
        }
        uint32_t missing = uint32_t(f[3]) | (uint32_t(f[4]) << 8) | (uint32_t(f[5]) << 16) | (uint32_t(f[6]) << 24);
        const uint8_t* data = f.data() + 7;
        const size_t frames = (f.size() - 7) / FRAME_BYTES;
        std::vector<int16_t> pcm;
        pcm.resize(frames * FRAME_SAMPLES);
        const size_t framesPerChunk = pn::VOICE_CHUNK / FRAME_BYTES;
        for (size_t i = 0; i < frames; i++) {
            const size_t chunk = i / framesPerChunk;
            if (chunk < 32 && (missing & (1u << chunk))) {
                std::memset(&pcm[i * FRAME_SAMPLES], 0, FRAME_SAMPLES * sizeof(int16_t));  // pezzo perso: silenzio
                continue;
            }
            codec2_decode(dec, &pcm[i * FRAME_SAMPLES], data + i * FRAME_BYTES);
        }
        g_playFile = req.file;
        g_playing = true;
        g_playStartUs = esp_timer_get_time();
        std::function<void()>* done = req.done;
        scheda::audio().playPcm(std::move(pcm), [done]() {
            g_playing = false;
            if (done) {
                (*done)();
                delete done;
            }
        });
    }
}
}  // namespace

Voce& voce() {
    static Voce v;
    return v;
}

bool Voce::begin() {
    g_enc = codec2_create(CODEC2_MODE_700C);
    if (!g_enc) return false;
    g_playQ = xQueueCreate(2, sizeof(PlayReq));
    xTaskCreatePinnedToCore(playTask, "voce", 16384, nullptr, 6, nullptr, 1);
    ESP_LOGI(TAG, "Codec2 700C: %d campioni, %d byte per frame", codec2_samples_per_frame(g_enc),
             codec2_bytes_per_frame(g_enc));
    return true;
}

bool Voce::startRecording() {
    if (scheda::audio().recording() || !g_enc) return false;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_rec.clear();
        g_rec.reserve(VOICE_MAX_MS / 40 * FRAME_BYTES);
    }
    g_recMs = 0;
    g_recDone = false;
    scheda::audio().startRecording(
        VOICE_MAX_MS,
        [](const int16_t* pcm, size_t n) {
            if (n != FRAME_SAMPLES) return;
            uint8_t bits[8];
            codec2_encode(g_enc, bits, const_cast<int16_t*>(pcm));
            std::lock_guard<std::mutex> l(g_mu);
            g_rec.insert(g_rec.end(), bits, bits + FRAME_BYTES);
            g_recMs += 40;
        },
        []() { g_recDone = true; });
    return true;
}

void Voce::stopRecording() { scheda::audio().stopRecording(); }

bool Voce::recording() const { return scheda::audio().recording(); }

uint32_t Voce::recordedMs() const { return g_recMs; }

std::vector<uint8_t> Voce::takeRecording(uint16_t& durationMs) {
    std::lock_guard<std::mutex> l(g_mu);
    durationMs = uint16_t(g_recMs.load());
    std::vector<uint8_t> r;
    r.swap(g_rec);
    return r;
}

void Voce::play(uint32_t file, std::function<void()> done) {
    stop();
    PlayReq r{file, done ? new std::function<void()>(std::move(done)) : nullptr};
    if (xQueueSend(g_playQ, &r, 0) != pdTRUE && r.done) delete r.done;
}

void Voce::stop() {
    if (g_playing) scheda::audio().stop();
    g_playing = false;
}

bool Voce::playing() const { return g_playing; }

uint32_t Voce::playedMs() const {
    return g_playing ? uint32_t((esp_timer_get_time() - g_playStartUs) / 1000) : 0;
}

uint32_t Voce::playingFile() const { return g_playFile; }

}  // namespace app
