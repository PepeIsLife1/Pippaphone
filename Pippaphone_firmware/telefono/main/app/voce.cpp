#include "app/voce.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
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

// ================================================================ chiamata in diretta
//
// Chi parla: ogni frame di 40 ms (28 bit di 700C) si accoda al pacchetto, uno dietro l'altro senza
// buchi; pieno il pacchetto (10 o 12 frame) parte. Chi ascolta: i pacchetti arrivano con ritardi un
// po' diversi, quindi si suonano a un ritmo fisso partendo dal primo del turno, piu' un margine
// (e un pacchetto in piu' con la ridondanza, perche' un pezzo perso arriva col successivo).
// Un pezzo che non c'e' al suo momento diventa silenzio.

namespace {
constexpr int CALL_JITTER_MS = 150;  // margine sui ritardi della rete
constexpr int CALL_LEAD_MS = 100;    // si decodifica un po' prima del momento di suonare
constexpr int CALL_SLOTS = 16;

std::atomic<bool> g_callOn{false};
std::atomic<bool> g_callTalking{false};
std::atomic<bool> g_hearing{false};
std::atomic<uint8_t> g_lostPct{0};
std::atomic<uint32_t> g_talkMs{0};
Voce::CallSink g_sink;
QueueHandle_t g_callQ = nullptr;
uint8_t g_fpp = 10;
uint8_t g_out[64];
size_t g_outBits = 0;
uint8_t g_outFrames = 0;

struct CallPkt {
    uint8_t turn, index, frames;
    bool last, copy, redundant;
    uint8_t len;
    uint8_t data[64];
    int64_t at;
};

// Accoda i primi 'bits' bit di src (dal piu' significativo) in dst, a partire dal bit pos.
void putBits(uint8_t* dst, size_t& pos, const uint8_t* src, int bits) {
    for (int i = 0; i < bits; i++, pos++) {
        if ((src[i >> 3] >> (7 - (i & 7))) & 1) dst[pos >> 3] |= uint8_t(0x80 >> (pos & 7));
    }
}
void getBits(const uint8_t* src, size_t& pos, uint8_t* dst, int bits) {
    std::memset(dst, 0, (bits + 7) / 8);
    for (int i = 0; i < bits; i++, pos++) {
        if ((src[pos >> 3] >> (7 - (pos & 7))) & 1) dst[i >> 3] |= uint8_t(0x80 >> (i & 7));
    }
}

void sendOut(bool last) {
    const size_t len = (g_outBits + 7) / 8;
    if (g_sink) g_sink(g_out, len, g_outFrames, last);
    std::memset(g_out, 0, sizeof(g_out));
    g_outBits = 0;
    g_outFrames = 0;
}

// Un breve "bip" quando l'altro ha finito di parlare: ora tocca a te.
void turnBeep() {
    int16_t b[8000 / 1000 * 120];
    const size_t n = sizeof(b) / sizeof(b[0]);
    for (size_t i = 0; i < n; i++) {
        const float env = i < 80 ? i / 80.0f : (n - i < 80 ? (n - i) / 80.0f : 1.0f);
        b[i] = int16_t(std::sin(2.0f * float(M_PI) * 1400.0f * float(i) / 8000.0f) * 5000.0f * env);
    }
    scheda::audio().streamPush(b, n);
}

void callTask(void*) {
    CODEC2* dec = codec2_create(CODEC2_MODE_700C);
    struct Slot {
        int index = -1;
        uint8_t frames = 0;
        uint8_t data[64];
    };
    static Slot slots[CALL_SLOTS];
    // 15 frame al massimo (anche il silenzio di un pacchetto perso sta qui).
    int16_t* pcm = static_cast<int16_t*>(heap_caps_malloc(15 * FRAME_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!pcm) pcm = static_cast<int16_t*>(malloc(15 * FRAME_SAMPLES * sizeof(int16_t)));
    int turn = -1;
    bool anchored = false, lastSeen = false;
    int next = 0, i0 = 0, lastIndex = 1000, slotMs = 400, delayMs = 0;
    int64_t t0 = 0, lastRx = 0;
    int played = 0, lost = 0;
    for (;;) {
        CallPkt p;
        const bool got = xQueueReceive(g_callQ, &p, pdMS_TO_TICKS(g_callOn ? 10 : 500)) == pdTRUE;
        if (!g_callOn) {
            turn = -1;
            anchored = false;
            g_hearing = false;
            continue;
        }
        const int64_t now = esp_timer_get_time() / 1000;
        if (got) {
            if (p.turn != turn) {  // l'altro ha cominciato un turno nuovo
                turn = p.turn;
                for (auto& s : slots) s.index = -1;
                anchored = lastSeen = false;
                lastIndex = 1000;
                played = lost = 0;
            }
            lastRx = now;
            if (p.frames && p.len <= sizeof(Slot::data) && (!anchored || p.index >= next)) {
                Slot& s = slots[p.index % CALL_SLOTS];
                if (s.index != p.index) {
                    s.index = p.index;
                    s.frames = p.frames;
                    std::memcpy(s.data, p.data, p.len);
                }
            }
            if (!p.copy && !anchored && p.frames) {
                // Primo pacchetto del turno: da qui il ritmo.
                anchored = true;
                i0 = p.index;
                t0 = p.at;
                slotMs = p.frames * 40;
                delayMs = (p.redundant ? slotMs : 0) + CALL_JITTER_MS;
                next = p.index;
                if (p.index > 0 && slots[(p.index - 1) % CALL_SLOTS].index == p.index - 1) next = p.index - 1;
            }
            if (p.last && !p.copy) {
                lastSeen = true;
                lastIndex = p.frames ? p.index : p.index - 1;
                if (!anchored) turnBeep();  // finito senza voce sentita: tocca comunque a te
            }
        }
        if (anchored) {
            for (;;) {
                const int64_t at = t0 + delayMs + int64_t(next - i0) * slotMs;
                if (now < at - CALL_LEAD_MS) break;
                Slot& s = slots[next % CALL_SLOTS];
                if (s.index == next) {
                    for (int f = 0; f < s.frames; f++) {
                        uint8_t bits[4];
                        size_t pos = size_t(f) * 28;
                        getBits(s.data, pos, bits, 28);
                        codec2_decode(dec, &pcm[f * FRAME_SAMPLES], bits);
                    }
                    scheda::audio().streamPush(pcm, size_t(s.frames) * FRAME_SAMPLES);
                    s.index = -1;
                    played++;
                } else if (now < at) {
                    break;  // aspetta fino all'ultimo
                } else {
                    // Perso: silenzio della stessa durata, cosi' il resto arriva al suo momento.
                    std::memset(pcm, 0, sizeof(int16_t) * size_t(slotMs) * 8);  // 8 campioni al ms
                    scheda::audio().streamPush(pcm, size_t(slotMs) * 8);
                    lost++;
                }
                next++;
                g_lostPct = uint8_t(played + lost ? lost * 100 / (played + lost) : 0);
                if (lastSeen && next > lastIndex) {
                    anchored = false;
                    turnBeep();
                    break;
                }
            }
            // Turno finito senza l'ultimo pacchetto: non arriva piu' niente.
            if (anchored && !lastSeen && now - lastRx > int64_t(slotMs) * 3 + delayMs) anchored = false;
        }
        g_hearing = anchored;
    }
}
}  // namespace

void Voce::callStart(CallSink sink) {
    if (g_callOn) return;
    g_sink = std::move(sink);
    if (!g_callQ) {
        g_callQ = xQueueCreate(16, sizeof(CallPkt));
        xTaskCreatePinnedToCore(callTask, "chiamata", 16384, nullptr, 7, nullptr, 1);
    }
    xQueueReset(g_callQ);
    stop();  // niente vocali mentre si parla
    scheda::audio().streamStart();
    g_lostPct = 0;
    g_callOn = true;
}

void Voce::callStop() {
    if (!g_callOn) return;
    g_callOn = false;
    if (g_callTalking) {
        g_callTalking = false;
        scheda::audio().stopRecording();
    }
    scheda::audio().streamStop();
    g_hearing = false;
}

bool Voce::callTalk(bool on, uint8_t framesPerPacket) {
    if (!on) {
        if (g_callTalking) scheda::audio().stopRecording();  // l'ultimo pacchetto parte alla fine
        return true;
    }
    if (!g_callOn || g_callTalking || scheda::audio().recording() || !g_enc) return false;
    g_fpp = framesPerPacket ? std::min<uint8_t>(framesPerPacket, 15) : 10;
    std::memset(g_out, 0, sizeof(g_out));
    g_outBits = 0;
    g_outFrames = 0;
    g_talkMs = 0;
    g_callTalking = true;
    scheda::audio().startRecording(
        pn::CALL_TURN_MAX_MS,
        [](const int16_t* pcm, size_t n) {
            if (n != FRAME_SAMPLES || !g_callTalking) return;
            uint8_t bits[8];
            codec2_encode(g_enc, bits, const_cast<int16_t*>(pcm));
            putBits(g_out, g_outBits, bits, 28);
            g_outFrames++;
            g_talkMs += 40;
            if (g_outFrames >= g_fpp) sendOut(false);
        },
        []() {
            sendOut(true);  // anche vuoto: dice all'altro che tocca a lui
            g_callTalking = false;
        });
    return true;
}

bool Voce::callTalking() const { return g_callTalking; }

uint32_t Voce::callTalkMs() const { return g_talkMs; }

void Voce::callReceived(uint8_t turn, uint8_t index, const uint8_t* data, size_t len, uint8_t frames,
                        const uint8_t* prev, size_t prevLen, uint8_t prevFrames, bool last, bool redundant) {
    if (!g_callOn || !g_callQ) return;
    const int64_t at = esp_timer_get_time() / 1000;
    CallPkt p{};
    p.turn = turn;
    p.redundant = redundant;
    p.at = at;
    if (prevFrames && index > 0 && prevLen <= sizeof(p.data)) {
        p.index = uint8_t(index - 1);
        p.frames = prevFrames;
        p.copy = true;
        p.len = uint8_t(prevLen);
        std::memcpy(p.data, prev, prevLen);
        xQueueSend(g_callQ, &p, 0);
    }
    if (len > sizeof(p.data)) return;
    p.index = index;
    p.frames = frames;
    p.copy = false;
    p.last = last;
    p.len = uint8_t(len);
    if (len) std::memcpy(p.data, data, len);
    xQueueSend(g_callQ, &p, 0);
}

bool Voce::callHearing() const { return g_hearing; }

uint8_t Voce::callLostPct() const { return g_lostPct; }

}  // namespace app
