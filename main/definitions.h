#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_STA_MAX_RETRIES 5

// Note: NVS key identifiers must be no more than 15 characters long
#define NVS_CFG_NAMESPACE "SMS3_cfg"
#define NVS_CFG_WIFI_SSID "wifi_cl_ssid"
#define NVS_CFG_WIFI_PWD "wifi_cl_pwd"

#ifdef __cplusplus
}
#endif