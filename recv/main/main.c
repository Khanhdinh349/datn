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
#include "wifi.h"
#include "esp_sntp.h"

static const char *TAG = "ESP32 GATEWAY";
fb_data_t data_fb;
bool flagPut= false;


void time_task() { 
	ESP_LOGI("TIME","Start TIMER");
    time_t now;
    struct tm timeinfo;
	int hour_i=0;
	int min_i=0;
	while(1){
		time(&now);
		localtime_r(&now, &timeinfo);
		// Is time set? If not, tm_year will be (1970 - 1900).
		if (timeinfo.tm_year < (2016 - 1900)) {
			ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
			ESP_LOGI(TAG, "Initializing SNTP");
			sntp_setoperatingmode(SNTP_OPMODE_POLL);
			sntp_setservername(0, "pool.ntp.org");
			//sntp_set_time_sync_notification_cb(time_sync_notification_cb);
			sntp_init();
			time_t now = 0;
			struct tm timeinfo = { 0 };
			int retry = 0;
			const int retry_count = 10;
			while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
				ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
				vTaskDelay(2000 / portTICK_PERIOD_MS);
			}
			time(&now);
			localtime_r(&now, &timeinfo);
			// update 'now' variable with current time
			time(&now);
		}

		char *strftime_buf_h = malloc(64*sizeof(char));
		char *strftime_buf_m = malloc(64*sizeof(char));

		// Set timezone to VN Standard Time
		setenv("TZ", "ICT-7", 1);
		tzset();
		localtime_r(&now, &timeinfo);
		strftime(strftime_buf_h, sizeof(strftime_buf_h), "%H", &timeinfo);
		strftime(strftime_buf_m, sizeof(strftime_buf_m), "%M", &timeinfo);
		

		ESP_LOGI(TAG, "=====Get Time=====");
		ESP_LOGI(TAG, "Hour is: %s ", strftime_buf_h);
		ESP_LOGI(TAG, "Min is: %s ", strftime_buf_m);
		
		hour_i=atoi(strftime_buf_h);
		min_i=atoi(strftime_buf_m);

		data_fb.hour=hour_i;
		data_fb.min=min_i;			
	
		if (sntp_get_sync_mode() == SNTP_SYNC_MODE_SMOOTH) {
			struct timeval outdelta;
			while (sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS) {
				adjtime(NULL, &outdelta);
				ESP_LOGI(TAG, "Waiting for adjusting time ... outdelta = %li sec: %li ms: %li us",
						(long)outdelta.tv_sec,
						outdelta.tv_usec/1000,
						outdelta.tv_usec%1000);
				vTaskDelay(2000 / portTICK_PERIOD_MS);
			}
		}
		vTaskDelay(10000 / portTICK_PERIOD_MS);
	}
}
void JSON_Analyze(const cJSON *const root)
{
	cJSON *current_element = NULL;
	cJSON_ArrayForEach(current_element, root)
	{
		if (current_element->string)
		{
			const char *string = current_element->string;
			ESP_LOGI("JSON", "[%s]", string);
			if(strcmp(string,"ID")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.ID=atoi(valuestring);
					ESP_LOGI("JSON", "%d", data_fb.ID);
				}
			}
			if(strcmp(string,"CO")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.CO=atof(valuestring);
					ESP_LOGI("JSON", "%f", data_fb.CO);
				}
			}
			if(strcmp(string,"UV")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.UV=atof(valuestring);
					ESP_LOGI("JSON", "%f", data_fb.UV);
				}
			}
			if(strcmp(string,"H")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.H=atof(valuestring);
					ESP_LOGI("JSON", "%f", data_fb.H);
				}
			}
			if(strcmp(string,"T")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.T=atof(valuestring);
					ESP_LOGI("JSON", "%f", data_fb.T);
				}
			}
			if(strcmp(string,"D")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.D=atof(valuestring);
					ESP_LOGI("JSON", "%f", data_fb.D);
				}
			}
			if(strcmp(string,"D10")==0){
				if (cJSON_IsString(current_element))
				{
					char *valuestring = current_element->valuestring;
					data_fb.D10=atof(valuestring);
					ESP_LOGI("JSON", "%f", data_fb.D10);
				}
			}
		}
	}
}
void task_rx(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	uint8_t buf[256];
	while (1)
	{
		lora_receive();
		if (lora_received())
		{
			int receive_len = lora_receive_packet(buf, sizeof(buf));
			ESP_LOGI(pcTaskGetName(NULL), "%d byte packet received:[%.*s]", receive_len, receive_len, buf);
			ESP_LOGI("RX", "Deserialize.....");
			cJSON *root2 = cJSON_Parse((char *)buf);
			JSON_Analyze(root2);
			cJSON_Delete(root2);
			flagPut=true;
		}
		vTaskDelay(1);
	}
}

void app_main()
{
	if (lora_init() == 0){
		ESP_LOGE(pcTaskGetName(NULL), "Does not recognize the module");
		while (1)
		{
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

	
	wifi_connection_begin();
	wifi_connection_start();

	xTaskCreate(&task_rx, "task_rx", 10 * 1024, NULL, 4, NULL);
	xTaskCreate(&time_task, "time_task", 10 * 1024, NULL, 3, NULL);
	//xTaskCreate(&firebase_task_realtime, "firebase_task_realtime", 10 * 1024, (void *)&data_fb, 2, NULL);
	xTaskCreate(&firebase_task_timestamp, "firebase_task_timestamp", 10 * 1024, (void *)&data_fb, 1, NULL);
}
