#include "fb.h"
extern fb_data_t data_fb;
extern bool flagPut;

char *firebaseAddress = "datn-kmt-default-rtdb.firebaseio.com";
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
        	#if CONFIG_debug
                ESP_LOGD(TAG_1, "HTTP_EVENT_ERROR");
            #endif
            break;
        case HTTP_EVENT_ON_CONNECTED:
        	#if CONFIG_debug
                ESP_LOGD(TAG_1, "HTTP_EVENT_ON_CONNECTED");
            #endif
            break;
        case HTTP_EVENT_HEADER_SENT:
        	#if CONFIG_debug
            	ESP_LOGD(TAG_1, "HTTP_EVENT_HEADER_SENT");
            #endif
            break;
        case HTTP_EVENT_ON_HEADER:
        	#if CONFIG_debug
            	ESP_LOGD(TAG_1, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            #endif
            break;
        case HTTP_EVENT_ON_DATA:
        	#if CONFIG_debug
        	    ESP_LOGD(TAG_1, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        	#endif
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
        	#if CONFIG_debug
                ESP_LOGD(TAG_1, "HTTP_EVENT_ON_FINISH");
            #endif
            break;
        case HTTP_EVENT_DISCONNECTED:
        	#if CONFIG_debug
            	ESP_LOGI(TAG_1, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
        	    ESP_LOGI(TAG_1, "Last esp error code: 0x%x", err);
            	ESP_LOGI(TAG_1, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            #endif
            break;
    }
    return ESP_OK;
}
void firebase_task_realtime(void* param){
    while(1){
        // ======================================
        // Start PUT request
        // ======================================
        char data[128];
        char url[128];
        char nodeAddress[10];
        
        fb_data_t fb=*((fb_data_t*)param);
        sprintf(nodeAddress,"n%d",fb.ID);
        sprintf(data,"{\"CO\":%f,\"UV\":%f,\"H\":%f,\"T\":%f,\"D\":%f,\"D10\":%f}", fb.CO, fb.UV,fb.H,fb.T,fb.D,fb.D10);

        // Create url
        sprintf(url, "https://%s/node/%s/%s.json",firebaseAddress,nodeAddress,nodeAddress);
        
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);

        esp_http_client_set_method(client, HTTP_METHOD_PUT);

        if (esp_http_client_open(client, strlen(data)) == ESP_OK) {
            ESP_LOGI("Firebase", "Connection opened");
            esp_http_client_write(client, data, strlen(data));

            esp_http_client_fetch_headers(client);
            //ESP_LOGI("Firebase", "HTTP POST Status = %d, content_length = %d", esp_http_client_get_status_code(client), esp_http_client_get_content_length(client));
            if (esp_http_client_get_status_code(client)==200){
                ESP_LOGI("Firebase", "Message successfuly sent!");
            }
            else {
                ESP_LOGI("Firebase", "Sending message failed!");
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            
        }
        else {
            ESP_LOGE("Firebase", "Connection failed");
        }
        // ======================================
        // End of PUT request
        // ======================================
        vTaskDelay(5000/portTICK_RATE_MS);
    }
}
void firebase_task_timestamp(void* param){
    while(1){
        // ======================================
        // Start PUT request
        // ======================================
        char data[128];
        char url[128];
        char nodeAddress[10];
        char nodeHour[3];
        char nodeHourMin[3];
        if(flagPut){
        fb_data_t fb=*((fb_data_t*)param);

        sprintf(nodeAddress,"n%d",fb.ID);
        printf("DEBUG: fb.ID = %d\n", fb.ID);
        sprintf(data,"{\"CO\":%f,\"UV\":%f,\"H\":%f,\"T\":%f,\"D\":%f,\"D10\":%f}", fb.CO, fb.UV,fb.H,fb.T,fb.D,fb.D10);
        sprintf(nodeHour,"%d",fb.hour);
        sprintf(nodeHourMin,"%d",fb.min);
        // Create url
        sprintf(url, "https://%s/node/%s/%s-%s/%s.json",firebaseAddress,nodeAddress,nodeAddress,nodeHour,nodeHourMin);
        
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);

        esp_http_client_set_method(client, HTTP_METHOD_PUT);

        if (esp_http_client_open(client, strlen(data)) == ESP_OK) {
            ESP_LOGI("Firebase", "Connection opened");
            esp_http_client_write(client, data, strlen(data));

            esp_http_client_fetch_headers(client);
            ESP_LOGI("Firebase", "HTTP POST Status = %d, content_length = %d", esp_http_client_get_status_code(client), esp_http_client_get_content_length(client));
            if (esp_http_client_get_status_code(client)==200){
                ESP_LOGI("Firebase", "Message successfuly sent!");
            }
            else {
                ESP_LOGI("Firebase", "Sending message failed!");
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
           
        }
        else {
            ESP_LOGE("Firebase", "Connection failed");
        }
       
             data_fb.ID= 0;
             data_fb.CO=0;
             data_fb.UV=0;
             data_fb.H=0;
             data_fb.T=0;
             data_fb.D=0;
             data_fb.D10=0;
             data_fb.hour=0;
             data_fb.min=0;
             flagPut =false;
        }
        vTaskDelay(1000/portTICK_RATE_MS);
    }
        // ======================================
        // End of PUT request
        // ======================================
}