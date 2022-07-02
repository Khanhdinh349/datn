#include "fb.h"
#include "cJSON.h"
extern fb_data_t data_fb;
extern fb_data_cotrol data_control;
extern bool flagControl;

fb_get_data data_get;
fb_get_data data_node1;
fb_get_data data_node2;
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
    char data[128];
    char url[128];
    char nodeAddress[10];
    fb_data_t fb=*((fb_data_t*)param);
    if(fb.ID!=0){
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
                ESP_LOGI("firebase_task_realtime", "Message successfuly sent!");
            }
            else {
                ESP_LOGI("firebase_task_realtime", "Sending message failed!");
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            
        }
        else {
            ESP_LOGE("Firebase", "Connection failed");
        }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
}
void firebase_task_timestamp(void* param){
    char data[128];
    char url[128];
    char nodeAddress[10];
    char nodeHour[3];
    char nodeHourMin[3];
    fb_data_t fb=*((fb_data_t*)param);
    if(fb.ID!=0){
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
        
        }else {
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
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
}

void firebase_task_get(){
    while (1){
        char url[128];
        //ESP_LOGI("firebase_task_get", "get data from firebase");
        // Create url
        sprintf(url, "https://%s/control/.json",firebaseAddress);
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (esp_http_client_open(client, 0) == ESP_OK) {
            esp_http_client_fetch_headers(client);
            if (esp_http_client_get_status_code(client)==200){
                ESP_LOGI("firebase_task_get", "Message successfuly sent!");
            }
            else {
                 ESP_LOGI("firebase_task_get", "Sending message failed!");
            }

            char valor[esp_http_client_get_content_length(client)];
            // Read the stream of data
            esp_http_client_read(client, valor, esp_http_client_get_content_length(client));
        
            ESP_LOGI("firebase_task_get", "%s",valor);
            cJSON *root = cJSON_Parse(valor);

            cJSON *device;
            device = cJSON_GetObjectItem(root,"device");
            
            cJSON *config;
            config = cJSON_GetObjectItem(root,"config");

            char *mode=cJSON_GetObjectItem(config,"mode")->valuestring;

            uint8_t fan=cJSON_GetObjectItem(device,"fan")->valueint;
            uint8_t window=cJSON_GetObjectItem(device,"window")->valueint;

            cJSON *element;
            element = cJSON_GetObjectItem(root,"element");

            float CO=cJSON_GetObjectItem(element,"CO")->valuedouble;
            float D=cJSON_GetObjectItem(element,"D")->valuedouble;
            float D10=cJSON_GetObjectItem(element,"D10")->valuedouble;

            data_get.fan = fan;
            data_get.window = window;
            data_get.CO = CO;
            data_get.D = D;
            data_get.D10 = D10;
            strcpy(data_get.mode,mode);

            cJSON_Delete(root);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
        else {
            ESP_LOGE("Firebase", "Connection failed");
        }
        vTaskDelay(500/ portTICK_PERIOD_MS);
    }
}
void firebase_task_control(void* param){
    //while (1){
        char data[128];
        char url[128];
        fb_data_cotrol fb_push=*((fb_data_cotrol*)param);
        //if(flagControl){
            sprintf(data,"{\"fan\":%d,\"window\":%d}",fb_push.fan,fb_push.window);
            // Create url
            sprintf(url, "https://%s/control/device/.json",firebaseAddress);
            esp_http_client_config_t config = {
                .url = url,
                .event_handler = _http_event_handler,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_method(client, HTTP_METHOD_PUT);
            if (esp_http_client_open(client, strlen(data)) == ESP_OK) {
                ESP_LOGI("firebase_task_control", "Connection opened");
                esp_http_client_write(client, data, strlen(data));
                esp_http_client_fetch_headers(client);
                if (esp_http_client_get_status_code(client)==200){
                    ESP_LOGI("firebase_task_control", "Message successfuly sent!");
                }
                else {
                    ESP_LOGI("firebase_task_control", "Sending message failed!");
                }
                esp_http_client_close(client);
                esp_http_client_cleanup(client);       
            }
            else {
                ESP_LOGE("firebase_task_control", "Connection failed");
            } 
        //}
        //flagControl=false;
        //vTaskDelete(NULL);
        vTaskDelay(100/portTICK_PERIOD_MS);  
    //}    
}
void firebase_task_mode(void* param){
    //while (1){
        char data[128];
        char url[128];
        char mode[10];
        fb_data_mode fb_mode=*((fb_data_mode*)param);
        strcpy(mode,fb_mode.mode);

        sprintf(data,"{\"mode\":\"%s\"}",mode);
        // Create url
        sprintf(url, "https://%s/control/config.json",firebaseAddress);
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_method(client, HTTP_METHOD_PUT);
        if (esp_http_client_open(client, strlen(data)) == ESP_OK) {
            ESP_LOGI("firebase_task_mode", "Connection opened");
            esp_http_client_write(client, data, strlen(data));
            esp_http_client_fetch_headers(client);
            if (esp_http_client_get_status_code(client)==200){
                ESP_LOGI("firebase_task_mode", "Message successfuly sent!");
            }
            else {
                ESP_LOGI("firebase_task_mode", "Sending message failed!");
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);       
        }
        else {
            ESP_LOGE("firebase_task_control", "Connection failed");
            } 
        vTaskDelay(500/portTICK_PERIOD_MS); 
    //}    
}
void firebase_task_get_node1(){
    char url[128];
    //ESP_LOGI("firebase_task_get_node1", "get data from node 1");
    // Create url
    sprintf(url, "https://%s/node/n1/n1.json",firebaseAddress);
    while (1){
        esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (esp_http_client_open(client, 0) == ESP_OK) {
            esp_http_client_fetch_headers(client);
            if (esp_http_client_get_status_code(client)==200){
                ESP_LOGI("firebase_task_get_node1", "Message successfuly sent!");
            }else {
                ESP_LOGI("firebase_task_get_node1", "Sending message failed!");
            }
            char valor[esp_http_client_get_content_length(client)];
            // Read the stream of data
            esp_http_client_read(client, valor, esp_http_client_get_content_length(client));
        
            cJSON *root = cJSON_Parse(valor);
            ESP_LOGI("firebase_task_get_node1", "%s",valor);

            float T=cJSON_GetObjectItem(root,"T")->valuedouble;
            float H=cJSON_GetObjectItem(root,"H")->valuedouble;
            float CO=cJSON_GetObjectItem(root,"CO")->valuedouble;
            float D=cJSON_GetObjectItem(root,"D")->valuedouble;
            float D10=cJSON_GetObjectItem(root,"D10")->valuedouble;
        
            data_node1.CO = CO;
            data_node1.T = T;
            data_node1.H = H;
            data_node1.D = D;
            data_node1.D10 = D10;

            cJSON_Delete(root);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }else {
            ESP_LOGE("Firebase", "Connection failed");
        }
		vTaskDelay(500/ portTICK_PERIOD_MS);
    }  
}
void firebase_task_get_node2(){
    char url[128];
    //ESP_LOGI("firebase_task_get", "get data from node 2");
    // Create url
    while (1){
        sprintf(url, "https://%s/node/n2/n2.json",firebaseAddress);
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = _http_event_handler,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (esp_http_client_open(client, 0) == ESP_OK) {
            esp_http_client_fetch_headers(client);
            if (esp_http_client_get_status_code(client)==200){
                ESP_LOGI("firebase_task_get_node2", "Message successfuly sent!");
            }else {
                ESP_LOGI("firebase_task_get_node2", "Sending message failed!");
            }
            char valor[esp_http_client_get_content_length(client)];
            // Read the stream of data
            esp_http_client_read(client, valor, esp_http_client_get_content_length(client));
        
            cJSON *root = cJSON_Parse(valor);
            ESP_LOGI("firebase_task_get_node2", "%s",valor);

            float T=cJSON_GetObjectItem(root,"T")->valuedouble;
            float H=cJSON_GetObjectItem(root,"H")->valuedouble;
            float CO=cJSON_GetObjectItem(root,"CO")->valuedouble;
            float D=cJSON_GetObjectItem(root,"D")->valuedouble;
            float D10=cJSON_GetObjectItem(root,"D10")->valuedouble;

            data_node2.CO = CO;
            data_node2.T = T;
            data_node2.H = H;
            data_node2.D = D;
            data_node2.D10 = D10;

            cJSON_Delete(root);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }else {
            ESP_LOGE("Firebase", "Connection failed");
        }
	    vTaskDelay(500 / portTICK_PERIOD_MS);
    }  
}