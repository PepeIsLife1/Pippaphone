#include "app/memoria.h"

#include <sys/stat.h>

#include <cstdio>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wear_levelling.h"

namespace app {

const char* const DATI = "/dati";

namespace {
const char* TAG = "memoria";
nvs_handle_t g_nvs = 0;
wl_handle_t g_wl = WL_INVALID_HANDLE;
}  // namespace

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
    char buf[64];
    size_t n = sizeof(buf);
    if (nvs_get_str(g_nvs, key, buf, &n) != ESP_OK) return def;
    return buf;
}

void setStr(const char* key, const std::string& v) {
    nvs_set_str(g_nvs, key, v.c_str());
    nvs_commit(g_nvs);
}

int32_t getInt(const char* key, int32_t def) {
    int32_t v = def;
    nvs_get_i32(g_nvs, key, &v);
    return v;
}

void setInt(const char* key, int32_t v) {
    nvs_set_i32(g_nvs, key, v);
    nvs_commit(g_nvs);
}

void eraseAll() {
    nvs_erase_all(g_nvs);
    nvs_commit(g_nvs);
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
    if (!readFile("/dati/cert.bin", data)) return out;
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
    writeFile("/dati/cert.bin", data);
}

}  // namespace app
