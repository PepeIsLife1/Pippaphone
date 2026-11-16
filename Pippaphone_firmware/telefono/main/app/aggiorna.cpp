#include "app/aggiorna.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#if CONFIG_SCHEDA_TDECK_PLUS
#include "app/cuffie.h"
#endif

namespace app {

namespace {
const char* TAG = "aggiorna";
std::mutex g_mu;
OtaStatus g_st;
httpd_handle_t g_http = nullptr;
esp_netif_t* g_netif = nullptr;
bool g_wifiInit = false;
// otaStop() in corso: il caricamento si ferma al pezzo dopo (e lascia andare il server).
std::atomic<bool> g_ferma{false};
// Socket del caricamento in corso (-1 = nessuno): otaStop() lo chiude, cosi' non si aspetta il resto.
std::atomic<int> g_fd{-1};
// Chi usa la rete (la schermata Aggiorna, i comandi di prova dalla USB): si spegne quando nessuno.
int g_utenti = 0;

const char PAGE[] =
    "<!doctype html><html lang=it><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>Pippaphone</title><style>body{font-family:sans-serif;background:#000;color:#fff;max-width:520px;"
    "margin:40px auto;padding:0 16px}h1{color:#FFB000}button{font-size:18px;padding:10px 20px;background:#FFB000;"
    "border:0;border-radius:20px}#s{margin-top:16px;color:#ccc}</style></head><body><h1>Pippaphone</h1>"
    "<p>Scegli il file del firmware (<b>pippaphone.bin</b>, nella cartella build) e premi Carica.</p>"
    "<input type=file id=f accept='.bin'> <button onclick=u()>Carica</button><div id=s></div><script>"
    "function u(){var f=document.getElementById('f').files[0];if(!f){return}var x=new XMLHttpRequest();"
    "x.upload.onprogress=function(e){document.getElementById('s').textContent='Invio '+Math.round(e.loaded*100/e.total)+'%'};"
    "x.onload=function(){document.getElementById('s').textContent=x.responseText};"
    "x.open('POST','/aggiorna');x.send(f)}</script></body></html>";

esp_err_t getRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

// Il file arriva a pezzi da PEZZO byte. Il pezzo non sta sullo stack: il compito del server ha 4 kB
// e la scrittura nella flash ne usa un bel po'; si prende dalla RAM interna (la flash si scrive da li').
constexpr size_t PEZZO = 2048;
// Ogni attesa dura recv_wait_timeout (30 s): dopo 3 di fila senza dati chi mandava se n'e' andato.
constexpr int MAX_ATTESE = 3;

esp_err_t postUpdate(httpd_req_t* req) {
    // Il socket per otaStop() finche' si e' qui dentro (a ogni uscita torna -1: mai un socket riusato).
    struct Socket {
        explicit Socket(int fd) { g_fd = fd; }
        ~Socket() { g_fd = -1; }
    } presa(httpd_req_to_sockfd(req));
    auto fine = [&](const char* risposta, const char* errore) {
        httpd_resp_send(req, risposta, HTTPD_RESP_USE_STRLEN);
        std::lock_guard<std::mutex> l(g_mu);
        if (errore) g_st.error = errore;
        else g_st.done = true;
        return ESP_OK;
    };
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    char* buf = static_cast<char*>(heap_caps_malloc(PEZZO, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    // Ogni uscita che lascia del file da leggere restituisce ESP_FAIL: il server chiude la connessione
    // invece di leggere e buttare il resto (32 byte alla volta, con attese di 30 s), e otaStop() non
    // resta ad aspettarlo.
    if (!buf) {
        fine("Errore: memoria del telefono finita, riprova.", "memoria finita");
        return ESP_FAIL;
    }
    esp_ota_handle_t h = 0;
    if (!part || esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
        heap_caps_free(buf);
        fine("Errore: partizione non disponibile", "partizione non disponibile");
        return ESP_FAIL;
    }
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_st.total = uint32_t(req->content_len);
        g_st.received = 0;
        g_st.error.clear();
    }
    enum { BENE, RETE, NON_VALIDO } esito = BENE;
    int left = int(req->content_len);
    int attese = 0;
    while (left > 0) {
        if (g_ferma) {  // uscita dalla schermata durante l'invio
            esito = RETE;
            break;
        }
        const int n = httpd_req_recv(req, buf, std::min<int>(left, int(PEZZO)));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++attese < MAX_ATTESE) continue;
        if (n <= 0) {
            esito = RETE;
            break;
        }
        if (esp_ota_write(h, buf, size_t(n)) != ESP_OK) {  // anche se il file non comincia come un firmware
            esito = NON_VALIDO;
            break;
        }
        attese = 0;
        left -= n;
        std::lock_guard<std::mutex> l(g_mu);
        g_st.received += uint32_t(n);
    }
    heap_caps_free(buf);
    // esp_ota_end libera la maniglia in ogni caso; se non si arriva in fondo serve esp_ota_abort.
    if (esito != BENE) esp_ota_abort(h);
    else if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) esito = NON_VALIDO;
    if (esito == RETE) {
        fine("Errore: l'invio si è interrotto, riprova.", "invio interrotto");
        return ESP_FAIL;
    }
    if (esito == NON_VALIDO) {
        fine("Errore: il file non è un firmware valido.", "file non valido");
        return left > 0 ? ESP_FAIL : ESP_OK;
    }
    ESP_LOGI(TAG, "firmware nuovo scritto (%u byte)", unsigned(req->content_len));
    return fine("Fatto! Il telefono si riavvia con il nuovo firmware.", nullptr);
}

// RAM interna libera (il Wi-Fi ne ha bisogno per i suoi compiti e per parte dei buffer).
void logRam(const char* when) {
    ESP_LOGI(TAG, "%s: RAM interna libera %u byte (blocco piu' grande %u), PSRAM libera %u kB", when,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// Un passo dell'avvio del Wi-Fi non riuscito: lo scrivono il log e la schermata.
bool failed(const char* step, esp_err_t e) {
    if (e == ESP_OK) return false;
    ESP_LOGE(TAG, "Wi-Fi non partito: %s -> %s", step, esp_err_to_name(e));
    logRam("dopo l'errore");
    g_st.active = false;
    g_st.error = std::string("Wi-Fi non partito (") + step + ": " + esp_err_to_name(e) + ")";
    return true;
}

// Driver Wi-Fi e rete, una volta sola (poi si accende e si spegne con start/stop). Se un passo non
// riesce, step dice quale.
esp_err_t wifiInit(const char*& step) {
    if (g_wifiInit) return ESP_OK;
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK) {
        step = "esp_netif_init";
        return e;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {  // gia' creato: va bene
        step = "esp_event_loop_create_default";
        return e;
    }
    if (!g_netif) g_netif = esp_netif_create_default_wifi_ap();
    if (!g_netif) {
        step = "esp_netif_create_default_wifi_ap";
        return ESP_FAIL;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&cfg);
    if (e != ESP_OK) {
        step = "esp_wifi_init";
        return e;
    }
    g_wifiInit = true;
    return ESP_OK;
}
}  // namespace

const char* firmwareVersion() { return esp_app_get_description()->version; }

void otaConfirm() { esp_ota_mark_app_valid_cancel_rollback(); }

bool otaStart(uint16_t number) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_st.active) {
        // Gia' accesa da un altro (schermata Aggiorna o comando USB): uno in piu'. Se si sta
        // spegnendo non la si conta (sparisce fra un attimo).
        if (g_ferma) return false;
        g_utenti++;
        return true;
    }
    char ssid[32], pass[16];
    snprintf(ssid, sizeof(ssid), "Pippaphone-%u", number);
    snprintf(pass, sizeof(pass), "%04lu%04lu", (unsigned long)(esp_random() % 10000),
             (unsigned long)(esp_random() % 10000));
    g_st = OtaStatus();
    g_st.active = true;
    g_st.ssid = ssid;
    g_st.password = pass;
    g_st.version = firmwareVersion();
#if CONFIG_SCHEDA_TDECK_PLUS
    // Cuffie USB-C accese: niente Wi-Fi, non stanno insieme nella RAM interna (col Wi-Fi ne restano circa
    // 24 KB). active e' gia' true: se le cuffie si accendono proprio adesso, accendi() (app/cuffie.cpp) vede
    // l'aggiornamento e rinuncia; qui si guarda dopo essersi annunciati, quindi uno dei due vede sempre l'altro.
    if (cuffie().accese()) {
        ESP_LOGW(TAG, "Wi-Fi non acceso: ci sono le cuffie USB-C");
        g_st.active = false;
        g_st.error = "spegni prima le cuffie USB-C";
        return false;
    }
#endif
    logRam("prima del Wi-Fi");

    // Ogni passo puo' fallire (soprattutto per RAM interna finita): prima si ignoravano gli errori e
    // la schermata mostrava nome e password di una rete che non esisteva.
    const char* step = "";
    const esp_err_t ie = wifiInit(step);
    if (failed(step, ie)) return false;
    wifi_config_t w = {};
    strncpy(reinterpret_cast<char*>(w.ap.ssid), ssid, sizeof(w.ap.ssid));
    w.ap.ssid_len = uint8_t(strlen(ssid));
    strncpy(reinterpret_cast<char*>(w.ap.password), pass, sizeof(w.ap.password));
    w.ap.channel = 6;
    w.ap.max_connection = 1;
    w.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (failed("esp_wifi_set_mode", esp_wifi_set_mode(WIFI_MODE_AP))) return false;
    if (failed("esp_wifi_set_config", esp_wifi_set_config(WIFI_IF_AP, &w))) return false;
    if (failed("esp_wifi_start", esp_wifi_start())) return false;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.recv_wait_timeout = 30;
    const esp_err_t he = httpd_start(&g_http, &hc);
    if (he != ESP_OK) {
        g_http = nullptr;
        esp_wifi_stop();
        failed("httpd_start", he);
        return false;
    }
    httpd_uri_t root = {};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = getRoot;
    httpd_register_uri_handler(g_http, &root);
    httpd_uri_t up = {};
    up.uri = "/aggiorna";
    up.method = HTTP_POST;
    up.handler = postUpdate;
    httpd_register_uri_handler(g_http, &up);
    g_utenti = 1;
    ESP_LOGI(TAG, "Wi-Fi %s acceso", ssid);
    logRam("Wi-Fi acceso");
    return true;
}

void otaStop() {
    httpd_handle_t http = nullptr;
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (!g_st.active || g_ferma) return;
        // La usa ancora qualcun altro: resta accesa (ma a firmware scritto si riparte comunque).
        if (!g_st.done && --g_utenti > 0) return;
        g_utenti = 0;
        g_ferma = true;
        http = g_http;
        g_http = nullptr;
    }
    // Un caricamento in corso si interrompe subito: la sua recv ritorna e il server si puo' fermare.
    const int fd = g_fd.load();
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
    // Senza il lucchetto: httpd_stop aspetta che il compito del server finisca la richiesta in corso,
    // e il caricamento prende il lucchetto a ogni pezzo (prima, uscendo durante l'invio, si
    // aspettavano a vicenda per sempre e l'interfaccia restava bloccata).
    if (http) httpd_stop(http);
    std::lock_guard<std::mutex> l(g_mu);
    esp_wifi_stop();
    g_st.active = false;
    g_ferma = false;
    if (g_st.done) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

OtaStatus otaStatus() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_st;
}

bool otaScan(std::vector<std::string>& reti, std::string& errore) {
    std::lock_guard<std::mutex> l(g_mu);
    reti.clear();
    if (g_st.active) {
        errore = "la rete dell'aggiornamento e' accesa";
        return false;
    }
    const char* step = "";
    esp_err_t e = wifiInit(step);
    if (e == ESP_OK) {
        step = "esp_wifi_set_mode";
        e = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (e == ESP_OK) {
        step = "esp_wifi_start";
        e = esp_wifi_start();
    }
    if (e == ESP_OK) {
        wifi_scan_config_t sc = {};
        sc.show_hidden = true;
        step = "esp_wifi_scan_start";
        e = esp_wifi_scan_start(&sc, true);  // aspetta la fine (tutti i canali, un paio di secondi)
    }
    if (e == ESP_OK) {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        std::vector<wifi_ap_record_t> rec(std::min<uint16_t>(n, 30));
        uint16_t got = uint16_t(rec.size());
        if (got) esp_wifi_scan_get_ap_records(&got, rec.data());
        esp_wifi_clear_ap_list();
        for (uint16_t i = 0; i < got; i++) {
            char b[96];
            const char* nome = reinterpret_cast<const char*>(rec[i].ssid);
            snprintf(b, sizeof(b), "%s canale %u %d dBm", *nome ? nome : "(nascosta)", unsigned(rec[i].primary),
                     int(rec[i].rssi));
            reti.push_back(b);
        }
        wifi_country_t c = {};
        if (esp_wifi_get_country(&c) == ESP_OK) {
            char b[64];
            snprintf(b, sizeof(b), "paese %c%c canali %u-%u", c.cc[0] ? c.cc[0] : '?', c.cc[1] ? c.cc[1] : '?',
                     unsigned(c.schan), unsigned(c.schan + c.nchan - 1));
            errore = b;  // non e' un errore: con true e' una nota
        }
    } else {
        errore = std::string(step) + ": " + esp_err_to_name(e);
        ESP_LOGE(TAG, "ricerca reti non riuscita: %s", errore.c_str());
    }
    if (g_wifiInit) esp_wifi_stop();
    return e == ESP_OK;
}

}  // namespace app
