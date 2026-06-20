#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"

#include "led_strip_encoder.h"
#include "rgb_led.h"

#define LED_STRIP_GPIO      48
#define LED_NUM_PIXELS      1        // Number of WS2812 devices in series
#define RMT_LED_STRIP_RES   10000000 // 10MHz tick resolution required for WS2812 timing

static const char *TAG = "SM-S3_rgb_led";

// FreeRTOS Binary Semaphore to signal when led data transmission completes
static SemaphoreHandle_t tx_done_sem = NULL;

rmt_channel_handle_t tx_chan = NULL;
rmt_encoder_handle_t led_encoder = NULL;
rmt_transmit_config_t tx_config = {0};
rmt_tx_channel_config_t tx_chan_config = {0};
TimerHandle_t flash_timer = NULL;

// Array buffer: WS2812 expects data sent in physical Green-Red-Blue (GRB) order
uint8_t led_buffer_on[LED_NUM_PIXELS * 3]; 
uint8_t led_buffer_off[LED_NUM_PIXELS * 3] = {0}; // Pre-filled with zeros for "off" state
static bool led_is_on = false; // Track current LED state
unsigned long led_flash_period_ms = 500; // Flash period in milliseconds

// IRAM-safe callback function for the TX Done event
static bool IRAM_ATTR rmt_tx_done_cb(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx) {
    BaseType_t high_task_wakeup = pdFALSE;
    // Give the semaphore to let the application task know the hardware is free
    xSemaphoreGiveFromISR(tx_done_sem, &high_task_wakeup);
    // Return true if giving the semaphore woke up a higher priority task
    return high_task_wakeup == pdTRUE;
}

// Non-blocking timer callback. Fires instantly on cycle, consumes near-zero CPU.
void flash_timer_callback(TimerHandle_t xTimer) {
    //ESP_LOGI(TAG, "flash_timer_callback led_is_on: %d", led_is_on );
    rmt_transmit_config_t async_tx_config = {
        .loop_count = 0,
        .flags.queue_nonblocking = 1 // Hand off to RMT hardware immediately
    };

    if (led_is_on) {
        rmt_transmit(tx_chan, led_encoder, led_buffer_off, sizeof(led_buffer_off), &async_tx_config);
        led_is_on = false;
    } else {
        rmt_transmit(tx_chan, led_encoder, led_buffer_on, sizeof(led_buffer_on), &async_tx_config);
        led_is_on = true;
    }
}

// Helper to safely format colors into memory
void rgb_led_set_pixel_grb(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < LED_NUM_PIXELS) {
        led_buffer_on[index * 3 + 0] = g; // Green first
        led_buffer_on[index * 3 + 1] = r; // Red second
        led_buffer_on[index * 3 + 2] = b; // Blue third
    }
}

void rgb_led_show_status() {
    ESP_LOGI(TAG, "led_buffer_on: [%d, %d, %d] led_is_on: %d", led_buffer_on[0], led_buffer_on[1], led_buffer_on[2], led_is_on);
}

void rgb_led_init() {
    ESP_LOGI(TAG, "Initializing RMT TX Channel for v6 Driver...");

    // Initialize our binary semaphore
    tx_done_sem = xSemaphoreCreateBinary();
    // Pre-give it so the very first call to transmit can run immediately
    xSemaphoreGive(tx_done_sem);

    // Create an RMT TX channel
    tx_chan_config.gpio_num = LED_STRIP_GPIO;
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_chan_config.resolution_hz = RMT_LED_STRIP_RES;
    tx_chan_config.mem_block_symbols = 64; // Standard buffer depth
    tx_chan_config.trans_queue_depth = 4;  // Allows queuing successive frames
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

    // Register our custom callback events structure to the RMT channel
    rmt_tx_event_callbacks_t cbs = {
        .on_trans_done = rmt_tx_done_cb, // Bind the TX complete interrupt
    };
    ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(tx_chan, &cbs, NULL));

    // Enable the RMT TX channel
    ESP_ERROR_CHECK(rmt_enable(tx_chan));

    // Instance the explicit LED Strip Encoder structure
    tx_chan_config.gpio_num = LED_STRIP_GPIO;
    tx_chan_config.clk_src = RMT_CLK_SRC_DEFAULT; 
    tx_chan_config.resolution_hz = RMT_LED_STRIP_RES; 
    tx_chan_config.mem_block_symbols = 64; // Standard buffer depth
    tx_chan_config.trans_queue_depth = 4;  // Allows queuing successive frames

    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RES,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    // Set transactional configs (waiting for delivery to completely finish)
    tx_config.loop_count = 0; // Transmit frame exactly once

        // 2. Create an asynchronous FreeRTOS daemon timer
    flash_timer = xTimerCreate(
        "LED_Flash_Timer", 
        pdMS_TO_TICKS(led_flash_period_ms), 
        pdTRUE, // Auto-reload to flash continuously
        NULL, 
        flash_timer_callback
    );
}

void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue) {
    rgb_led_set_pixel_grb(0, red, green, blue); // Set the first (and only) pixel's color
    if (led_is_on) {
        // If LED is currently on, update the color immediately
        rgb_led_on(true); // Re-send the updated "on" buffer to change color
    }
}

void rgb_led_on(bool onState) {
    //ESP_LOGI(TAG, "rgb_led_on: %d", onState);
    // Wait on the semaphore *only before transmitting* to ensure the last transmission completed
    // This stops you from clobbering the transmission buffer while the DMA/peripheral is using it
    if (xSemaphoreTake(tx_done_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Fires asynchronously and returns instantly — does NOT block thread
        ESP_ERROR_CHECK(rmt_transmit(tx_chan, led_encoder, onState ? led_buffer_on : led_buffer_off, sizeof(led_buffer_on), &tx_config));
        led_is_on = onState; // Update state tracking
    } else {
        ESP_LOGE(TAG, "Previous transmission timed out or busy!");
    }
}

void rgb_led_flash(bool flashState, uint32_t period_ms) {
    // Update the timer period if it's already running
    if (period_ms > 0 && flashState) {
        xTimerChangePeriod(flash_timer, pdMS_TO_TICKS(period_ms), 0);
    }

    if (flashState) {
        xTimerStart(flash_timer, 0);
    } else {
        xTimerStop(flash_timer, 0);
    }
}

void rgb_led_toggle() {
    if (led_is_on) {
        rgb_led_on(false);
    } else {
        rgb_led_on(true);
    }
}

