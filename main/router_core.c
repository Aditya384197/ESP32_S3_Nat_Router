#include "router_core.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/lwip_napt.h"
#include "lwip/ip4_addr.h"
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#define UPLINK_SSID "Airtel_2.4GHz"
#define UPLINK_PASSWORD "Kgf@0987"
#define AP_SSID "ESP32S3-NAT"
#define AP_PASSWORD "ak@12345"
#define AP_MAX_CONNECTIONS 8

static esp_netif_t *sta_netif;
static esp_netif_t *ap_netif;
static bool napt_enabled;
static uint8_t recovery_index;
static const uint8_t recovery_channels[] = {1, 6, 11};

static void set_ap_network(void)
{
    esp_netif_ip_info_t ip = {0};
    IP4_ADDR(&ip.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
}

static void set_napt(bool enable)
{
    if (ap_netif == NULL) {
        napt_enabled = false;
        return;
    }
    if (enable == napt_enabled) {
        return;
    }
    esp_err_t err = enable ? esp_netif_napt_enable(ap_netif) : esp_netif_napt_disable(ap_netif);
    napt_enabled = (err == ESP_OK) && enable;
}

static void set_radio(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW40));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW40));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(80));
}

static void configure_interfaces(void)
{
    wifi_config_t sta = {0};
    wifi_config_t ap = {0};

    memcpy(sta.sta.ssid, UPLINK_SSID, strlen(UPLINK_SSID));
    memcpy(sta.sta.password, UPLINK_PASSWORD, strlen(UPLINK_PASSWORD));
    sta.sta.channel = 0;
    sta.sta.scan_method = WIFI_FAST_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta.sta.failure_retry_cnt = 10;
    sta.sta.threshold.rssi = -127;
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;

    memcpy(ap.ap.ssid, AP_SSID, strlen(AP_SSID));
    memcpy(ap.ap.password, AP_PASSWORD, strlen(AP_PASSWORD));
    ap.ap.ssid_len = strlen(AP_SSID);
    ap.ap.channel = recovery_channels[0];
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = AP_MAX_CONNECTIONS;
    ap.ap.beacon_interval = 100;
    ap.ap.pmf_cfg.capable = true;
    ap.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    set_radio();
}

static bool find_uplink(uint8_t channel, uint8_t bssid[6])
{
    wifi_scan_config_t scan = {0};
    scan.ssid = (uint8_t *)UPLINK_SSID;
    scan.channel = channel;
    scan.show_hidden = true;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan.scan_time.active.min = 20;
    scan.scan_time.active.max = 40;

    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        return false;
    }

    uint16_t count = 12;
    wifi_ap_record_t records[12];
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        return false;
    }

    for (uint16_t i = 0; i < count; ++i) {
        if (records[i].primary == channel && strncmp((const char *)records[i].ssid, UPLINK_SSID, sizeof(records[i].ssid)) == 0) {
            memcpy(bssid, records[i].bssid, 6);
            return true;
        }
    }
    return false;
}

static void reconnect_uplink(void)
{
    wifi_config_t sta = {0};
    uint8_t bssid[6];
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &sta));

    for (size_t i = 0; i < sizeof(recovery_channels); ++i) {
        uint8_t channel = recovery_channels[recovery_index];
        recovery_index = (recovery_index + 1) % sizeof(recovery_channels);
        if (!find_uplink(channel, bssid)) {
            continue;
        }
        sta.sta.channel = channel;
        sta.sta.bssid_set = true;
        memcpy(sta.sta.bssid, bssid, 6);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    sta.sta.channel = 0;
    sta.sta.bssid_set = false;
    memset(sta.sta.bssid, 0, sizeof(sta.sta.bssid));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) {
        return;
    }

    if (id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *event = (const wifi_event_sta_connected_t *)data;
        recovery_index = 0;
        if (event != NULL) {
            wifi_config_t sta = {0};
            if (esp_wifi_get_config(WIFI_IF_STA, &sta) == ESP_OK) {
                sta.sta.channel = event->channel;
                sta.sta.bssid_set = true;
                memcpy(sta.sta.bssid, event->bssid, 6);
                esp_wifi_set_config(WIFI_IF_STA, &sta);
            }
        }
        set_radio();
        return;
    }

    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        set_napt(false);
        reconnect_uplink();
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != IP_EVENT) {
        return;
    }
    if (id == IP_EVENT_STA_GOT_IP) {
        set_napt(true);
    } else if (id == IP_EVENT_STA_LOST_IP) {
        set_napt(false);
    }
}

void router_core_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    assert(sta_netif != NULL && ap_netif != NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.wifi_task_core_id = 0;
    cfg.static_rx_buf_num = 24;
    cfg.dynamic_rx_buf_num = 85;
    cfg.static_tx_buf_num = 32;
    cfg.cache_tx_buf_num = 32;
    cfg.dynamic_tx_buf_num = 32;
    cfg.tx_buf_type = 1;
    cfg.rx_ba_win = 32;
    cfg.tx_ba_win = 32;
    cfg.ampdu_rx_enable = 1;
    cfg.ampdu_tx_enable = 1;
    cfg.amsdu_tx_enable = 1;
    cfg.nvs_enable = 0;
    cfg.sta_disconnected_pm = false;

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event, NULL));
    configure_interfaces();
    set_ap_network();
    set_napt(false);
    ESP_ERROR_CHECK(esp_wifi_start());
}
