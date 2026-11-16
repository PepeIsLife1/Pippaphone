#include "app/musica.h"

#include "sdkconfig.h"

#if CONFIG_SCHEDA_TDECK_PLUS
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "minimp3.h"
#include "scheda/audio.h"

namespace app {

namespace {
const char* TAG = "musica";
const char* const CARTELLA = "/sd/musica";
constexpr size_t MAX_BRANI = 500;
constexpr size_t SU_CAP = 4096;  // campioni interpolati per giro (8 kB in PSRAM)

// Il MAX98357A del T-Deck accetta solo 8, 16, 32, 44,1 e 48 kHz (88,2 e 96 qui non servono): un brano a
// un'altra frequenza si suona a un suo multiplo intero che sia fra queste, interpolando (11,025 e
// 22,05 kHz -> 44,1; 12 e 24 -> 48). Restituisce il fattore (1: va bene cosi'), 0 se non c'e'.
uint32_t fattoreUscita(uint32_t hz) {
    static const uint32_t CASSA[] = {8000, 16000, 32000, 44100, 48000};
    for (uint32_t k = 1; k <= 6; k++)
        for (const uint32_t c : CASSA)
            if (hz * k == c) return k;
    return 0;
}

bool finisceCon(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(uint8_t(s[s.size() - n + i])) != ext[i]) return false;
    return true;
}

void scan(const std::string& dir, const std::string& prefisso, std::vector<Brano>& out, int livello) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;
        const std::string p = dir + "/" + n;
        // Cartella o file lo dice gia' readdir (la FATFS di ESP-IDF mette DT_DIR o DT_REG): niente
        // stat(), che con FatFs rilegge la cartella dall'inizio a ogni voce.
        if (e->d_type == DT_DIR) {
            if (livello == 0) scan(p, n + "/", out, 1);
            continue;
        }
        if (!finisceCon(n, ".mp3") && !finisceCon(n, ".wav")) continue;
        Brano b;
        b.path = p;
        b.nome = prefisso + n.substr(0, n.size() - 4);
        std::replace(b.nome.begin(), b.nome.end(), '_', ' ');
        out.push_back(b);
        if (out.size() >= MAX_BRANI) break;
    }
    closedir(d);
}

void utf8(std::string& s, uint32_t c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) {
        s += char(0xC0 | (c >> 6));
        s += char(0x80 | (c & 0x3F));
    } else {
        s += char(0xE0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 0x3F));
        s += char(0x80 | (c & 0x3F));
    }
}

// Testo di un frame ID3 (codifica nel primo byte: 0 Latin-1, 1 UTF-16 con BOM, 2 UTF-16BE, 3 UTF-8).
std::string testoId3(const uint8_t* d, size_t n) {
    if (!n) return "";
    const uint8_t enc = d[0];
    d++;
    n--;
    std::string s;
    if (enc == 0 || enc == 3) {
        for (size_t i = 0; i < n && d[i]; i++) {
            if (enc == 3) s += char(d[i]);
            else utf8(s, d[i]);
        }
    } else {
        bool be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            be = d[0] == 0xFE && d[1] == 0xFF;
            i = 2;
        }
        for (; i + 1 < n; i += 2) {
            const uint32_t c = be ? (uint32_t(d[i]) << 8 | d[i + 1]) : (uint32_t(d[i + 1]) << 8 | d[i]);
            if (!c) break;
            if (c >= 0xD800 && c <= 0xDFFF) continue;  // fuori dal piano base: si salta
            utf8(s, c);
        }
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

uint32_t syncsafe(const uint8_t* p) { return uint32_t(p[0] & 0x7F) << 21 | uint32_t(p[1] & 0x7F) << 14 | uint32_t(p[2] & 0x7F) << 7 | (p[3] & 0x7F); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// Tag ID3v2 in testa al file: titolo e artista; restituisce dove comincia l'audio (0 se non c'e').
long leggiId3(FILE* f, std::string& titolo, std::string& artista) {
    uint8_t h[10];
    fseek(f, 0, SEEK_SET);
    if (fread(h, 1, 10, f) != 10 || std::memcmp(h, "ID3", 3) != 0) return 0;
    const uint8_t ver = h[3], flags = h[5];
    const uint32_t size = syncsafe(h + 6);
    const long totale = 10 + long(size) + ((flags & 0x10) ? 10 : 0);
    if (ver < 3 || ver > 4) return totale;  // v2.2 (id di 3 lettere): solo si salta
    const size_t n = std::min<uint32_t>(size, 64 * 1024);
    std::vector<uint8_t> t(n);
    if (fread(t.data(), 1, n, f) != n) return totale;
    size_t p = 0;
    if ((flags & 0x40) && n >= 4) {
        // Intestazione estesa (in v2.3 la dimensione non conta i suoi 4 byte): conto a 64 bit e al
        // massimo n, perche' con una dimensione assurda p non deve fare il giro.
        const uint64_t e = ver == 4 ? syncsafe(t.data()) : uint64_t(be32(t.data())) + 4;
        p = e < n ? size_t(e) : n;
    }
    while (p + 10 <= n) {
        if (!t[p]) break;  // riempimento
        const uint32_t fs = ver == 4 ? syncsafe(&t[p + 4]) : be32(&t[p + 4]);
        // In v2.3 la dimensione usa tutti i 32 bit: si confronta con quel che resta (p + 10 <= n),
        // senza somme che fanno il giro e lascerebbero p fermo o lo manderebbero fuori dal buffer.
        if (!fs || fs > n - p - 10) break;
        if (!std::memcmp(&t[p], "TIT2", 4)) titolo = testoId3(&t[p + 10], fs);
        else if (!std::memcmp(&t[p], "TPE1", 4)) artista = testoId3(&t[p + 10], fs);
        p += 10 + fs;
    }
    return totale;
}

// MP3 VBR: il primo frame puo' essere quello "Xing" (o "Info") scritto da LAME e simili, con il numero
// dei frame del brano. Il testo sta dopo l'intestazione (4 byte), il CRC se c'e' (2) e le informazioni
// laterali (MPEG-1: 32 byte, 17 se mono; MPEG-2 e 2.5: 17, 9 se mono). 0 se non c'e'.
uint32_t frameXing(const uint8_t* fr, size_t n) {
    if (n < 4) return 0;
    const bool mpeg1 = (fr[1] & 0x08) != 0, mono = (fr[3] & 0xC0) == 0xC0, crc = (fr[1] & 0x01) == 0;
    const size_t o = 4 + (crc ? 2 : 0) + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
    if (o + 12 > n) return 0;
    if (std::memcmp(fr + o, "Xing", 4) != 0 && std::memcmp(fr + o, "Info", 4) != 0) return 0;
    if (!(fr[o + 7] & 0x01)) return 0;  // il numero dei frame non c'e'
    return be32(fr + o + 8);
}

void trampolino(void*) { musica().compito(); }
}  // namespace

Musica& musica() {
    static Musica m;
    return m;
}

std::vector<Brano> Musica::elenco(bool rileggi) {
    if (letti_ && !rileggi) return brani_;
    // Cartella che manca (ENOENT) o microSD che non risponde (tolta e rimessa a telefono acceso: la
    // FatFs da' errore di disco, e "0 brani" sembrerebbe una cartella vuota).
    errno = 0;
    if (DIR* d = opendir(CARTELLA)) {
        closedir(d);
        sdNonRisponde_ = false;
    } else {
        sdNonRisponde_ = errno != ENOENT;
    }
    std::vector<Brano> out;
    scan(CARTELLA, "", out, 0);
    std::sort(out.begin(), out.end(), [](const Brano& a, const Brano& b) {
        return std::lexicographical_compare(a.nome.begin(), a.nome.end(), b.nome.begin(), b.nome.end(),
                                            [](char x, char y) { return std::tolower(uint8_t(x)) < std::tolower(uint8_t(y)); });
    });
    // Mentre suona non si cambia l'elenco (il compito lo sta usando).
    if (!attiva_) brani_ = out;
    letti_ = true;
    ESP_LOGI(TAG, "%u brani in %s", unsigned(out.size()), CARTELLA);
    return out;
}

Brano Musica::brano() const {
    Brano b;
    const int i = indice_;
    if (i >= 0 && i < int(brani_.size())) b = brani_[size_t(i)];
    b.titolo = titolo_;
    b.artista = artista_;
    return b;
}

uint32_t Musica::posizioneS() const {
    // Una lettura sola: il compito della musica (sull'altro core) lo azzera a fine brano, e fra il
    // controllo e la divisione si dividerebbe per zero (riavvio).
    const uint32_t r = rate_;
    if (!r) return 0;
    const uint64_t coda = uint64_t(scheda::audio().musicQueuedMs()) * r / 1000;
    return uint32_t((campioni_ > coda ? campioni_ - coda : 0) / r);
}

void Musica::suona(int indice) {
    if (indice < 0 || indice >= int(brani_.size())) return;
    if (!task_) {
        TaskHandle_t h = nullptr;
        // Stack in PSRAM: minimp3 ne usa ~16 kB e la RAM interna serve ad altro. Il compito legge
        // solo la microSD (niente scritture nella flash interna, che spengono la cache).
        if (xTaskCreatePinnedToCoreWithCaps(trampolino, "musica", 24 * 1024, nullptr, 6, &h, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
            errore_ = "memoria finita";
            return;
        }
        task_ = h;
    }
    errore_.clear();
    pausa_ = false;
    scheda::audio().musicPause(false);
    cmdIndice_ = indice;
    cmd_ = SUONA;
    attiva_ = true;
    indice_ = indice;
    xTaskNotifyGive(static_cast<TaskHandle_t>(task_));
}

void Musica::pausa(bool p) {
    if (!attiva_) return;
    pausa_ = p;
    scheda::audio().musicPause(p);
}

void Musica::ferma() {
    if (!task_) return;
    cmd_ = FERMA;
    xTaskNotifyGive(static_cast<TaskHandle_t>(task_));
}

void Musica::successivo() {
    if (indice_ + 1 < int(brani_.size())) suona(indice_ + 1);
    else ferma();
}

void Musica::precedente() {
    if (posizioneS() > 3 || indice_ <= 0) suona(std::max(0, int(indice_)));
    else suona(indice_ - 1);
}

void Musica::salta(int secondi) {
    if (!attiva_ || !task_) return;
    cmdSalto_ = secondi;
    cmd_ = SALTA;
    xTaskNotifyGive(static_cast<TaskHandle_t>(task_));
}

bool Musica::controlla() { return cmd_ == NESSUNO || cmd_ == SALTA; }

void Musica::spingi(const int16_t* mono, size_t n, uint32_t rate) {
    if (rate != rate_) {
        // Frequenza nuova: si aspetta che finisca il brano prima (se ce n'era uno), poi la cassa cambia.
        while (rate_ && scheda::audio().musicQueuedMs() > 0 && cmd_ == NESSUNO) vTaskDelay(pdMS_TO_TICKS(20));
        const uint32_t k = fattoreUscita(rate);
        if (!k || !scheda::audio().musicStart(rate * k)) {
            errore_ = k ? "audio non disponibile" : "frequenza non supportata";
            cmd_ = FERMA;
            return;
        }
        fattore_ = k;
        conUltimo_ = false;
        rate_ = rate;
    }
    if (fattore_ == 1) {
        while (n && cmd_ == NESSUNO) {
            const size_t k = scheda::audio().musicPush(mono, n, 100);
            mono += k;
            n -= k;
            campioni_ += k;
        }
        return;
    }
    // Interpolazione lineare: per ogni campione x escono fattore_ campioni che vanno dal precedente
    // fino a x. Il precedente resta da una chiamata all'altra: fra un pezzo e l'altro niente scatti.
    if (!su_) su_ = static_cast<int16_t*>(heap_caps_malloc(SU_CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!su_) {
        errore_ = "memoria finita";
        cmd_ = FERMA;
        return;
    }
    const int32_t k = int32_t(fattore_);
    while (n && cmd_ == NESSUNO) {
        const size_t m = std::min(n, SU_CAP / size_t(k));
        int32_t p = conUltimo_ ? ultimo_ : mono[0];
        int16_t* o = su_;
        for (size_t i = 0; i < m; i++) {
            const int32_t x = mono[i];
            for (int32_t j = 1; j <= k; j++) *o++ = int16_t(p + (x - p) * j / k);
            p = x;
        }
        ultimo_ = int16_t(p);
        conUltimo_ = true;
        // campioni_ conta i campioni del file: uno ogni fattore_ messi in coda.
        const size_t tot = m * size_t(k);
        size_t fatti = 0, contati = 0;
        while (fatti < tot && cmd_ == NESSUNO) {
            fatti += scheda::audio().musicPush(su_ + fatti, tot - fatti, 100);
            campioni_ += fatti / size_t(k) - contati;
            contati = fatti / size_t(k);
        }
        mono += m;
        n -= m;
    }
}

bool Musica::decodificaMp3(void* fp) {
    FILE* f = static_cast<FILE*>(fp);
    constexpr size_t CAP = 16 * 1024;
    auto* buf = static_cast<uint8_t*>(heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM));
    auto* dec = static_cast<mp3dec_t*>(heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM));
    auto* pcm = static_cast<int16_t*>(heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    bool ok = buf && dec && pcm;
    if (ok) {
        mp3dec_init(dec);
        fseek(f, inizioDati_, SEEK_SET);
        size_t len = 0, pos = 0;
        bool eof = false;
        bool primo = true;
        for (;;) {
            if (cmd_ == SALTA) {
                const int64_t target = std::max<int64_t>(0, int64_t(posizioneS()) + cmdSalto_);
                cmd_ = NESSUNO;
                if (kbps_) {
                    const long off = inizioDati_ + long(target * kbps_ * 125);
                    if (off >= fineDati_) break;  // oltre la fine: brano finito
                    fseek(f, off, SEEK_SET);
                    len = pos = 0;
                    eof = false;
                    mp3dec_init(dec);
                    campioni_ = uint64_t(target) * rate_;
                    if (rate_) {
                        scheda::audio().musicStart(rate_ * fattore_);  // via quello che c'era in coda
                        conUltimo_ = false;  // si riparte da un altro punto: niente ponte col campione di prima
                    }
                }
            }
            if (cmd_ != NESSUNO) break;
            if (!eof && len - pos < 4096) {
                std::memmove(buf, buf + pos, len - pos);
                len -= pos;
                pos = 0;
                const long resto = fineDati_ - ftell(f);
                const size_t vuoi = std::min<size_t>(CAP - len, size_t(std::max<long>(0, resto)));
                const size_t got = vuoi ? fread(buf + len, 1, vuoi, f) : 0;
                if (!got) eof = true;
                len += got;
            }
            if (pos >= len) break;
            mp3dec_frame_info_t info;
            const int n = mp3dec_decode_frame(dec, buf + pos, int(len - pos), pcm, &info);
            if (!info.frame_bytes) {
                if (eof) break;
                pos = len;  // niente di buono in questo pezzo: avanti
                continue;
            }
            const uint8_t* fr = buf + pos;  // il frame appena decodificato comincia frame_offset byte dopo
            pos += size_t(info.frame_bytes);
            if (n <= 0 || info.hz <= 0) continue;
            if (primo && info.bitrate_kbps > 0) {
                primo = false;
                // Nei VBR il primo frame e' quello "Xing"/"Info" (silenzio, col bitrate di base): se dice
                // quanti frame ci sono, durata giusta e bitrate medio per i salti. Un bitrate medio fuori
                // da quelli possibili per l'MP3 (8-320 kbps) vuol dire un numero sbagliato: si ignora.
                const uint64_t campioni =
                    uint64_t(frameXing(fr + info.frame_offset, size_t(info.frame_bytes - info.frame_offset))) * uint32_t(n);
                const uint64_t medio = campioni ? uint64_t(fineDati_ - inizioDati_) * uint32_t(info.hz) / (campioni * 125) : 0;
                if (medio >= 8 && medio <= 320) {
                    kbps_ = uint32_t(medio);
                    durataS_ = uint32_t(campioni / uint32_t(info.hz));
                } else {
                    kbps_ = uint32_t(info.bitrate_kbps);
                    durataS_ = uint32_t((fineDati_ - inizioDati_) / long(kbps_ * 125));
                }
            }
            if (info.channels == 2)
                for (int i = 0; i < n; i++) pcm[i] = int16_t((int32_t(pcm[2 * i]) + pcm[2 * i + 1]) / 2);
            spingi(pcm, size_t(n), uint32_t(info.hz));
        }
    } else {
        errore_ = "memoria finita";
    }
    heap_caps_free(buf);
    heap_caps_free(dec);
    heap_caps_free(pcm);
    return ok;
}

bool Musica::decodificaWav(void* fp) {
    FILE* f = static_cast<FILE*>(fp);
    const int frame = (wavBit_ / 8) * wavCanali_;
    constexpr size_t BLK = 4096;
    auto* raw = static_cast<uint8_t*>(heap_caps_malloc(BLK, MALLOC_CAP_SPIRAM));
    auto* mono = static_cast<int16_t*>(heap_caps_malloc(BLK * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    bool ok = raw && mono && frame > 0;
    if (ok) {
        fseek(f, inizioDati_, SEEK_SET);
        for (;;) {
            if (cmd_ == SALTA) {
                const int64_t target = std::max<int64_t>(0, int64_t(posizioneS()) + cmdSalto_);
                cmd_ = NESSUNO;
                const long off = inizioDati_ + long(target * wavRate_ * uint32_t(frame));
                if (off >= fineDati_) break;
                fseek(f, off, SEEK_SET);
                campioni_ = uint64_t(target) * wavRate_;
                if (rate_) {
                    scheda::audio().musicStart(rate_ * fattore_);
                    conUltimo_ = false;
                }
            }
            if (cmd_ != NESSUNO) break;
            const long resto = fineDati_ - ftell(f);
            if (resto <= 0) break;
            const size_t vuoi = std::min<size_t>(BLK - BLK % size_t(frame), size_t(resto));
            const size_t got = fread(raw, 1, vuoi, f);
            if (!got) break;
            const size_t n = got / size_t(frame);
            for (size_t i = 0; i < n; i++) {
                int32_t sum = 0;
                for (int c = 0; c < wavCanali_; c++) {
                    const uint8_t* s = raw + i * size_t(frame) + size_t(c * (wavBit_ / 8));
                    sum += wavBit_ == 16 ? int16_t(s[0] | (s[1] << 8)) : (int32_t(s[0]) - 128) << 8;
                }
                mono[i] = int16_t(sum / wavCanali_);
            }
            spingi(mono, n, wavRate_);
        }
    } else if (frame > 0) {
        errore_ = "memoria finita";
    }
    heap_caps_free(raw);
    heap_caps_free(mono);
    return ok;
}

// Apre e suona il brano; false se non si e' potuto leggere.
bool Musica::apri(int indice) {
    if (indice < 0 || indice >= int(brani_.size())) return false;
    const std::string path = brani_[size_t(indice)].path;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        errore_ = "file non leggibile";
        return false;
    }
    struct stat st;
    const long size = stat(path.c_str(), &st) == 0 ? long(st.st_size) : 0;
    titolo_.clear();
    artista_.clear();
    kbps_ = 0;
    durataS_ = 0;
    campioni_ = 0;
    bool ok = false;
    if (finisceCon(path, ".mp3")) {
        inizioDati_ = leggiId3(f, titolo_, artista_);
        fineDati_ = size;
        // Tag ID3v1 in fondo (128 byte che cominciano con "TAG"): non e' audio.
        uint8_t tag[3] = {};
        if (size > 128 && fseek(f, size - 128, SEEK_SET) == 0 && fread(tag, 1, 3, f) == 3 && !std::memcmp(tag, "TAG", 3))
            fineDati_ = size - 128;
        ok = decodificaMp3(f);
    } else {
        // WAV: blocco "fmt " (PCM a 8 o 16 bit, 1 o 2 canali) e blocco "data".
        uint8_t h[12];
        ok = fread(h, 1, 12, f) == 12 && !std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "WAVE", 4);
        bool fmt = false;
        while (ok) {
            uint8_t c[8];
            if (fread(c, 1, 8, f) != 8) {
                ok = false;
                break;
            }
            const uint32_t len = uint32_t(c[4]) | uint32_t(c[5]) << 8 | uint32_t(c[6]) << 16 | uint32_t(c[7]) << 24;
            // Conti a 64 bit: con long (32 bit) una lunghezza da 0x80000000 in su farebbe tornare
            // indietro fseek, anche sullo stesso blocco (e si girerebbe per sempre).
            uint64_t daSaltare = uint64_t(len) + (len & 1);
            if (!std::memcmp(c, "fmt ", 4)) {
                uint8_t d[16] = {};
                if (len < 16 || fread(d, 1, 16, f) != 16) {
                    ok = false;
                    break;
                }
                const uint16_t formato = uint16_t(d[0] | d[1] << 8);
                wavCanali_ = uint16_t(d[2] | d[3] << 8);
                wavRate_ = uint32_t(d[4]) | uint32_t(d[5]) << 8 | uint32_t(d[6]) << 16 | uint32_t(d[7]) << 24;
                wavBit_ = uint16_t(d[14] | d[15] << 8);
                fmt = (formato == 1 || formato == 0xFFFE) && (wavCanali_ == 1 || wavCanali_ == 2) &&
                      (wavBit_ == 8 || wavBit_ == 16) && wavRate_ >= 8000 && wavRate_ <= 48000;
                daSaltare -= 16;  // gia' letti
            } else if (!std::memcmp(c, "data", 4)) {
                inizioDati_ = ftell(f);
                // 0 o 0xFFFFFFFF (WAV scritto in un flusso, per esempio da ffmpeg verso una pipe) o
                // oltre il file: l'audio va fino alla fine del file.
                const uint64_t fine = uint64_t(inizioDati_) + len;
                fineDati_ = (len == 0 || fine > uint64_t(size)) ? size : long(fine);
                break;
            }
            // Blocco che esce dal file (o dimensione del file sconosciuta: niente salti al buio).
            if (size <= 0 || uint64_t(ftell(f)) + daSaltare > uint64_t(size)) {
                ok = false;
                break;
            }
            fseek(f, long(daSaltare), SEEK_CUR);
        }
        if (ok && !fmt) {
            ok = false;
            errore_ = "WAV non supportato (PCM 8/16 bit, 8-48 kHz)";  // una riga sul T-Deck
        }
        if (ok && !fattoreUscita(wavRate_)) {
            // Frequenza che la cassa non accetta e senza un multiplo intero fra quelle che accetta.
            ok = false;
            char t[40];
            snprintf(t, sizeof(t), "WAV a %u Hz non supportato", unsigned(wavRate_));  // una riga
            errore_ = t;
        }
        if (ok) {
            durataS_ = uint32_t((fineDati_ - inizioDati_) / long(wavRate_ * (wavBit_ / 8) * wavCanali_));
            ok = decodificaWav(f);
        } else if (errore_.empty()) {
            errore_ = "file WAV non valido";
        }
    }
    fclose(f);
    return ok;
}

void Musica::compito() {
    for (;;) {
        if (cmd_ == NESSUNO) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const Comando c = cmd_;
        cmd_ = NESSUNO;
        if (c == FERMA) {
            scheda::audio().musicStop();
            rate_ = 0;
            attiva_ = false;
            pausa_ = false;
            indice_ = -1;
            continue;
        }
        if (c != SUONA) continue;  // un salto senza un brano che si legge non vale niente
        rate_ = 0;  // la coda si svuota: il brano scelto parte subito
        int i = cmdIndice_;
        // Un salto lo usa solo chi decodifica (decodificaMp3/Wav): qui fuori non e' un comando nuovo.
        // Se arriva a brano gia' tutto letto il brano finisce e si va avanti (o l'elenco finisce).
        while (i >= 0 && i < int(brani_.size()) && controlla()) {
            indice_ = i;
            ESP_LOGI(TAG, "suona %s", brani_[size_t(i)].path.c_str());
            if (!apri(i)) ESP_LOGW(TAG, "%s: %s", brani_[size_t(i)].path.c_str(), errore_.c_str());
            if (cmd_ == SALTA) cmd_ = NESSUNO;
            if (cmd_ != NESSUNO) break;
            i++;  // brano finito: il successivo
        }
        if (cmd_ == SALTA) cmd_ = NESSUNO;
        if (cmd_ != NESSUNO) continue;  // comando nuovo (altro brano, ferma)
        // Fine dell'elenco: si lascia finire la coda (un salto non la interrompe), poi silenzio.
        while (scheda::audio().musicQueuedMs() > 0 && controlla()) vTaskDelay(pdMS_TO_TICKS(50));
        if (cmd_ == SALTA) cmd_ = NESSUNO;
        if (cmd_ == NESSUNO) {
            scheda::audio().musicStop();
            rate_ = 0;
            attiva_ = false;
            indice_ = -1;
        }
    }
}

}  // namespace app
#endif  // CONFIG_SCHEDA_TDECK_PLUS
