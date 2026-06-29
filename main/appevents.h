#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "usb/hid_host.h"
#include "esp_event.h"

/**
 * @brief APP event group
 *
 * Application logic can be different. There is a one among other ways to distinguish the
 * event by application event group.
 * In this example we have two event groups:
 * APP_EVENT            - General event, which is APP_QUIT_PIN press event (Generally, it is IO0).
 * APP_EVENT_HID_HOST   - HID Host Driver event, such as device connection/disconnection or input report.
 */
typedef enum {
    APP_EVENT = 0,
    APP_EVENT_WIFI_AP,
    APP_EVENT_WIFI_STA,
    APP_EVENT_HID_HOST,
    APP_EVENT_HID_INTERFACE
} app_event_group_t;

/**
 * @brief APP event queue
 *
 * This event is used for delivering WiFi AP state from callback to a task.
 */
typedef struct {
    app_event_group_t event_group;
    // WiFi Events fot AP & STA
    struct {
        int32_t event_id;
        esp_event_base_t event_base;
        void* event_data;
    } wifi;
    // HID Host - Device related info
    struct {
        hid_host_device_handle_t handle;
        hid_host_driver_event_t event;
        void *arg;
    } hid_host_device;
} app_event_queue_t;

#ifdef __cplusplus
}
#endif