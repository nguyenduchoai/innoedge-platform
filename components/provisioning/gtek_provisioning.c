#include "gtek_provisioning.h"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gtek_wifi_manager.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "gtek.prov";

static gtek_provisioning_ui_fn s_ui_notify;

void gtek_provisioning_set_ui_notify(gtek_provisioning_ui_fn fn)
{
    s_ui_notify = fn;
}

void ble_store_config_init(void);

static const ble_uuid128_t s_service_uuid =
    BLE_UUID128_INIT(0xa6, 0xff, 0xa9, 0x1b, 0x91, 0xf4, 0xf2, 0xad,
                     0x0f, 0x4a, 0x6d, 0xcf, 0x54, 0x44, 0x23, 0x2f);
static const ble_uuid128_t s_command_uuid =
    BLE_UUID128_INIT(0xa6, 0xff, 0xa9, 0x1b, 0x91, 0xf4, 0xf2, 0xad,
                     0x0f, 0x4a, 0x6d, 0xcf, 0x55, 0x44, 0x23, 0x2f);
static const ble_uuid128_t s_status_uuid =
    BLE_UUID128_INIT(0xa6, 0xff, 0xa9, 0x1b, 0x91, 0xf4, 0xf2, 0xad,
                     0x0f, 0x4a, 0x6d, 0xcf, 0x56, 0x44, 0x23, 0x2f);

static uint8_t s_own_addr_type;
static bool s_ble_started;
static bool s_restarting;
static char s_device_name[32] = CONFIG_GTEK_BLE_SETUP_PREFIX;
static char s_status[128] = "{\"state\":\"idle\"}";
static uint16_t s_status_val_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static gtek_device_config_t s_config;

static void advertise(void);

static const char *json_string_any(cJSON *root, const char *key1, const char *key2,
                                   const char *key3)
{
    cJSON *item = cJSON_GetObjectItem(root, key1);
    if (!cJSON_IsString(item) && key2) {
        item = cJSON_GetObjectItem(root, key2);
    }
    if (!cJSON_IsString(item) && key3) {
        item = cJSON_GetObjectItem(root, key3);
    }
    return cJSON_IsString(item) ? item->valuestring : "";
}

static void set_status(const char *state, const char *detail)
{
    if (detail && detail[0]) {
        snprintf(s_status, sizeof(s_status), "{\"state\":\"%s\",\"detail\":\"%s\"}",
                 state, detail);
    } else {
        snprintf(s_status, sizeof(s_status), "{\"state\":\"%s\"}", state);
    }
    if (s_status_val_handle != 0) {
        ble_gatts_chr_updated(s_status_val_handle);
    }
}

static void restart_task(void *arg)
{
    (void)arg;
    s_restarting = true;
    vTaskDelay(pdMS_TO_TICKS(350));
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_gap_adv_stop();
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

static int write_json_from_mbuf(struct os_mbuf *om, char *out, size_t out_len)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len == 0 || len >= out_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    int rc = ble_hs_mbuf_to_flat(om, out, out_len - 1, &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    out[len] = '\0';
    return 0;
}

static esp_err_t save_optional_strings(cJSON *root)
{
    const char *server_url = json_string_any(root, "serverUrl", "serverBaseUrl", "baseUrl");
    const char *ws_url = json_string_any(root, "wsUrl", "websocketUrl", "websocket_url");
    const char *token = json_string_any(root, "authToken", "deviceToken", "token");

    if (server_url[0] != '\0') {
        if (strlen(server_url) >= sizeof(s_config.server_base_url)) {
            return ESP_ERR_INVALID_SIZE;
        }
        ESP_RETURN_ON_ERROR(gtek_config_store_save_server_base_url(server_url),
                            TAG, "save server url failed");
    }
    if (ws_url[0] != '\0') {
        if (strlen(ws_url) >= sizeof(s_config.websocket_url)) {
            return ESP_ERR_INVALID_SIZE;
        }
        ESP_RETURN_ON_ERROR(gtek_config_store_save_websocket_url(ws_url),
                            TAG, "save ws url failed");
    }
    if (token[0] != '\0') {
        if (strlen(token) >= sizeof(s_config.device_token)) {
            return ESP_ERR_INVALID_SIZE;
        }
        ESP_RETURN_ON_ERROR(gtek_config_store_save_token(token), TAG, "save token failed");
        ESP_RETURN_ON_ERROR(gtek_config_store_save_assigned(true), TAG, "save assigned failed");
    }
    return ESP_OK;
}

static int handle_command_write(struct os_mbuf *om)
{
    char payload[512];
    int rc = write_json_from_mbuf(om, payload, sizeof(payload));
    if (rc != 0) {
        set_status("error", "payload_too_large");
        return rc;
    }

    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        set_status("error", "json");
        return 0;
    }
    const char *ssid = json_string_any(root, "ssid", "wifiSsid", "wifi_ssid");
    const char *password = json_string_any(root, "password", "wifiPassword", "wifi_password");
    if (ssid[0] == '\0') {
        cJSON_Delete(root);
        set_status("error", "ssid_empty");
        return 0;
    }
    if (strlen(ssid) >= sizeof(s_config.wifi_ssid)) {
        cJSON_Delete(root);
        set_status("error", "ssid_too_long");
        return 0;
    }
    if (strlen(password) >= sizeof(s_config.wifi_password)) {
        cJSON_Delete(root);
        set_status("error", "password_too_long");
        return 0;
    }

    ESP_LOGI(TAG, "BLE provisioning received SSID=%s", ssid);
    set_status("saving", NULL);
    esp_err_t err = gtek_config_store_save_wifi(ssid, password);
    if (err == ESP_OK) {
        err = save_optional_strings(root);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save provisioning data failed: %s", esp_err_to_name(err));
        set_status("error", "nvs");
        return 0;
    }

    set_status("ok", "restarting");
    BaseType_t ok = xTaskCreate(restart_task, "gtek_ble_reboot", 3072, NULL, 5, NULL);
    if (ok != pdPASS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    return 0;
}

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        ble_uuid_cmp(ctxt->chr->uuid, &s_command_uuid.u) == 0) {
        return handle_command_write(ctxt->om);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR &&
        ble_uuid_cmp(ctxt->chr->uuid, &s_status_uuid.u) == 0) {
        int rc = os_mbuf_append(ctxt->om, s_status, strlen(s_status));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_command_uuid.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_val_handle,
            },
            {0},
        },
    },
    {0},
};

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connect status=%d", event->connect.status);
        if (event->connect.status != 0) {
            advertise();
        } else {
            s_conn_handle = event->connect.conn_handle;
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnect reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (!s_restarting) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!s_restarting) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "BLE subscribe conn=%d attr=%d notify=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE mtu=%d", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.uuids128 = (ble_uuid128_t[]) {s_service_uuid};
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (uint8_t *)s_device_name;
    rsp_fields.name_len = strlen(s_device_name);
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "scan rsp rc=%d", rc);
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start rc=%d", rc);
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure addr rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr rc=%d", rc);
        return;
    }
    set_status("idle", NULL);
    advertise();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void set_device_name(void)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_device_name, sizeof(s_device_name), "%s-%02X:%02X",
                 CONFIG_GTEK_BLE_SETUP_PREFIX, mac[4], mac[5]);
    } else {
        snprintf(s_device_name, sizeof(s_device_name), "%s", CONFIG_GTEK_BLE_SETUP_PREFIX);
    }
}

static esp_err_t start_ble(const gtek_device_config_t *config)
{
    if (s_ble_started) {
        return ESP_OK;
    }
    if (config) {
        s_config = *config;
    } else {
        memset(&s_config, 0, sizeof(s_config));
    }
    set_device_name();
    s_restarting = false;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt count rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt add rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "set BLE name rc=%d", rc);
        return ESP_FAIL;
    }
    ble_store_config_init();
    nimble_port_freertos_init(host_task);

    s_ble_started = true;
    ESP_LOGI(TAG, "BLE provisioning advertising as %s", s_device_name);
    return ESP_OK;
}

esp_err_t gtek_provisioning_start(const gtek_device_config_t *config)
{
    if (s_ui_notify) {
        s_ui_notify();
    }

    esp_err_t ap_err = gtek_wifi_manager_start_provisioning();
    if (ap_err != ESP_OK) {
        ESP_LOGW(TAG, "SoftAP provisioning failed: %s", esp_err_to_name(ap_err));
    }

    esp_err_t ble_err = start_ble(config);
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "BLE provisioning failed: %s", esp_err_to_name(ble_err));
    }

    if (ble_err == ESP_OK || ap_err == ESP_OK) {
        return ESP_OK;
    }
    return ble_err != ESP_OK ? ble_err : ap_err;
}
