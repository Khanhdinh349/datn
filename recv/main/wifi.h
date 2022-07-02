#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define SSID "1B KMT"
#define PASS "a1234567"

static esp_err_t event_handler(void *ctx, system_event_t *event);
void wifi_connection_start(void);
void wifi_connection_begin(void);
void wifi_connection_end(void);