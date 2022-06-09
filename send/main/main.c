#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lora.h"

#include "string.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_adc_cal.h"
#include "sht30.h"
#include "mhz19.h"

#define MQ          				ADC1_CHANNEL_3   //
#define UV          				ADC1_CHANNEL_6   //gpio num 34
#define PM25          				ADC1_CHANNEL_7   //gpio num 35
#define ADC_EXAMPLE_ATTEN           ADC_ATTEN_DB_11
#define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_VREF
static sht3x_t dev;
static esp_adc_cal_characteristics_t adc1_chars;
struct Data 
{
	float TP;
	float HM;
	int UV;
	float D;
	int CO2;
	int CO;

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
	float voMeasured = 0;
	float calcVoltage = 0;
	float dustDensity = 0;
	uint32_t adc_raw[3];
    bool cali_enable = adc_calibration_init();

    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(UV, ADC_EXAMPLE_ATTEN));
	ESP_ERROR_CHECK(adc1_config_channel_atten(PM25, ADC_EXAMPLE_ATTEN));

	gpio_set_direction(GPIO_NUM_32, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_NUM_32, 1);
    while (1) {
		gpio_set_level(GPIO_NUM_32, 0);
		vTaskDelay(0.28/ portTICK_PERIOD_MS);
		adc_raw[0]= adc1_get_raw(PM25);
			if (cali_enable) {
			voMeasured = esp_adc_cal_raw_to_voltage(adc_raw[0], &adc1_chars);
		}
		vTaskDelay(0.044/ portTICK_PERIOD_MS);
		gpio_set_level(GPIO_NUM_32, 1);
		vTaskDelay(9.68/ portTICK_PERIOD_MS);
		calcVoltage = voMeasured * (3.0 / 1024);
		dustDensity = 0.17 * calcVoltage - 0.1;
		data.D=dustDensity;
		ESP_LOGI("ADC", "raw  data PM2.5: %d", adc_raw[0]);
		ESP_LOGI("ADC", "voMeasured PM2.5 : %f", voMeasured);
		ESP_LOGI("ADC", "calcVoltage PM2.5: %f",calcVoltage);
		ESP_LOGI("ADC", "dustDensity PM2.5: %f",dustDensity);


        adc_raw[1]= adc1_get_raw(UV);
        if (cali_enable) {
            data.UV = esp_adc_cal_raw_to_voltage(adc_raw[1], &adc1_chars);
        }
        ESP_LOGI("ADC", "raw  data UV: %d", adc_raw[1]);
		ESP_LOGI("ADC", "voltage UV: %d", data.UV);

		adc_raw[2]= adc1_get_raw(MQ);
		data.CO=adc_raw[2];
        // if (cali_enable) {
        //     data.UV = esp_adc_cal_raw_to_voltage(adc_raw[2], &adc1_chars);
        // }
        ESP_LOGI("ADC", "raw  data MQ: %d", adc_raw[2]);
		ESP_LOGI("ADC", "data.CO %d", data.CO);

        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
}

void task_tx(void *data)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	uint8_t buf[256]; // Maximum Payload size of SX1276/77/78/79 is 255
    struct Data *data_parsed;
	data_parsed = data;
	uint8_t ID=1;
	while(1) {
		//"{\"ID\":\"1\",\"TP\":\"30\",\"HM\":\"60\",\"UV\":\"50\"}"
		int send_len = sprintf((char *)buf,"{\"ID\":\"%d\",\"CO2\":\"%d\",\"CO\":\"%d\",\"UV\":\"%d\",\"H\":\"%f\",\"T\":\"%f\",\"D\":\"%f\"}",
										ID,data_parsed->CO2,data_parsed->CO,data_parsed->UV,data_parsed->HM,data_parsed->TP,data_parsed->D);
		lora_send_packet(buf, send_len);
		//ESP_LOGI(pcTaskGetName(NULL), "TEMP: %d, HUM: %d, UV: %d",data_parsed->TP,data_parsed->HM,data_parsed->UV);
		vTaskDelay(5000/ portTICK_PERIOD_MS);
	} // end while
}

void task_sht30(void *arg){
    esp_err_t res;

    // Start periodic measurements with 1 measurement per second.
    ESP_ERROR_CHECK(sht3x_start_measurement(&dev, SHT3X_PERIODIC_1MPS, SHT3X_HIGH));

    // Wait until first measurement is ready (constant time of at least 30 ms
    // or the duration returned from *sht3x_get_measurement_duration*).
    vTaskDelay(sht3x_get_measurement_duration(SHT3X_HIGH));

    TickType_t last_wakeup = xTaskGetTickCount();

    while (1)
    {
        // Get the values and do something with them.
        if ((res = sht3x_get_results(&dev, &data.TP, &data.HM)) == ESP_OK)
			ESP_LOGI("task_sht30","SHT3x Sensor: %.2f °C, %.2f %%\n", data.TP, data.HM);
        else
			ESP_LOGI("task_sht30","Could not get results: %d (%s)", res, esp_err_to_name(res));

        // Wait until 2 seconds (cycle time) are over.
        vTaskDelayUntil(&last_wakeup, pdMS_TO_TICKS(10000));
    }
}
void task_mhz19(void *arg){
    ESP_LOGI("task_mhz19", "Init Uart");
    init_uart();
	set_self_calibration(true);
	while (1)
	{
		int co2 = read_co2();
		data.CO2=co2;
		ESP_LOGI("task_mhz19", "Co2: %i ppm", co2);
		vTaskDelay(100/ portTICK_PERIOD_MS);
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
	// xTaskCreate(&task_adc, "task_adc", 4096, NULL, 1, NULL);
	// xTaskCreate(&task_tx, "task_tx", 4096, &data, 1, NULL);
	// xTaskCreate(&task_sht30, "task_sht30", 4096, NULL, 1, NULL);

	ESP_ERROR_CHECK(i2cdev_init());
	memset(&dev, 0, sizeof(sht3x_t));

	ESP_ERROR_CHECK(sht3x_init_desc(&dev, 0x44, 0, 4, 5));
	ESP_ERROR_CHECK(sht3x_init(&dev));

	xTaskCreate(&task_sht30, "task_sht30" ,4096, NULL, 6, NULL);
	xTaskCreate(&task_mhz19, "task_mhz19", 4096, NULL, 5, NULL);
	xTaskCreate(&task_adc, "task_adc", 4096, NULL, 4, NULL);

	xTaskCreate(&task_tx, "task_tx", 4096, &data, 1, NULL);
// #endif
// #if CONFIG_RECEIVER
// 	xTaskCreate(&task_rx, "task_rx", 4096, NULL, 1, NULL);
// #endif

}