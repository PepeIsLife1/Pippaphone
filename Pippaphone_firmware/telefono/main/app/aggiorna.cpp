#include "app/aggiorna.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app {

namespace {
const char* TAG = "aggiorna";
std::mutex g_mu;
OtaStatus g_st;
httpd_handle_t g_http = nullptr;
esp_netif_t* g_netif = nullptr;
bool g_wifiInit = false;

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

esp_err_t postUpdate(httpd_req_t* req) {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t h = 0;
    if (!part || esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
        httpd_resp_send(req, "Errore: partizione non disponibile", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_st.total = uint32_t(req->content_len);
        g_st.received = 0;
        g_st.error.clear();
    }
    char buf[2048];
    int left = int(req->content_len);
    bool ok = true;
    while (left > 0) {
        int n = httpd_req_recv(req, buf, std::min<int>(left, sizeof(buf)));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0 || esp_ota_write(h, buf, size_t(n)) != ESP_OK) {
            ok = false;
            break;
        }
        left -= n;
        std::lock_guard<std::mutex> l(g_mu);
        g_st.received += uint32_t(n);
    }
    if (ok && esp_ota_end(h) == ESP_OK && esp_ota_set_boot_partition(part) == ESP_OK) {
        httpd_resp_send(req, "Fatto! Il telefono si riavvia con il nuovo firmware.", HTTPD_RESP_USE_STRLEN);
        std::lock_guard<std::mutex> l(g_mu);
        g_st.done = true;
    } else {
        if (ok) esp_ota_abort(h);
        httpd_resp_send(req, "Errore: il file non e' un firmware valido.", HTTPD_RESP_USE_STRLEN);
        std::lock_guard<std::mutex> l(g_mu);
        g_st.error = "file non valido";
    }
    return ESP_OK;
}
}  // namespace

const char* firmwareVersion() { return esp_app_get_description()->version; }

void otaConfirm() { esp_ota_mark_app_valid_cancel_rollback(); }

void otaStart(uint16_t number) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_st.active) return;
    char ssid[32], pass[16];
    snprintf(ssid, sizeof(ssid), "Pippaphone-%u", number);
    snprintf(pass, sizeof(pass), "%04lu%04lu", (unsigned long)(esp_random() % 10000),
             (unsigned long)(esp_random() % 10000));
    g_st = OtaStatus();
    g_st.active = true;
    g_st.ssid = ssid;
    g_st.password = pass;
    g_st.version = firmwareVersion();

    if (!g_wifiInit) {
        esp_netif_init();
        esp_event_loop_create_default();
        g_netif = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        g_wifiInit = true;
    }
    wifi_config_t w = {};
    strncpy(reinterpret_cast<char*>(w.ap.ssid), ssid, sizeof(w.ap.ssid));
    w.ap.ssid_len = uint8_t(strlen(ssid));
    strncpy(reinterpret_cast<char*>(w.ap.password), pass, sizeof(w.ap.password));
    w.ap.channel = 6;
    w.ap.max_connection = 1;
    w.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &w);
    esp_wifi_start();

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.recv_wait_timeout = 30;
    if (httpd_start(&g_http, &hc) == ESP_OK) {
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
    }
    ESP_LOGI(TAG, "Wi-Fi %s acceso", ssid);
}

void otaStop() {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_st.active) return;
    if (g_http) httpd_stop(g_http);
    g_http = nullptr;
    esp_wifi_stop();
    g_st.active = false;
    if (g_st.done) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

OtaStatus otaStatus() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_st;
}

}  // namespace app
