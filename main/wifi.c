/*
 * WiFi Client and SoftAP
*/

#include <string.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "appevents.h"
#include "definitions.h"
#include "wifi.h"

static const char *TAG = "SM-S3_wifi";

extern QueueHandle_t app_event_queue;
extern nvs_handle_t nvs_cfg_handle;

// Access Point parameters:
#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define GTK_REKEY_INTERVAL 0
#endif

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

char wifi_sta_ip_str[16] = "";   // WiFi Station IP
esp_ip4_addr_t wifi_sta_ip;             // WiFi Station IP

// Event handler for Wi-Fi and IP events
static void wifi_sta_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    bool hide_event = false;    // DO not pass envent onto main app
    app_event_queue_t evt_queue = {
        .event_group = APP_EVENT_WIFI_STA,
        .wifi.event_id = event_id,
        .wifi.event_base = event_base,
        .wifi.event_data = NULL
    };

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Connect to the AP once the Wi-Fi driver is initialized
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Handle disconnection and attempt retries
        if (s_retry_num < WIFI_STA_MAX_RETRIES) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying connection to the AP (Attempt %d/%d)", s_retry_num, WIFI_STA_MAX_RETRIES);
            hide_event = true;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGE(TAG,"Failed to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Successfully connected and received an IP address via DHCP
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        wifi_sta_ip = event->ip_info.ip;        // Save IP address
        esp_ip4addr_ntoa(&wifi_sta_ip, wifi_sta_ip_str, sizeof(wifi_sta_ip_str));
        //ESP_LOGI(TAG, "Successfully connected! Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    if ((app_event_queue) && !hide_event) xQueueSend(app_event_queue, &evt_queue, 0);
}

// WiFi Sstation event handler
bool wifi_init_client(void) {
    s_wifi_event_group = xEventGroupCreate();

    // Initialize the underlying TCP/IP network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create the default system event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Setup default Wi-Fi hardware configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    
    // Register event handlers for Wi-Fi states and IP allocation
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL, &instance_got_ip));

    // Set Wi-Fi Credentials
    size_t ssid_size = 32;
    char wifi_ssid_str[ssid_size];
    if (nvs_get_str(nvs_cfg_handle, NVS_CFG_WIFI_SSID, wifi_ssid_str, &ssid_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed ot get WiFi SSID from NVS");
        return false;
    }
    size_t pwd_size = 64;
    char wifi_pwd_str[pwd_size];
    if (nvs_get_str(nvs_cfg_handle, NVS_CFG_WIFI_PWD, wifi_pwd_str, &pwd_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed ot get WiFi password from NVS");
        return false;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Minimum security level accepted
        },
    };
    memcpy(wifi_config.sta.ssid, wifi_ssid_str, ssid_size);
    memcpy(wifi_config.sta.password, wifi_pwd_str, pwd_size);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    
    // Start the Wi-Fi hardware driver
    ESP_ERROR_CHECK(esp_wifi_start() );

    //ESP_LOGI(TAG, "wifi_init_sta finished.");

    /*
    // Block application execution until one of the WIFI bits is set - timout is indefinite
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    // Evaluate connection result
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", wifi_ssid_str);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", wifi_ssid_str);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    */
    return true;
}

// WiFi AP event handler
static void wifi_ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    bool hide_event = false;
    app_event_queue_t evt_queue = {
        .event_group = APP_EVENT_WIFI_AP,
        .wifi.event_id = event_id,
        .wifi.event_base = event_base,
        .wifi.event_data = event_data
    };

    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED:
        wifi_event_ap_staconnected_t* con_event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(con_event->mac), con_event->aid);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        wifi_event_ap_stadisconnected_t* discon_event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d", MAC2STR(discon_event->mac), discon_event->aid, discon_event->reason);
        break;
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "WiFi soft-AP started");
        break;
    case WIFI_EVENT_AP_STOP:
        ESP_LOGI(TAG, "WiFi soft-AP stopped");
        break;
    default:
        ESP_LOGW(TAG, "WIFI_AP event %d not implemented", event_id);
        hide_event = true;        // event will not be added to event queue
        break;
    }
    if ((app_event_queue) && !hide_event) xQueueSend(app_event_queue, &evt_queue, 0);
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_ap_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                .protected_keep_alive = 1,
            },
#endif
            .gtk_rekey_interval = GTK_REKEY_INTERVAL,
        },
    };
    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d", ESP_WIFI_SSID, ESP_WIFI_PASS, ESP_WIFI_CHANNEL);
}
