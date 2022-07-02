#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <malloc.h>
#include "cJSON.h"
#include "fb.h"
#include "wifi.h"
#include "esp_sntp.h"
#include "driver/mcpwm.h"

#include "lora.h"
#include "lcd.h"


#define I2C_MASTER_SCL_IO 5 /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO 4 /*!< gpio number for I2C master data  */
#define ESP_SLAVE_ADDR 0x27  /*!< ESP32 slave address, you can set any 7bit value */

#define SERVO_MIN_PULSEWIDTH_US (1000) // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US (2000) // Maximum pulse width in microsecond
#define SERVO_MAX_DEGREE        (90)   // Maximum angle in degree upto which servo can rotate

#define SERVO_PULSE_GPIO        (26)   // GPIO connects to the PWM signal line

extern fb_get_data data_get;
extern fb_get_data data_node1;
extern fb_get_data data_node2;
static const char *TAG = "ESP32 GATEWAY";
bool flagControl =false;

fb_data_t data_fb;
fb_data_cotrol data_control;
fb_data_mode data_mode;

void time_task(){ 
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
			firebase_task_realtime(&data_fb);
			firebase_task_timestamp(&data_fb);
		}
		vTaskDelay(1);
	}
}
void task_lcd(){
	/* Display and bus I2C init */
	lcd_init(I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, ESP_SLAVE_ADDR, 16, 2, LCD_5x8DOTS);
	lcd_begin();	
	char screen[16];
	char screen1[16];
	while (1)
	{	
		lcd_clear();
		sprintf(screen,"MODE: %s",data_get.mode);
		lcd_setCursor(0, 0);
		lcd_print(screen);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		//NODE1
		//firebase_task_get_node1();

		lcd_clear();
		sprintf(screen,"NODE 1");
		lcd_setCursor(0, 0);
		lcd_print(screen);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		lcd_clear();
		sprintf(screen,"T: %.1f  H: %.1f",data_node1.T,data_node1.H);
		lcd_setCursor(0, 0);
		lcd_print(screen);
		sprintf(screen1,"CO: %.1f",data_node1.CO);
		lcd_setCursor(0, 1);
		lcd_print(screen1);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		
		lcd_clear();
		sprintf(screen,"PM2.5 : %.1f",data_node1.D);
		lcd_setCursor(0, 0);
		lcd_print(screen);

		sprintf(screen1,"PM10  : %.1f",data_node1.D10);
		lcd_setCursor(0, 1);
		lcd_print(screen1);
		vTaskDelay(2000 / portTICK_PERIOD_MS);

		//NODE2
		//firebase_task_get_node2();
		lcd_clear();
		sprintf(screen,"NODE 2");
		lcd_setCursor(0, 0);
		lcd_print(screen);
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		lcd_clear();
		sprintf(screen,"T: %.1f  H: %.1f",data_node2.T,data_node2.H);
		lcd_setCursor(0, 0);
		lcd_print(screen);
		sprintf(screen1,"CO: %.1f",data_node2.CO);
		lcd_setCursor(0, 1);
		lcd_print(screen1);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
		
		lcd_clear();
		sprintf(screen,"PM2.5 : %.1f",data_node2.D);
		lcd_setCursor(0, 0);
		lcd_print(screen);
		sprintf(screen1,"PM10 : %.1f",data_node2.D10);
		lcd_setCursor(0, 1);
		lcd_print(screen1);
		vTaskDelay(2000 / portTICK_PERIOD_MS);		
	}
}
void btn_init(void){
    gpio_config_t btn ;
    memset(&btn,0,sizeof(gpio_config_t));

	btn.mode=GPIO_MODE_INPUT;
    btn.pin_bit_mask= (1ULL << GPIO_NUM_16);
    btn.pull_up_en=GPIO_PULLUP_ENABLE;
    btn.intr_type=GPIO_INTR_ANYEDGE;
    gpio_config(&btn);

    btn.mode=GPIO_MODE_INPUT;
    btn.pin_bit_mask= (1ULL << GPIO_NUM_22);
    btn.pull_up_en=GPIO_PULLUP_ENABLE;
    btn.intr_type=GPIO_INTR_ANYEDGE;
    gpio_config(&btn);

	btn.mode=GPIO_MODE_INPUT;
    btn.pin_bit_mask= (1ULL << GPIO_NUM_21);
    btn.pull_up_en=GPIO_PULLUP_ENABLE;
    btn.intr_type=GPIO_INTR_ANYEDGE;
    gpio_config(&btn);
}
void pwm_init(void){
	mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, SERVO_PULSE_GPIO); // To drive a RC servo, one MCPWM generator is enough
    mcpwm_config_t pwm_config = {
        .frequency = 50, // frequency = 50Hz, i.e. for every servo motor time period should be 20ms
        .cmpr_a = 0,     // duty cycle of PWMxA = 0
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,
    };
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
}
void task_servo(int angle){
	pwm_init();
	int result;
	result=(angle + SERVO_MAX_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (2 * SERVO_MAX_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
	ESP_ERROR_CHECK(mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, result));
	vTaskDelay(pdMS_TO_TICKS(100));
}
void task_control(){
	btn_init();
	int fan=data_get.fan;
	int window=data_get.window;
	while (1){
		int level16 = gpio_get_level(GPIO_NUM_16);
		int level22 = gpio_get_level(GPIO_NUM_22);	
		int level21 = gpio_get_level(GPIO_NUM_21);

		gpio_pad_select_gpio(GPIO_NUM_14);  
		gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
		if(!level16 || !level21 || !level22){
			if(!level22){
				ESP_LOGI("task_control_get","Change mode");
				data_control.fan = 0;
				data_control.window = 0;
				if(strcmp(data_get.mode,"auto")==0){
					strcpy(data_mode.mode,"manual");
					firebase_task_mode(&data_mode);
					firebase_task_control(&data_control);
				}else{
					strcpy(data_mode.mode,"auto");	
					firebase_task_mode(&data_mode);
					firebase_task_control(&data_control);
				}	
				vTaskDelay(10/ portTICK_PERIOD_MS);
			}	
			if(!level21){
				ESP_LOGI("task_control_get","Change level 21");
				window=data_get.window;
				strcpy(data_mode.mode,"manual");
				if(data_get.fan == 0){
					ESP_LOGI("task_btn","btn22 on %d", level22);
					gpio_set_level(GPIO_NUM_14, 1);
					fan=1;	
					data_control.fan = fan;
				}else{
					ESP_LOGI("task_btn","btn22 off %d", level22);
					gpio_set_level(GPIO_NUM_14, 0);
					fan=0;
					data_control.fan = fan;				
				}
				firebase_task_control(&data_control);
				vTaskDelay(10/ portTICK_PERIOD_MS);
				firebase_task_mode(&data_mode);
				vTaskDelay(10/ portTICK_PERIOD_MS);
			}
			if(!level16){
				ESP_LOGI("task_control_get","Change level 22");
				fan=data_get.fan;
				strcpy(data_mode.mode,"manual");
				if(data_get.window == 0){
					ESP_LOGI("task_btn","btn4 on %d", level21);
					task_servo(90);
					window=1;
					data_control.window = window;
				}else{
					ESP_LOGI("task_btn","btn4 off %d",level21);
					task_servo(-90);
					window=0;	
					data_control.window = window;
				};
				firebase_task_control(&data_control);
				firebase_task_mode(&data_mode);
				vTaskDelay(10/ portTICK_PERIOD_MS);
			}	
		}
		else{
			if(data_get.fan != fan){
				if(data_get.fan == 1){
					fan =1;
				ESP_LOGI("task_control_get","control get fan ");
				gpio_set_level(GPIO_NUM_14, 1);
				}else{
					fan =0;
				ESP_LOGI("task_control_get","control get fan");
				gpio_set_level(GPIO_NUM_14, 0);
				}
			}
			if(data_get.window != window){
				if(data_get.window == 1){
					window =1;
					ESP_LOGI("task_control_get","control get window");
					task_servo(90);
				}else {
					window =0;
					ESP_LOGI("task_control_get","control get window");
					task_servo(-90);
				}
			}	
		}
		if(strcmp(data_get.mode,"auto")==0){
			fan=data_get.fan;
			window=data_get.window;
			if(data_get.CO <= data_node1.CO || data_get.D <= data_node1.D || data_get.D10 <= data_node1.D10){
				if(data_control.fan!=1){
					gpio_set_level(GPIO_NUM_14, 1);
					fan=1;	
					data_control.fan = fan;	
					firebase_task_control(&data_control);
				}
			}else{
				if(data_control.fan!=0){
					gpio_set_level(GPIO_NUM_14, 0);
					fan=0;	
					data_control.fan = fan;	
					firebase_task_control(&data_control);
				}	
			}	
			if(data_get.CO <= data_node2.CO || data_get.D <= data_node2.D || data_get.D10 <= data_node2.D10){
				if(data_control.window!=1){
					window=1;
					task_servo(90);	
					data_control.window = window;
					firebase_task_control(&data_control);
				}
			}else{
				if(data_control.window!=0){
					window=0;
					task_servo(-90);	
					data_control.window = window;
					firebase_task_control(&data_control);
				}
			}		
			vTaskDelay(100/ portTICK_PERIOD_MS);
		}
	vTaskDelay(100/ portTICK_PERIOD_MS);
	}
}
void app_main(){
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
	lora_set_bandwidth(bw);
	lora_set_spreading_factor(sf);

	wifi_connection_begin();
	wifi_connection_start();
	
	xTaskCreate(&task_rx, "task_rx", 4*1024, NULL, 5, NULL);
	xTaskCreate(&task_control, "task_control", 4*1024, NULL, 4, NULL);
	
	xTaskCreate(&task_lcd, "task_lcd", 4*1024, NULL, 2, NULL);
	xTaskCreate(&firebase_task_get, "firebase_task_get", 4*1024, NULL, 1, NULL);
	xTaskCreate(&firebase_task_get_node1, "firebase_task_get_node1", 4*1024, NULL, 0, NULL);
	xTaskCreate(&firebase_task_get_node2, "firebase_task_get_node2", 4*1024, NULL, 0, NULL);
	xTaskCreate(&time_task, "time_task", 10*1024, NULL, 0, NULL);
}
