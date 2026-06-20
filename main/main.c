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
#include "rgb_led.h"
#include "wifi.h"

#define BOOT_BUTTON_GPIO_PIN 0

static const char *TAG = "SM-S3_main";

QueueHandle_t app_event_queue = NULL;

bool activate_AP = false;

void handle_wifi_ap_events (app_event_queue_t evt_queue) {

    wifi_sta_list_t wifi_sta_list;

    if (evt_queue.wifi_ap.event_id == WIFI_EVENT_AP_START) {
        rgb_led_set_color(0, 0, 32); // Set LED to blue to indicate softAP is running
        rgb_led_flash(true, 0); // Start flashing LED to indicate AP is active
    } else if (evt_queue.wifi_ap.event_id == WIFI_EVENT_AP_STACONNECTED) {
       rgb_led_flash(false, 0); // Stop flashing LED to indicate connection established
       rgb_led_on(true);   // LED on steady
    } else if (evt_queue.wifi_ap.event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        esp_err_t ret = esp_wifi_ap_get_sta_list(&wifi_sta_list);   // checl hwo many clients are connected
        if (ret == ESP_OK) {
            if (wifi_sta_list.num < 1) rgb_led_flash(true, 0); // Start flashing LED to indicate connection established
        } else {
            ESP_LOGW(TAG, "esp_wifi_ap_get_sta_list returned error");
        }
    }else {
        ESP_LOGW(TAG, "APP_EVENT_WIFI_AP event %d not implemented", evt_queue.wifi_ap.event_id);
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

    // Check boot button
    boot_button();
    rgb_led_set_color(0, 32, 0); // Set LED to green to indicate startup
}

void app_main(void)
{
    app_init();     //

    // Configure event queue for async processing
    app_event_queue_t evt_queue;
    app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));

    wifi_init_softap();

    // main task loop
    while (true) {
        // Retrieve events from queue
        if (xQueueReceive(app_event_queue, &evt_queue, portMAX_DELAY)) {

            if (APP_EVENT == evt_queue.event_group) {
                ESP_LOGW(TAG, "APP_EVENT not implemented");
            }

            // Handle WiFi AP events
            if (APP_EVENT_WIFI_AP ==  evt_queue.event_group) {
                handle_wifi_ap_events(evt_queue);
            }

            // Handle WiFi client events
            if (APP_EVENT_WIFI_CLIENT == evt_queue.event_group) {
                ESP_LOGW(TAG, "APP_EVENT_WIFI_CLIENT not implemented");
            }
        } 
        //vTaskDelay(pdMS_TO_TICKS(100)); // Main task can perform other work or sleep
    }

}
