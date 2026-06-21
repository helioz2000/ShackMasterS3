#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "esp_http_server.h"

httpd_handle_t start_webserver(void);
void stop_webserver(void);

#ifdef __cplusplus
}
#endif

