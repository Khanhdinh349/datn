#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lora.h"
#include "dht11.h"
#include "string.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#define ADC1_CHAN4          ADC1_CHANNEL_6   //gpio num 34
#define ADC_EXAMPLE_ATTEN           ADC_ATTEN_DB_11
#define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_VREF

static esp_adc_cal_characteristics_t adc1_chars;
struct Data 
{
	int TP;
	int HM;
	int UV;
};

struct Data data;

static bool adc_calibration_init(void)
{
    esp_err_t ret;
    bool cali_enable = false;

    ret = esp_adc_cal_check_efuse(ADC_EXAMPLE_CALI_SCHEME);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW("ADC", "Calibration scheme not supported, skip software calibration");
    } else if (ret == ESP_ERR_INVALID_VERSION) {
        ESP_LOGW("ADC", "eFuse not burnt, skip software calibration");
    } else if (ret == ESP_OK) {
        cali_enable = true;
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_EXAMPLE_ATTEN, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
    } else {
        ESP_LOGE("ADC", "Invalid arg");
    }

    return cali_enable;
}
void task_adc(void *pvParameters){
	uint32_t adc_raw[1];
    bool cali_enable = adc_calibration_init();

    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHAN4, ADC_EXAMPLE_ATTEN));
    while (1) {
        adc_raw[0]= adc1_get_raw(ADC1_CHAN4);
        if (cali_enable) {
            data.UV = esp_adc_cal_raw_to_voltage(adc_raw[0], &adc1_chars);
        }
        ESP_LOGI("ADC", "raw  data: %d", adc_raw[0]);
		ESP_LOGI("ADC", "voltage: %d", data.UV);
        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
}

void task_tx(void *data)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	uint8_t buf[256]; // Maximum Payload size of SX1276/77/78/79 is 255
    struct Data *data_parsed;
	data_parsed = data;
	while(1) {
		//"{\"ID\":\"1\",\"TP\":\"30\",\"HM\":\"60\",\"UV\":\"50\"}"
		int send_len = sprintf((char *)buf,"{\"ID\":\"1\",\"TP\":\"%d\",\"HM\":\"%d\",\"UV\":\"%d\"}",data_parsed->TP,data_parsed->HM,data_parsed->UV);
		lora_send_packet(buf, send_len);
		ESP_LOGI(pcTaskGetName(NULL), "TEMP: %d, HUM: %d, UV: %d",data_parsed->TP,data_parsed->HM,data_parsed->UV);
		vTaskDelay(5000/ portTICK_PERIOD_MS);
	} // end while
}
void start_dht11()
{
    DHT11_init(GPIO_NUM_4);
	
    while(1) {
		if (DHT11_read().status  < 0)
		{
			ESP_LOGE(pcTaskGetName(NULL), "FAIL IN READ DATA DHT11");
		}
		else
		{
			data.HM=DHT11_read().humidity;
			data.TP=DHT11_read().temperature;
			ESP_LOGI(pcTaskGetName(NULL), "Temperature is %d \n",data.TP);
			ESP_LOGI(pcTaskGetName(NULL), "Humidity is %d\n", data.HM);
		}
		 vTaskDelay(1000/ portTICK_PERIOD_MS);
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
	//lora_set_coding_rate(CONFIG_CODING_RATE);
	//cr = lora_get_coding_rate();
	ESP_LOGI(pcTaskGetName(NULL), "coding_rate=%d", cr);

	lora_set_bandwidth(bw);
	//lora_set_bandwidth(CONFIG_BANDWIDTH);
	//int bw = lora_get_bandwidth();
	ESP_LOGI(pcTaskGetName(NULL), "bandwidth=%d", bw);

	lora_set_spreading_factor(sf);
	//lora_set_spreading_factor(CONFIG_SF_RATE);
	//int sf = lora_get_spreading_factor();
	ESP_LOGI(pcTaskGetName(NULL), "spreading_factor=%d", sf);

// #if CONFIG_SENDER
	xTaskCreate(&task_adc, "task_adc", 4096, NULL, 1, NULL);
	xTaskCreate(&start_dht11, "dht11", 1024*2, NULL, 5, NULL);
	xTaskCreate(&task_tx, "task_tx", 4096, &data, 1, NULL);
// #endif
// #if CONFIG_RECEIVER
// 	xTaskCreate(&task_rx, "task_rx", 4096, NULL, 1, NULL);
// #endif
}
