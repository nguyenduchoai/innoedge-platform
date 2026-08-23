// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_wifi_manager.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "gtek.wifi";
static EventGroupHandle_t s_wifi_events;
static const int WIFI_CONNECTED_BIT = BIT0;
static bool s_wifi_initialized;
static bool s_handlers_registered;
static bool s_prov_started;
static httpd_handle_t s_prov_server;

#define SCAN_MAX_APS 20
static wifi_ap_record_t s_aps[SCAN_MAX_APS];
static uint16_t s_aps_count;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "disconnected, reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "connected");
    }
}

static void ensure_default_netifs(void)
{
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_create_default_wifi_sta();
    }
}

static esp_err_t ensure_wifi_init(bool register_handlers)
{
    if (!s_wifi_events) {
        s_wifi_events = xEventGroupCreate();
        if (!s_wifi_events) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_wifi_initialized = true;
    }

    if (register_handlers && !s_handlers_registered) {
        esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler, NULL, NULL);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &wifi_event_handler, NULL, NULL);
        if (err != ESP_OK) {
            return err;
        }
        s_handlers_registered = true;
    }
    return ESP_OK;
}

static void scan_wifi_now(void)
{
    wifi_scan_config_t scan_config = {0};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        s_aps_count = 0;
        return;
    }
    uint16_t count = SCAN_MAX_APS;
    err = esp_wifi_scan_get_ap_records(&count, s_aps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan records failed: %s", esp_err_to_name(err));
        s_aps_count = 0;
        return;
    }
    s_aps_count = count;
    ESP_LOGI(TAG, "scan got %u APs", (unsigned)s_aps_count);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t prov_scan_handler(httpd_req_t *req)
{
    scan_wifi_now();
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "aps");
    for (int i = 0; i < s_aps_count; i++) {
        if (s_aps[i].ssid[0] == 0) {
            continue;
        }
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (const char *)s_aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", s_aps[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", s_aps[i].authmode);
        cJSON_AddItemToArray(arr, ap);
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t err = httpd_resp_send(req, json ? json : "{\"aps\":[]}", HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    cJSON_Delete(root);
    return err;
}

static esp_err_t read_body(httpd_req_t *req, char **out)
{
    int len = req->content_len;
    if (len <= 0 || len > 512) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *buf = calloc(1, len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    int received = 0;
    while (received < len) {
        int n = httpd_req_recv(req, buf + received, len - received);
        if (n <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        received += n;
    }
    buf[received] = '\0';
    *out = buf;
    return ESP_OK;
}

static esp_err_t prov_save_handler(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t err = read_body(req, &body);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json(req, "{\"ok\":false,\"error\":\"json\",\"err\":\"json\"}");
    }

    cJSON *jssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *jpass = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(jssid) || jssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_json(req, "{\"ok\":false,\"error\":\"ssid_empty\",\"err\":\"ssid_empty\"}");
    }

    const char *ssid = jssid->valuestring;
    const char *password = cJSON_IsString(jpass) ? jpass->valuestring : "";
    err = gtek_config_store_save_wifi(ssid, password);
    ESP_LOGI(TAG, "save WiFi SSID=%s -> %s", ssid, esp_err_to_name(err));
    cJSON_Delete(root);

    if (err != ESP_OK) {
        return send_json(req, "{\"ok\":false,\"error\":\"nvs\",\"err\":\"nvs\"}");
    }
    send_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return ESP_OK;
}

static esp_err_t prov_get_handler(httpd_req_t *req)
{
    static const char *html =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Gtek WiFi setup</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;"
        "max-width:480px;margin:0 auto;padding:16px;background:#f6f7fb;color:#0f172a}"
        "h2{margin:8px 0 4px;color:#155eef}.sub{color:#64748b;margin:0 0 16px}"
        ".card{background:#fff;border:1px solid #dbe3ef;border-radius:8px;padding:14px;margin:12px 0}"
        ".net{display:flex;gap:8px;align-items:center;padding:11px;border-radius:6px;cursor:pointer}"
        ".net:hover,.net.sel{background:#eaf1ff}.name{flex:1;font-weight:600}"
        "input,button{width:100%;font-size:16px;border-radius:6px;padding:12px;margin-top:8px}"
        "input{border:1px solid #cbd5e1}button{border:0;background:#155eef;color:#fff;font-weight:700}"
        "button.refresh{background:#fff;color:#155eef;border:1px solid #155eef;padding:8px;font-size:14px}"
        ".msg{display:none;margin-top:10px;padding:10px;border-radius:6px}.err{display:block;background:#fee2e2;color:#991b1b}"
        ".ok{display:block;background:#dcfce7;color:#166534}.loading{color:#64748b;padding:18px;text-align:center}"
        "</style></head><body>"
        "<h2>Gtek</h2><p class=sub>Connect this device to store WiFi.</p>"
        "<div class=card><div style='display:flex;justify-content:space-between;align-items:center'>"
        "<b>Nearby WiFi</b><button class=refresh onclick=loadScan()>Scan</button></div>"
        "<div id=list class=loading>Scanning...</div></div>"
        "<div class=card><label>SSID</label><input id=ssid placeholder='Choose or type WiFi name'>"
        "<label>Password</label><input id=pass type=password placeholder='Leave blank for open WiFi'>"
        "<button id=save onclick=save()>Save and reboot</button><div id=msg class=msg></div></div>"
        "<script>"
        "function bars(r){return r>-50?'****':r>-65?'***':r>-78?'**':'*'}"
        "function lock(a){return a==0?'':'locked'}"
        "function loadScan(){let l=document.getElementById('list');l.className='loading';l.textContent='Scanning...';"
        "fetch('/scan').then(r=>r.json()).then(d=>{let aps=(d.aps||[]).sort((a,b)=>b.rssi-a.rssi);"
        "if(!aps.length){l.textContent='No WiFi found';return}l.className='';l.innerHTML='';"
        "aps.forEach(ap=>{let n=document.createElement('div');n.className='net';"
        "n.innerHTML='<span>'+bars(ap.rssi)+'</span><span class=name></span><span>'+ap.rssi+' dBm</span><span>'+lock(ap.auth)+'</span>';"
        "n.querySelector('.name').textContent=ap.ssid;n.onclick=()=>{document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));"
        "n.classList.add('sel');document.getElementById('ssid').value=ap.ssid;document.getElementById('pass').focus()};l.appendChild(n)})"
        "}).catch(e=>{l.textContent='Scan error: '+e})}"
        "function save(){let s=document.getElementById('ssid').value.trim(),p=document.getElementById('pass').value;"
        "let m=document.getElementById('msg'),b=document.getElementById('save');if(!s){m.className='msg err';m.textContent='Enter SSID';return}"
        "b.disabled=true;b.textContent='Saving...';fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,password:p})})"
        ".then(r=>r.json()).then(d=>{if(d.ok){m.className='msg ok';m.textContent='Saved. Device is rebooting.'}"
        "else{m.className='msg err';m.textContent='Error: '+(d.error||d.err||'unknown');b.disabled=false;b.textContent='Save and reboot'}})"
        ".catch(e=>{m.className='msg err';m.textContent='Network error: '+e;b.disabled=false;b.textContent='Save and reboot'})}"
        "loadScan();</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static void provisioning_ssid(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(out, out_len, "%s-%02X:%02X", CONFIG_GTEK_BLE_SETUP_PREFIX, mac[4], mac[5]);
    } else {
        snprintf(out, out_len, "%s", CONFIG_GTEK_BLE_SETUP_PREFIX);
    }
}

static void copy_wifi_text(uint8_t *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    memset(dst, 0, dst_len);
    if (!src) {
        return;
    }
    size_t len = strnlen(src, dst_len - 1);
    memcpy(dst, src, len);
}

esp_err_t gtek_wifi_manager_start(const gtek_device_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "no WiFi SSID configured; provisioning is required");
        return ESP_ERR_INVALID_STATE;
    }

    ensure_default_netifs();
    esp_err_t err = ensure_wifi_init(true);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {0};
    copy_wifi_text(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), config->wifi_ssid);
    copy_wifi_text(wifi_config.sta.password, sizeof(wifi_config.sta.password),
                   config->wifi_password);
    wifi_config.sta.threshold.authmode =
        config->wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_set_mode(s_prov_started ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        return err;
    }
    (void)esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(15000));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t gtek_wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!s_wifi_events) {
        return ESP_ERR_INVALID_STATE;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                           timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t gtek_wifi_manager_start_provisioning(void)
{
    if (s_prov_started) {
        return ESP_OK;
    }
    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")) {
        esp_netif_create_default_wifi_ap();
    }
    ensure_default_netifs();
    esp_err_t err = ensure_wifi_init(false);
    if (err != ESP_OK) {
        return err;
    }

    char ap_ssid[32];
    provisioning_ssid(ap_ssid, sizeof(ap_ssid));
    wifi_config_t ap_config = {0};
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", ap_ssid);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        return err;
    }

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.max_uri_handlers = 8;
    hcfg.max_open_sockets = 2;
    hcfg.stack_size = 6144;
    err = httpd_start(&s_prov_server, &hcfg);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = prov_get_handler};
    httpd_register_uri_handler(s_prov_server, &root);
    httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = prov_get_handler};
    httpd_register_uri_handler(s_prov_server, &wifi);
    httpd_uri_t scan = {.uri = "/scan", .method = HTTP_GET, .handler = prov_scan_handler};
    httpd_register_uri_handler(s_prov_server, &scan);
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = prov_save_handler};
    httpd_register_uri_handler(s_prov_server, &save);
    httpd_uri_t android = {.uri = "/generate_204", .method = HTTP_GET, .handler = prov_get_handler};
    httpd_register_uri_handler(s_prov_server, &android);
    httpd_uri_t ios = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = prov_get_handler};
    httpd_register_uri_handler(s_prov_server, &ios);

    s_prov_started = true;
    ESP_LOGI(TAG, "AP %s started at http://192.168.4.1/", ap_ssid);
    scan_wifi_now();
    return ESP_OK;
}

int gtek_wifi_manager_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}
