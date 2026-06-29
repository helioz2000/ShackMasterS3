/*  
 * See README.md for details
*/

#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "appevents.h"
#include "definitions.h"
#include "rgb_led.h"
#include "wifi.h"
#include "httpserver.h"
#include "usbhost.h"

#define BOOT_BUTTON_GPIO_PIN 0

static const char *TAG = "SM-S3_main";

QueueHandle_t app_event_queue = NULL;
nvs_handle_t nvs_cfg_handle;
bool activate_AP = true;        // Activate AP as a default

extern char wifi_sta_ip_str[];
extern esp_ip4_addr_t wifi_sta_ip;

// WiFi Station (Client) events
void handle_wifi_sta_events (app_event_queue_t evt_queue) {

    if (evt_queue.wifi.event_base == IP_EVENT && evt_queue.wifi.event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Successfully connected! Got IP: " IPSTR, IP2STR(&wifi_sta_ip));
        start_webserver();
        rgb_led_set_color(0,128,0);
        rgb_led_flash(false,0);
        rgb_led_pulse_every(true);    // Start pulsing LED to indicate server is running
    }

    if (evt_queue.wifi.event_base == WIFI_EVENT && evt_queue.wifi.event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi Client disconnected");
        rgb_led_pulse_every(false);
        rgb_led_set_color(64,0,0);    // Flashing Red to indicate disconnected
        rgb_led_flash(true,0);
    }

    if (evt_queue.wifi.event_base == WIFI_EVENT && evt_queue.wifi.event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Driver is loaded");
    }
}

// WiFi Access Point events
void handle_wifi_ap_events (app_event_queue_t evt_queue) {

    wifi_sta_list_t wifi_sta_list;

    if (evt_queue.wifi.event_id == WIFI_EVENT_AP_START) {
        rgb_led_set_color(0, 0, 32); // Set LED to blue to indicate softAP is running
        rgb_led_flash(true, 0); // Start flashing LED to indicate AP is active
    } else if (evt_queue.wifi.event_id == WIFI_EVENT_AP_STACONNECTED) {
        start_webserver();
        rgb_led_flash(false, 0); // Stop flashing LED to indicate connection established
        rgb_led_on(true);   // LED on steady
    } else if (evt_queue.wifi.event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        esp_err_t ret = esp_wifi_ap_get_sta_list(&wifi_sta_list);   // checl hwo many clients are connected
        if (ret == ESP_OK) {
            if (wifi_sta_list.num < 1) rgb_led_flash(true, 0); // Start flashing LED to indicate connection established
        } else {
            ESP_LOGW(TAG, "esp_wifi_ap_get_sta_list returned error");
        }
    }else {
        ESP_LOGW(TAG, "APP_EVENT_WIFI_AP event %d not implemented", evt_queue.wifi.event_id);
    }
}

void boot_button (void) {
    // Configure boot button input
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO_PIN), // Bitmask of the pin(s)
        .mode = GPIO_MODE_INPUT,                  // Set as input mode
        .pull_up_en = GPIO_PULLUP_ENABLE,         // Enable internal pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,     // Disable internal pull-down
        .intr_type = GPIO_INTR_DISABLE            // Disable interrupts for polling
    };
    gpio_config(&io_conf);

    // Wait for 2 seconds for use to press boot button
    for(int i = 1; i<20; i++) {
        if (gpio_get_level(BOOT_BUTTON_GPIO_PIN) == 0) {
            activate_AP = true;
            ESP_LOGI(TAG, "AP activatedBoot button activated (ESP_WIFI_MODE_AP)");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_init(void) {
    size_t required_size = 0;
    // Set to local timezone (e.g., AEST - Australian Eastern Standard Time)
    setenv("TZ", "AEST-10AEDT,M10.1.0,M4.1.0", 1);
    tzset();
    // Initialize the RGB LED
    rgb_led_init(); 
    rgb_led_set_color(64, 0, 64); // Set LED to Magenta to indicate startup
    rgb_led_on(true); // Turn on the LED

    //Initialize NVS (Non Volatile Storage) system
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Check if WiFi SSID has been stored in NVS
    // If no SSID is found activiate AP mode
    // If SSID is present the boot button can be used to forec AP mode
    if (nvs_open(NVS_CFG_NAMESPACE, NVS_READWRITE, &nvs_cfg_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle!");
    } else {
        ESP_LOGI(TAG, "NVS open success");
        ret = nvs_get_str(nvs_cfg_handle, NVS_CFG_WIFI_SSID, NULL, &required_size);
        if ((ret == ESP_OK) && (required_size > 0)) {
            activate_AP = false;
            boot_button();
        }
    }

    rgb_led_set_color(0, 32, 0); // Set LED to green to indicate startup
}

void app_main(void)
{
    app_init();     //

    // Configure event queue for async processing
    app_event_queue_t evt_queue;
    app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));
    if (activate_AP) {
        wifi_init_softap();
    } else {
        wifi_init_client();
    }

    // Start USB HID host
    usb_hid_init();

    // Main task loop
    while (true) {
        // Retrieve events from queue
        if (xQueueReceive(app_event_queue, &evt_queue, portMAX_DELAY)) {

            if (APP_EVENT == evt_queue.event_group) {
                ESP_LOGW(TAG, "APP_EVENT not implemented");
            }

            if (APP_EVENT_HID_HOST ==  evt_queue.event_group) {
                hid_host_device_event(evt_queue.hid_host_device.handle, evt_queue.hid_host_device.event, evt_queue.hid_host_device.arg);
            }

            if (APP_EVENT_HID_INTERFACE == evt_queue.event_group) {
                hid_host_interface_event(evt_queue.hid_host_device.handle, evt_queue.hid_host_device.event, evt_queue.hid_host_device.arg);
            }

            // Handle WiFi AP events
            if (APP_EVENT_WIFI_AP ==  evt_queue.event_group) {
                handle_wifi_ap_events(evt_queue);
            }

            // Handle WiFi client events
            if (APP_EVENT_WIFI_STA == evt_queue.event_group) {
                handle_wifi_sta_events(evt_queue);
            }
        } 
        //vTaskDelay(pdMS_TO_TICKS(100)); // Main task can perform other work or sleep
    }

}
