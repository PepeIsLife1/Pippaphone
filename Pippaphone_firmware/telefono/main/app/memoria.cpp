#include "app/memoria.h"

#include <sys/stat.h>

#include <cstdio>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wear_levelling.h"
#if CONFIG_SCHEDA_TDECK_PLUS
#include <dirent.h>

#include <cstdlib>
#include <cstring>
#include <map>

#include "driver/sdspi_host.h"
#include "scheda/scheda.h"
#include "sdmmc_cmd.h"
#endif

namespace app {

#if CONFIG_SCHEDA_TDECK_PLUS
namespace {
char g_datiBuf[24] = "/dati";
}
const char* DATI = g_datiBuf;
const char* const MAPPE = "/sd/mappe";
#else
const char* const DATI = "/dati";
#endif

namespace {
const char* TAG = "memoria";
nvs_handle_t g_nvs = 0;
wl_handle_t g_wl = WL_INVALID_HANDLE;
}  // namespace

#if CONFIG_SCHEDA_TDECK_PLUS
// ---------------------------------------------------------------- T-Deck: microSD

namespace {
bool g_sd = false;
std::map<std::string, std::string> g_conf;  // impostazioni.txt sulla microSD

// Impostazioni che vanno nel file sulla microSD (il PIN resta nella memoria interna).
const char* const CONF_KEYS[] = {"luce", "volume", "vibra", "suoneria", "luce_tasti", "spegni_s", "potenza", "voce_n"};
bool inConf(const char* key) { return g_sd && std::strcmp(key, "pin") != 0; }
std::string confPath() { return std::string(DATI) + "/impostazioni.txt"; }

bool mountSd() {
    if (!scheda::spiBusReady()) return false;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = scheda::SPI_HOST_SCHEDA;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = scheda::PIN_SD_CS;
    slot.host_id = scheda::SPI_HOST_SCHEDA;
    esp_vfs_fat_mount_config_t m = {};
    m.format_if_mount_failed = false;  // la scheda di qualcuno non si formatta mai da soli
    m.max_files = 8;
    m.allocation_unit_size = 16 * 1024;
    sdmmc_card_t* card = nullptr;
    const esp_err_t e = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &m, &card);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "microSD assente o non leggibile (%s): dati nella memoria interna", esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(TAG, "microSD %s, %llu MB", card->cid.name,
             (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024));
    return true;
}

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool copyFile(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    static char buf[4096];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) ok &= fwrite(buf, 1, n, out) == n;
    fclose(in);
    fclose(out);
    return ok;
}

// Copia (o cancella, se to e' vuoto) i file di una cartella, senza sottocartelle.
int eachFile(const std::string& dir, const std::string& to) {
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    int n = 0;
    while (dirent* e = readdir(d)) {
        if (e->d_type == DT_DIR) continue;
        const std::string p = dir + "/" + e->d_name;
        if (to.empty()) remove(p.c_str());
        else copyFile(p, to + "/" + e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

// La prima volta con la microSD: i dati gia' nella memoria interna si copiano sulla scheda.
void migrate() {
    esp_vfs_fat_mount_config_t m = {};
    m.format_if_mount_failed = false;
    m.max_files = 4;
    wl_handle_t wl = WL_INVALID_HANDLE;
    if (esp_vfs_fat_spiflash_mount_rw_wl("/int", "dati", &m, &wl) != ESP_OK) return;  // mai usata
    const std::string to = DATI;
    const int n = eachFile("/int", to) + eachFile("/int/chat", to + "/chat") + eachFile("/int/voce", to + "/voce");
    esp_vfs_fat_spiflash_unmount_rw_wl("/int", wl);
    if (n) ESP_LOGI(TAG, "copiati %d file dalla memoria interna alla microSD", n);
}

void saveConf() {
    std::string s =
        "# Impostazioni del Pippaphone: si possono cambiare anche qui, a telefono spento.\n"
        "# luce e volume: luce in %, volume 0-10; suoneria 0 = silenziosa, 1 = classica, 2 = breve;\n"
        "# spegni_s = secondi prima di spegnere lo schermo; potenza in dBm (0 = automatica).\n";
    for (const auto& kv : g_conf) s += kv.first + "=" + kv.second + "\n";
    writeFile(confPath(), std::vector<uint8_t>(s.begin(), s.end()));
}

void loadConf() {
    std::vector<uint8_t> d;
    if (readFile(confPath(), d)) {
        std::string line;
        for (size_t i = 0; i <= d.size(); i++) {
            const char c = i < d.size() ? char(d[i]) : '\n';
            if (c != '\n' && c != '\r') {
                line += c;
                continue;
            }
            const size_t eq = line.find('=');
            if (!line.empty() && line[0] != '#' && eq != std::string::npos) g_conf[line.substr(0, eq)] = line.substr(eq + 1);
            line.clear();
        }
        return;
    }
    // Prima volta sulla microSD: si parte dalle impostazioni nella memoria interna.
    for (const char* k : CONF_KEYS) {
        int32_t v;
        if (nvs_get_i32(g_nvs, k, &v) == ESP_OK) g_conf[k] = std::to_string(v);
    }
    saveConf();
}
}  // namespace

std::string datiPath(const char* rel) { return std::string(DATI) + rel; }

bool sdPresente() { return g_sd; }

void writeDeviceInfo(const std::string& text) {
    if (g_sd) writeFile(std::string(DATI) + "/info.txt", std::vector<uint8_t>(text.begin(), text.end()));
}

bool memoriaInit() {
    // Chiavi, certificato e PIN: nella memoria interna come sulla PCB.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK || nvs_open("pippaphone", NVS_READWRITE, &g_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "NVS non disponibile");
        return false;
    }
    if (mountSd()) {
        g_sd = true;
        std::strcpy(g_datiBuf, "/sd/pippaphone");
        mkdir(DATI, 0775);
        const bool fresh = !exists(datiPath("/rubrica.bin")) && !exists(datiPath("/gruppi.bin")) &&
                           !exists(datiPath("/chat")) && !exists(datiPath("/impostazioni.txt"));
        mkdir(DATI_CPATH("/chat"), 0775);
        mkdir(DATI_CPATH("/voce"), 0775);
        mkdir(MAPPE, 0775);
        if (fresh) migrate();
        loadConf();
        ESP_LOGI(TAG, "dati sulla microSD in %s", DATI);
        return true;
    }
    // Senza microSD: partizione interna come sulla PCB.
    esp_vfs_fat_mount_config_t m = {};
    m.format_if_mount_failed = true;
    m.max_files = 6;
    m.allocation_unit_size = 4096;
    if (esp_vfs_fat_spiflash_mount_rw_wl(DATI, "dati", &m, &g_wl) != ESP_OK) {
        ESP_LOGE(TAG, "partizione dati non montata");
        return false;
    }
    mkdir("/dati/chat", 0775);
    mkdir("/dati/voce", 0775);
    return true;
}
#else
bool memoriaInit() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK || nvs_open("pippaphone", NVS_READWRITE, &g_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "NVS non disponibile");
        return false;
    }
    esp_vfs_fat_mount_config_t m = {};
    m.format_if_mount_failed = true;
    m.max_files = 6;
    m.allocation_unit_size = 4096;
    if (esp_vfs_fat_spiflash_mount_rw_wl(DATI, "dati", &m, &g_wl) != ESP_OK) {
        ESP_LOGE(TAG, "partizione dati non montata");
        return false;
    }
    mkdir("/dati/chat", 0775);
    mkdir("/dati/voce", 0775);
    return true;
}
#endif

// ---------------------------------------------------------------- NVS

static bool getBlob(const char* key, void* out, size_t len) {
    size_t n = len;
    return nvs_get_blob(g_nvs, key, out, &n) == ESP_OK && n == len;
}

static void setBlob(const char* key, const void* data, size_t len) {
    nvs_set_blob(g_nvs, key, data, len);
    nvs_commit(g_nvs);
}

bool loadIdentity(pn::Identity& id) {
    uint8_t seeds[64];
    if (getBlob("id_semi", seeds, sizeof(seeds))) {
        id = pn::Identity::fromSeeds(seeds, seeds + 32);
        pn::wipe(seeds, sizeof(seeds));
        return true;
    }
    id = pn::Identity::generate();
    id.seeds(seeds, seeds + 32);
    setBlob("id_semi", seeds, sizeof(seeds));
    pn::wipe(seeds, sizeof(seeds));
    return false;
}

bool loadCredentials(pn::Cert& own, pn::Key32& adminPub) {
    uint8_t c[pn::CERT_SIZE];
    if (!getBlob("cert", c, sizeof(c)) || !getBlob("admin_pub", adminPub.data(), 32)) return false;
    return pn::Cert::parse(c, sizeof(c), own);
}

void saveCredentials(const pn::Cert& own, const pn::Key32& adminPub) {
    uint8_t c[pn::CERT_SIZE];
    own.serialize(c);
    setBlob("cert", c, sizeof(c));
    setBlob("admin_pub", adminPub.data(), 32);
}

bool loadAdminIdentity(pn::Identity& admin) {
    uint8_t seeds[64];
    if (!getBlob("admin_semi", seeds, sizeof(seeds))) return false;
    admin = pn::Identity::fromSeeds(seeds, seeds + 32);
    pn::wipe(seeds, sizeof(seeds));
    return true;
}

void saveAdminIdentity(const pn::Identity& admin) {
    uint8_t seeds[64];
    admin.seeds(seeds, seeds + 32);
    setBlob("admin_semi", seeds, sizeof(seeds));
    pn::wipe(seeds, sizeof(seeds));
}

uint32_t loadSeq() {
    uint32_t s = 1;
    nvs_get_u32(g_nvs, "seq", &s);
    return s;
}

void saveSeq(uint32_t next) {
    nvs_set_u32(g_nvs, "seq", next);
    nvs_commit(g_nvs);
}

std::string getStr(const char* key, const std::string& def) {
#if CONFIG_SCHEDA_TDECK_PLUS
    if (inConf(key)) {
        auto it = g_conf.find(key);
        return it == g_conf.end() ? def : it->second;
    }
#endif
    char buf[64];
    size_t n = sizeof(buf);
    if (nvs_get_str(g_nvs, key, buf, &n) != ESP_OK) return def;
    return buf;
}

void setStr(const char* key, const std::string& v) {
#if CONFIG_SCHEDA_TDECK_PLUS
    if (inConf(key)) {
        g_conf[key] = v;
        saveConf();
        return;
    }
#endif
    nvs_set_str(g_nvs, key, v.c_str());
    nvs_commit(g_nvs);
}

int32_t getInt(const char* key, int32_t def) {
#if CONFIG_SCHEDA_TDECK_PLUS
    if (inConf(key)) {
        auto it = g_conf.find(key);
        return it == g_conf.end() ? def : int32_t(std::atol(it->second.c_str()));
    }
#endif
    int32_t v = def;
    nvs_get_i32(g_nvs, key, &v);
    return v;
}

void setInt(const char* key, int32_t v) {
#if CONFIG_SCHEDA_TDECK_PLUS
    if (inConf(key)) {
        g_conf[key] = std::to_string(v);
        saveConf();
        return;
    }
#endif
    nvs_set_i32(g_nvs, key, v);
    nvs_commit(g_nvs);
}

void eraseAll() {
    nvs_erase_all(g_nvs);
    nvs_commit(g_nvs);
#if CONFIG_SCHEDA_TDECK_PLUS
    if (g_sd) {
        // Solo i dati del telefono: mappe e altri file sulla scheda restano.
        const std::string d = DATI;
        eachFile(d + "/chat", "");
        eachFile(d + "/voce", "");
        eachFile(d, "");
        g_conf.clear();
        esp_vfs_fat_mount_config_t m = {};
        m.format_if_mount_failed = true;
        m.max_files = 6;
        esp_vfs_fat_spiflash_format_cfg_rw_wl("/dati", "dati", &m);  // anche l'eventuale copia interna
        return;
    }
#endif
    esp_vfs_fat_spiflash_unmount_rw_wl(DATI, g_wl);
    esp_vfs_fat_mount_config_t m = {};
    m.format_if_mount_failed = true;
    m.max_files = 6;
    esp_vfs_fat_spiflash_format_cfg_rw_wl(DATI, "dati", &m);
}

// ---------------------------------------------------------------- file

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    // Spento tra la cancellazione e la rinomina di writeFile(): c'e' ancora il temporaneo.
    if (!f) f = fopen((path + ".tmp").c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = n <= 0 || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    // Scrive in un file temporaneo e poi rinomina: niente file a meta' se si spegne.
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!ok) return false;
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

void removeFile(const std::string& path) { remove(path.c_str()); }

std::vector<pn::Cert> loadCerts() {
    std::vector<pn::Cert> out;
    std::vector<uint8_t> data;
    if (!readFile(DATI_PATH("/cert.bin"), data)) return out;
    for (size_t i = 0; i + pn::CERT_SIZE <= data.size(); i += pn::CERT_SIZE) {
        pn::Cert c;
        if (pn::Cert::parse(data.data() + i, pn::CERT_SIZE, c)) out.push_back(c);
    }
    return out;
}

void saveCert(const pn::Cert& c) {
    std::vector<pn::Cert> all = loadCerts();
    bool replaced = false;
    for (auto& x : all)
        if (x.number == c.number) {
            x = c;
            replaced = true;
        }
    if (!replaced) all.push_back(c);
    std::vector<uint8_t> data(all.size() * pn::CERT_SIZE);
    for (size_t i = 0; i < all.size(); i++) all[i].serialize(data.data() + i * pn::CERT_SIZE);
    writeFile(DATI_PATH("/cert.bin"), data);
}

}  // namespace app
