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
#include "esp_tls.h"
#include "esp_http_client.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lora.h"
#include <malloc.h>
#include "cJSON.h"
#include "fb.h"


#define EXAMPLE_ESP_WIFI_SSID      					"tPhone"
#define EXAMPLE_ESP_WIFI_PASS      					"00112233"
#define EXAMPLE_ESP_MAXIMUM_RETRY  					5
#define WIFI_CONNECTED_BIT 							BIT0
#define WIFI_FAIL_BIT      							BIT1
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD 			WIFI_AUTH_WPA2_PSK

static const char *TAG = "ESP32 GATEWAY";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

void time ()
{
    time_t     now;
    struct tm  ts;
    char       hour[3];
    setenv("TZ", "ICT-7", 1);
    int hour_t;

    // Get current time
    time(&now);

    // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
    ts = *localtime(&now);
    strftime(hour, sizeof(hour), "%H", &ts);
    hour_t = atoi(hour);
    
}
void JSON_Analyze(const cJSON * const root) {
	cJSON *current_element = NULL;;
	cJSON_ArrayForEach(current_element, root) {
		if (current_element->string) {
			const char* string = current_element->string;
			ESP_LOGI("JSON", "[%s]", string);
		}
		if (cJSON_IsInvalid(current_element)) {
			ESP_LOGI("JSON", "Invalid");
		} else if (cJSON_IsFalse(current_element)) {
			ESP_LOGI("JSON", "False");
		} else if (cJSON_IsTrue(current_element)) {
			ESP_LOGI("JSON", "True");
		} else if (cJSON_IsNull(current_element)) {
			ESP_LOGI("JSON", "Null");
		} else if (cJSON_IsNumber(current_element)) {
			int valueint = current_element->valueint;
			double valuedouble = current_element->valuedouble;
			ESP_LOGI("JSON", "int=%d double=%f", valueint, valuedouble);
		} else if (cJSON_IsString(current_element)) {
			const char* valuestring = current_element->valuestring;
			ESP_LOGI("JSON", "%s", valuestring);
		} else if (cJSON_IsArray(current_element)) {
			JSON_Analyze(current_element);
		} else if (cJSON_IsObject(current_element)) {
			JSON_Analyze(current_element);
		} else if (cJSON_IsRaw(current_element)) {
			ESP_LOGI("JSON", "Raw(Not support)");
		}
	}
}
void task_rx(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
		uint8_t buf[256];
	while(1) {
		lora_receive();
		if (lora_received()) {
			int receive_len = lora_receive_packet(buf, sizeof(buf));
			ESP_LOGI(pcTaskGetName(NULL), "%d byte packet received:[%.*s]", receive_len, receive_len, buf);
			ESP_LOGI("RX", "Deserialize.....");
			cJSON *root2 = cJSON_Parse((char*)buf);
			JSON_Analyze(root2);
			cJSON_Delete(root2);
		} 
		vTaskDelay(1);
	}
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
	     .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}
void app_main()
{	
	if (lora_init() == 0) {
		ESP_LOGE(pcTaskGetName(NULL), "Does not recognize the module");
		while(1) {
			vTaskDelay(1);
		}
	}

#if CONFIG_169MHZ
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 169MHz");
	lora_set_frequency(169e6); // 169MHz
#elif CONFIG_433MHZ
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 433MHz");
	lora_set_frequency(433e6); // 433MHz
#elif CONFIG_470MHZ
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 470MHz");
	lora_set_frequency(470e6); // 470MHz
#elif CONFIG_866MHZ
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 866MHz");
	lora_set_frequency(866e6); // 866MHz
#elif CONFIG_915MHZ
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 915MHz");
	lora_set_frequency(915e6); // 915MHz
#elif CONFIG_OTHER
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is %dMHz", CONFIG_OTHER_FREQUENCY);
	long frequency = CONFIG_OTHER_FREQUENCY * 1000000;
	lora_set_frequency(frequency);
#endif

	lora_enable_crc();
	int cr = 1;
	int bw = 9;
	int sf = 12;
#if CONFIF_ADVANCED
	cr = CONFIG_CODING_RATE
	bw = CONFIG_BANDWIDTH;
	sf = CONFIG_SF_RATE;
#endif
	lora_set_coding_rate(cr);
	ESP_LOGI(pcTaskGetName(NULL), "coding_rate=%d", cr);
	lora_set_bandwidth(bw);
	ESP_LOGI(pcTaskGetName(NULL), "bandwidth=%d", bw);
	lora_set_spreading_factor(sf);
	ESP_LOGI(pcTaskGetName(NULL), "spreading_factor=%d", sf);
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

	xTaskCreate(&task_rx, "task_rx", 4096, NULL, 1, NULL);
	if ( http_post_init() != pdPASS ){
        ESP_LOGE(TAG,"Error while initing http_post");
    }
}
