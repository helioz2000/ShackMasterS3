/*
 * USB HID host
 * for ShackMaster USB connection
*/

#include "stdio.h"
#include "time.h"
#include "stdint.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "sdkconfig.h"

#include "appevents.h"
#include "usbhost.h"

// Define the USB device details - all other devices will be rejected
#define TARGET_VENDOR_ID   0x0483       //STMicroelectronics
#define TARGET_PRODUCT_ID  0xA1DE       //RigExpert ShackMaster 600 

static const char *TAG = "SM-S3_usbhost";

bool sm_is_connected = false;
bool sm_awaiting_response = false;
hid_host_device_handle_t sm_device_handle;
TimerHandle_t sm_response_timer = NULL;
#define SM_RESPONSE_TIMEOUT_MS 300

// USB device details
char usb_dev_manufacturer[HID_STR_DESC_MAX_LENGTH] = {0};
char usb_dev_product[HID_STR_DESC_MAX_LENGTH] = {0};
char usb_dev_serial[HID_STR_DESC_MAX_LENGTH] = {0};

#define SM_HID_REPORT_ID 7      // HID report ID for Shackmaster
#define CMD_LEN 63
unsigned char cmd_buf[CMD_LEN] = { 0 };
unsigned char cmd_power_onoff[] = {SM_HID_REPORT_ID, 6, 'P','S','W','x', 0x0d, 0x0a};
unsigned char cmd_power_status[] = {SM_HID_REPORT_ID, 6, 'P','O','W','E','R', 0x0a};
unsigned char cmd_get_analogs[] = {SM_HID_REPORT_ID, 1, 0x0c};

extern QueueHandle_t app_event_queue;

// Analog value storage
sm_values_t sm_values;

/**
 * @brief Non-blocking timer callback.
 * 
 * Fires instantly on cycle, consumes near-zero CPU.
 * A reply from SM should be received well before this function is called
 * and sm_awaiting_response should be false.
 * Check if sm_awaiting_response is true which indicates no response has been received. 
 */
void sm_response_timer_callback(TimerHandle_t xTimer) {
    //ESP_LOGI(TAG, "%s", __func__ );
    if (!sm_awaiting_response) return;
    sm_awaiting_response = false;
    ESP_LOGW(TAG, "SM failed to respond");
}

/**
 * @brief Send data to USB device
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] report_id          USB HID report identifier 
 * @param[in] data               Pointer to data buffer for byte sequence
 * @param[in] data_len           Length of data in buffer
 * @param[in] expect_response    True if a response from the device is expected
 */
esp_err_t send_hid_output(hid_host_device_handle_t hid_device_handle, uint8_t report_id, uint8_t *data, size_t data_len, bool expect_response)
{
    //ESP_LOGI(TAG,"send_hid_output()");
    if (!sm_is_connected) {
        ESP_LOGW(TAG,"Shackmaster is not connected - HID output failed");
        return ESP_FAIL;
    }

    if (hid_device_handle == NULL) {
        ESP_LOGE(TAG, "Device handle is invalid.");
        return ESP_ERR_INVALID_ARG;
    }

    if (sm_awaiting_response) {
        ESP_LOGW(TAG,"Still waiting for reponse to last Shackmaster command - HID output failed");
        return ESP_FAIL;
    }

    // Set a report via the Control Endpoint (or interrupt out pipe if configured)
    esp_err_t err = hid_class_request_set_report( hid_device_handle, HID_REPORT_TYPE_OUTPUT, report_id,  data, data_len );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transfer failed: %s", esp_err_to_name(err));
        return err;
    }

    //ESP_LOGI(TAG, "Sent %d bytes to device successfully.", data_len); 
    if (expect_response) {
        sm_awaiting_response = true;
        xTimerStart(sm_response_timer, 0);
    } else {
        sm_awaiting_response = false;
    }
    //ESP_LOGI(TAG,"send_hid_output() Done");

    return ESP_OK;
}

/**
 * @brief Switch ShackMaster power on/off
 */
bool sm_set_power(bool newState) {
    // clear command buffer
    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, cmd_power_onoff, sizeof(cmd_power_onoff));
    if(newState) {
        cmd_buf[5] = '1';       // Switch ON
    } else {
        cmd_buf[5] = '0';       // Switch OFF
    }

    if ( send_hid_output(sm_device_handle, SM_HID_REPORT_ID, cmd_buf, sizeof(cmd_buf), true) == ESP_OK) {
        return true;
    }
    return false;
}

/**
 * @brief Read analog values  from ShackMaster
 */
bool sm_get_values() {
    //ESP_LOGI(TAG, "%s", __func__);
    memset(cmd_buf, 0, sizeof(cmd_buf));    // clear command buffer
    memcpy(cmd_buf, cmd_get_analogs, sizeof(cmd_get_analogs));
    if ( send_hid_output(sm_device_handle, SM_HID_REPORT_ID, cmd_buf, sizeof(cmd_buf), true) == ESP_OK) {
        return true;
    }
    ESP_LOGI(TAG,"sm_get_values() failed");
    return false;
}

/**
 * @brief Read power status from ShackMaster
 */
bool sm_get_power(void) {
    //ESP_LOGI(TAG, "%s", __func__);
    // clear command buffer
    memset(cmd_buf, 0, sizeof(cmd_buf));
    memcpy(cmd_buf, cmd_power_status, sizeof(cmd_power_onoff));
    if ( send_hid_output(sm_device_handle, SM_HID_REPORT_ID, cmd_buf, sizeof(cmd_buf), true) == ESP_OK) {
        return true;
    }
    return false;
}

/**
 * @brief Print all data in the sm_values_t structure 
 *
 * the date/time is printed in UTC format
 *
 * @param[in] data    Pointer to data buffer for recevied byte sequence
 * @param[in] length  Length of data in buffer
 */
void print_sm_values() {
    printf ("%d.%dV %d.%dA\n", sm_values.voltage / 10, sm_values.voltage % 10, sm_values.current / 10, sm_values.current % 10); 
    printf ("USB1 %d.%dV %d.%dA\n", sm_values.usb1_v / 10, sm_values.usb1_v % 10, sm_values.usb1_a/100, sm_values.usb1_a % 100);
    printf ("USB2 %d.%dV %d.%dA\n", sm_values.usb2_v / 10, sm_values.usb2_v % 10, sm_values.usb2_a/100, sm_values.usb2_a % 100);
    printf ("USB3 %d.%dV %d.%dA\n", sm_values.usb3_v / 10, sm_values.usb3_v % 10, sm_values.usb3_a/100, sm_values.usb3_a % 100);
    printf ("USB4 %d.%dV %d.%dA\n", sm_values.usb4_v / 10, sm_values.usb4_v % 10, sm_values.usb4_a/100, sm_values.usb4_a % 100);
    printf("Current: %d.%dA (Max:%dA)\n", sm_values.current_tot / 10, sm_values.current_tot % 10 , sm_values.current_max);
    printf("Temp In: %d Out: %d Fan: %d%% \n", sm_values.temp_in, sm_values.temp_out, sm_values.fan_duty);
    printf("Mains %dV %d.%dA %d.%dHz\n", sm_values.supply_V, sm_values.current_tot / 10, sm_values.current_tot % 10, sm_values.supply_F / 10, sm_values.supply_F % 10);
    printf("Power: %dW (Max:%dW)\n", sm_values.power_tot, sm_values.power_max);
    printf("Serial: 5003%05ld Version %d.%d.%d\n", sm_values.serial, sm_values.ver_major, sm_values.ver_minor, sm_values.ver_build);
    printf("Position: %d Error 1: %04x Error 2: %04x\n", sm_values.acc_pos, sm_values.err1, sm_values.err2);
    time_t raw_time = (time_t)sm_values.time;
    struct tm time_info;
    char time_string[64];
    localtime_r(&raw_time, &time_info);
    strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", &time_info);
    printf("Current Date/Time: %s\n", time_string);
}

/**
 * @brief Decode "Get Analog Values" data received from Shackmaster 
 *
 * the byte sequence is mapped into an sm_values_t structure
 * data items > 8 bytes are byte swapped 
 *
 * @param[in] data    Pointer to data buffer for recevied byte sequence
 * @param[in] length  Length of data in buffer
 */
void sm_values_decode(const uint8_t *const data, const int length){
    // print raw data buffer
    //for (int i = 0; i < length; i++) { printf("%02X ", data[i]); }
    //printf("\r\nNum Bytes: %d\n", data[1]);
    // Note: the PDU length in data[1] is incorrect in sm firmware V1.1.2, is should be 47 bytes
    memcpy(&sm_values, &data[3], sizeof(sm_values));
    // Byteswap all 16 bit values
    sm_values.current = (int16_t)__builtin_bswap16((uint16_t)sm_values.current);
    sm_values.usb1_a = (int16_t)__builtin_bswap16((uint16_t)sm_values.usb1_a);
    sm_values.supply_V = (int16_t)__builtin_bswap16((uint16_t)sm_values.supply_V);
    sm_values.supply_F = (int16_t)__builtin_bswap16((uint16_t)sm_values.supply_F);
    sm_values.power_tot = (int16_t)__builtin_bswap16((uint16_t)sm_values.power_tot);
    sm_values.current_tot = (int16_t)__builtin_bswap16((uint16_t)sm_values.current_tot);
    sm_values.power_max = (int16_t)__builtin_bswap16((uint16_t)sm_values.power_max);
    sm_values.current_max = (int16_t)__builtin_bswap16((uint16_t)sm_values.current_max);
    sm_values.usb2_a = (int16_t)__builtin_bswap16((uint16_t)sm_values.usb2_a);
    sm_values.usb3_a = (int16_t)__builtin_bswap16((uint16_t)sm_values.usb3_a);
    sm_values.usb4_a = (int16_t)__builtin_bswap16((uint16_t)sm_values.usb4_a);
    sm_values.err1 = (uint16_t)__builtin_bswap16((uint16_t)sm_values.err1);
    sm_values.err2 = (uint16_t)__builtin_bswap16((uint16_t)sm_values.err2);
    sm_values.serial = (uint32_t)__builtin_bswap32((uint32_t)sm_values.serial);
    sm_values.time = (uint32_t)__builtin_bswap32((uint32_t)sm_values.time);
    //print_sm_values();
}

void sm_values_zero() {
    memset(&sm_values, 0, sizeof(sm_values));
}

/**
 * @brief USB HID Host Interface report callback handler
 *
 * @param[in] data    Pointer to input report data buffer
 * @param[in] length  Length of input report data buffer
 */
static void hid_host_report_callback(const uint8_t *const data, const int length) {
    //ESP_LOGI(TAG,"%s", __func__);
    char response[64] = { 0 };
    if (data[0] != SM_HID_REPORT_ID) {
        ESP_LOGE(TAG, "%s - Unexpected report ID %d received from %s: %d", __func__, data[0], usb_dev_product );
        return;
    }

    sm_awaiting_response = false;

    // Analog value response?
    if (data[1] > 20 && (data[2] == 0x0c)) {
        //printf("Ananlog values received"); 
        sm_values_decode(data, length);
    } else {    // everything else is ASCII
        //int len = data[1];      // data length
        //for (int i = 0; i < len+2; i++) { printf("%02X ", data[i]); }
        //printf("\r\n");
        memcpy(response, &data[2], data[1]);
        //printf("SM response: %s", response);        // CR LF is contains in SM response
    }
}

/**
 * @brief USB HID Host interface callback
 *
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host interface event
 * @param[in] arg                Pointer to arguments, does not used
 */
void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    //ESP_LOGI(TAG,"%s - event: %d", __func__, event);
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        //ESP_LOGI(TAG, "%s - HID_HOST_INTERFACE_EVENT_INPUT_REPORT", __func__);
        memset(data,0, sizeof(data));
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, data, 64, &data_length));
        hid_host_report_callback(data, data_length);
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        sm_is_connected = false;
        ESP_LOGI(TAG, "HID DISCONNECT: %s S/N: %s", usb_dev_product, usb_dev_serial );
        // Notify main application
        const app_event_queue_t evt_queue = {
            .event_group = APP_EVENT_HID_INTERFACE,
            // HID Host Device related info         
            .hid_host_device.handle = hid_device_handle,
            .hid_host_device.event = event,
            .hid_host_device.arg = arg
        };
        if (app_event_queue) {
            xQueueSend(app_event_queue, &evt_queue, 0);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGE(TAG, "%s - HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR, __func__");
        sm_awaiting_response = false;
        break;
    default:
        ESP_LOGW(TAG, "HID Device - Unhandled event: %d (possibly suspend/resume)", event);
        break;
    }
}

/**
 * @brief Convert string from wide format to ascii
 *
 * @param[in]  src  source string in wide format (wchar)
 * @param[out] dst  destination string in ASCII format
 */
void parse_usb_wchar(const wchar_t *src, char *dest, size_t dest_max_len) {
    size_t i = 0;
    // Loop until we find an actual null terminator or hit our destination limit
    while (src[i] != 0 && i < (dest_max_len - 1)) {
        // Extract only the lower 8 bits of the wide character
        char c = (char)(src[i] & 0xFF);
        
        // Safety check: if an unexpected null byte is inline, don't break early
        // if there's text, but treat actual zeroes as a string end.
        if (c == '\0') {
            break;
        }
        dest[i] = c;
        i++;
    }
    dest[i] = '\0'; // Enforce clean null termination
}

/**
 * @brief Read USB Device information
 *
 * @param[in] hid_device_handle  HID Device handle
 */
void read_device_details(hid_host_device_handle_t hid_device_handle) {
    hid_host_dev_info_t dev_info;
    // Query runtime details directly using the public HID API
    ESP_ERROR_CHECK(hid_host_get_device_info(hid_device_handle, &dev_info));
    // Extract and convert each string
    parse_usb_wchar(dev_info.iManufacturer, usb_dev_manufacturer, sizeof(usb_dev_manufacturer));
    parse_usb_wchar(dev_info.iProduct, usb_dev_product, sizeof(usb_dev_product));
    parse_usb_wchar(dev_info.iSerialNumber, usb_dev_serial, sizeof(usb_dev_serial));

    ESP_LOGI(TAG, "DEVICE INFO ====================================");
    ESP_LOGI(TAG, "Vendor ID (VID):   0x%04X", dev_info.VID);
    ESP_LOGI(TAG, "Product ID (PID):  0x%04X", dev_info.PID);
    ESP_LOGI(TAG, "Manufacturer:      %s", usb_dev_manufacturer);
    ESP_LOGI(TAG, "Description:       %s", usb_dev_product);
    ESP_LOGI(TAG, "Serial Number:     %s", usb_dev_serial);
    ESP_LOGI(TAG, "DEVICE INFO ====================================");
}

/**
 * @brief USB HID Host Interface event
 *
 * Gets called via the app_event_queue
 * 
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host Device event
 * @param[in] arg                Pointer to arguments, does not used
 */
void hid_host_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            sm_is_connected = false;
            sm_awaiting_response = false;
            sm_values_zero();
            ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
            usb_dev_manufacturer[0] = 0;
            usb_dev_product[0] = 0;
            usb_dev_serial[0] = 0;
            ESP_LOGI(TAG, "Device %s closed", usb_dev_product);
            break;
        default:
            ESP_LOGW(TAG, "%s - unhandled event %d", __func__, event);
    }
}

/**
 * @brief USB HID Host Device event
 *
 * Gets called via the app_event_queue
 * 
 * @param[in] hid_device_handle  HID Device handle
 * @param[in] event              HID Host Device event
 * @param[in] arg                Pointer to arguments, does not used
 */
void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    hid_host_dev_params_t dev_params;

    //ESP_LOGI(TAG, "%s %d", __func__, event);

    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));    

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HID Device CONNECTED");

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL
        };
        // Open device
        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));

        read_device_details(hid_device_handle);

        //hid_report_protocol_t hid_report_protocol;
        // Start device polling
        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        //ESP_ERROR_CHECK(hid_class_request_get_protocol(hid_device_handle, &hid_report_protocol));
        //ESP_LOGI(TAG, "%s protocol %d", __func__, hid_report_protocol);
        ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_REPORT));
        // store handle for further interaction;
        sm_device_handle = hid_device_handle;
        sm_is_connected = true;
        sm_get_power();
        break;
    default:
        ESP_LOGE(TAG, "%s - event %d not handled", __func__, event);
        break;
    }
}

/**
 * @brief HID Host Device callback
 *
 * Puts new HID Device event to the queue
 *
 * @param[in] hid_device_handle HID Device handle
 * @param[in] event             HID Device event
 * @param[in] arg               Not used
 */
void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg)
{
    ESP_LOGI(TAG, "%s - event %d", __func__, event);
    const app_event_queue_t evt_queue = {
        .event_group = APP_EVENT_HID_HOST,
        // HID Host Device related info
        .hid_host_device.handle = hid_device_handle,
        .hid_host_device.event = event,
        .hid_host_device.arg = arg
    };

    if (app_event_queue) {
        xQueueSend(app_event_queue, &evt_queue, 0);
    }
}

/**
 * @brief Timer task
 * 
 * to request ananlog values from SM
 *
 */
void hid_timer_task(void *pvParameters)
{
    // Define our execution period (2000ms converted to FreeRTOS system ticks)
    const TickType_t xPeriod = pdMS_TO_TICKS(2000);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    ESP_LOGI(TAG, "%s - task started.", __func__);

    while (1) {
        // Wait here until exactly 2 seconds have elapsed since the last wake time
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

       //ESP_LOGI(TAG, "%s - Repeating 2-second TX triggered. (%d)", __func__, sm_is_connected);

        // Check if a device is connected before acting
        if (sm_is_connected && (sm_device_handle != NULL) && !sm_awaiting_response ) {
            if (!sm_get_values()) {
                ESP_LOGE(TAG, "SM Get Analog Values request send failed");
            }
        } else {
            ESP_LOGD(TAG, "Device not connected or busy. Skipping periodic transmission.");
        }
    }
    ESP_LOGW(TAG, "Repeating 2-second TX task exited.");
}

void usb_hid_init(void) {
    BaseType_t usb_task_created, timer_task_created;
    /*
    * Create usb_lib_task to:
    * - initialize USB Host library
    * - Handle USB Host events while APP pin in in HIGH state
    */
    usb_task_created = xTaskCreatePinnedToCore(usb_host_task, "usb_events", 4096, xTaskGetCurrentTaskHandle(), 2, NULL, 0);
    assert(usb_task_created == pdTRUE);

    // Wait for notification from usb_lib_task to proceed
    ulTaskNotifyTake(false, 1000);

    // Create the timer task
    timer_task_created = xTaskCreatePinnedToCore(hid_timer_task,"hid_timer_task", 4096, NULL, 2, NULL, 0);
    assert(timer_task_created == pdTRUE);

    /*
    * HID host driver configuration
    * - create background task for handling low level event inside the HID driver
    * - provide the device callback to get new HID Device connection event
    */
    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };

    ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));

    // Create queue
    //app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));

    // Create timer to check SM response
    sm_response_timer = xTimerCreate( "SM Response Timer", pdMS_TO_TICKS(SM_RESPONSE_TIMEOUT_MS), pdFALSE, NULL, sm_response_timer_callback );

    ESP_LOGI(TAG, "Waiting for HID Device to be connected");
}

void usb_hid_deinit(void) {
    ESP_LOGI(TAG, "HID Driver uninstall");
    ESP_ERROR_CHECK(hid_host_uninstall());
}

#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
/**
 * @brief Callback for USB device enumeration filtering
 *
 * @param[in] device_desc  USB device description struct
 * @return bool value, true to indicate it the device enumeration is accepted, false to reject device
 * 
 * @note USB_HOST_ENABLE_ENUM_FILTER_CALLBACK must be enabled (menuconfig) for this function to work
 */
bool usb_host_enum_filter_cb(const usb_device_desc_t *device_desc, unsigned char *bConfigurationValue) {

    ESP_LOGI(TAG, "VID: 0x%04X, PID: 0x%04X. Opening interface...\n", device_desc->idVendor, device_desc->idProduct);

    if (device_desc->idVendor != TARGET_VENDOR_ID) {
        ESP_LOGE(TAG," vendor ID mismatch, expecting 0x%04X", TARGET_VENDOR_ID);
        return false;
    }
    if (device_desc->idProduct != TARGET_PRODUCT_ID) {
        ESP_LOGE(TAG," vendor ID mismatch, expecting 0x%04X", TARGET_PRODUCT_ID);
        return false;
    }
    return true; // Accept device
}
#endif

/**
 * @brief Start USB Host install and handle common USB host library events
 *
 * @param[in] arg  Not used
 */
void usb_host_task(void *arg) {
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
        .enum_filter_cb = usb_host_enum_filter_cb,
#endif
    };

#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    ESP_LOGI(TAG,"USB host enumeration filter: VendorID 0x%04X ProductID 0x%04X", TARGET_VENDOR_ID, TARGET_PRODUCT_ID);
#else
    ESP_LOGI(TAG,"USB host enumeration filter OFF");
#endif

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive(arg);

    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        // In this example, there is only one client registered
        // So, once we deregister the client, this call must succeed with ESP_OK
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
            break;
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            // Executing when the USB device has been disconnected
            //ESP_LOGI(TAG,"usb_host_task() USB_HOST_LIB_EVENT_FLAGS_ALL_FREE");
        }
    }

    ESP_LOGI(TAG, "USB shutdown");
    // Clean up USB Host
    vTaskDelay(10); // Short delay to allow clients clean-up
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskDelete(NULL);
}

