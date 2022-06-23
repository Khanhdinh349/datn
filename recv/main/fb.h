#ifndef FB_H
#define FB_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"


#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "tcpip_adapter.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

typedef struct 
{
   int ID;//node id
   float CO;//CO
   float UV;//UV
   float H; //Humi
   float T; //Temp
   float D; //Dust2.5
   float D10; //Dust10
   int hour; 
   int min;
}fb_data_t;

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
void firebase_task_realtime(void* param);
void firebase_task_timestamp(void* param);
#endif