#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <time.h>
#include <sys/time.h>

#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "fb.h"
#include "fb_cf.h"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER FIREBASE_WEB_SERVER // should be parsed by the app
#define WEB_HOST FIREBASE_WEB_HOST
#define WEB_DEVICE_NAME_TEMPLATE "ESP32-%02X%02X%02X%02X%02X%02X"
#define WEB_DEVICE_NAME_SIZE (sizeof("ESP32-000000000000") + 1)

#define PUBLISH_SIZE (sizeof("{\"timestamp\":4098895716000,\"t\":100.99,\"h\":100.00,\"p\":10000.00,\"iaq\":500.00,\"evoc\":1000.00,\"eco2\":10000.00,\"a\":03}") + 1)
#define PUBLISH_TEMPLATE "{\"timestamp\":%lu,\"t\":%.2f,\"h\":%.2f,\"p\":%.2f,\"iaq\":%.2f,\"evoc\":%.2f,\"eco2\":%.2f,\"a\":%d}"
#define BOOTUP_TEMPLATE "{\"timestamp\":{\".sv\": \"timestamp\"},\"ip\":\"%d.%d.%d.%d\",\"ssid\":\"%s\",\"rssi\":%ld}"
#define BOOTUP_TEMPLATE_SIZE (sizeof("{\"timestamp\":{\".sv\": \"timestamp\"},\"ip\":\"255.255.255.255\",\"ssid\":\"HUAWEI-E5776-D797\",\"rssi\":-255}") + 1)

#define STORAGE_NAMESPACE "http-storage"

static const char *TAG = "http-post";

char WEB_DEVICE_NAME[WEB_DEVICE_NAME_SIZE] = {'\0'};

#define MESSAGE_QUEUE_LENGTH (288*2) // two days

static QueueHandle_t msg_queue = NULL;
TaskHandle_t http_post_task_handler = NULL;
static time_t calibration_epoch = 0; 
static uint32_t calibration_esp_timer = 0;

/* Root cert for howsmyssl.com, taken from server_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect iaq-sensor-default-rtdb.europe-west1.firebasedatabase.app:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");
esp_tls_client_session_t *tls_client_session = NULL;

static time_t calibrate_timestamp(uint32_t value);

void http_post_erase(void) {
    nvs_handle my_handle;
    esp_err_t err;

    ESP_LOGI(TAG,"Going to erase state.");

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) goto exit_and_cleanup;

    err = nvs_erase_all(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while erasing!");
    }
    else {
        ESP_LOGI(TAG,"State erased!");
    }

exit_and_cleanup:

    // Close
    nvs_close(my_handle);

    return;
}

void http_post_save_messages(void) {
    nvs_handle my_handle;
    esp_err_t err;

    uint8_t pendent_messages = (uint8_t)uxQueueMessagesWaiting(msg_queue);

    message_t recv_msg;

    uint8_t *buffer = NULL;

    buffer = pvPortMalloc(sizeof(message_t) * pendent_messages);

    if ( buffer == NULL ) return; // no space left 
    // discard everything, and start over ...

    for (uint8_t i = 0; i < pendent_messages; i++ ) {
        if (xQueueReceive(msg_queue, (void *)&recv_msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            // update only for new readings
            if (recv_msg.type == BME_NEW_READING) {
                // update type
                recv_msg.type = BME_READING_FROM_EEPROM;
                // update timestamp
                recv_msg.timestamp = calibrate_timestamp(recv_msg.timestamp);
            }
            // put inside the buffer
            memcpy(&buffer[i*sizeof(message_t)],&recv_msg,sizeof(recv_msg));
        } else {
            // stop here and update
            pendent_messages = i;
            break;
        }
    }

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) goto cleanup_and_exit;

    // save
    err = nvs_set_blob(my_handle, "backup", buffer, pendent_messages*sizeof(message_t) );
    if (err != ESP_OK) goto cleanup_and_exit;

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) goto cleanup_and_exit;

cleanup_and_exit:
    // close
    nvs_close(my_handle);

    if (buffer != NULL) vPortFree(buffer);

    return;
}

void http_post_load_messages(void) {
    nvs_handle my_handle;
    esp_err_t err;
    uint8_t *buffer = NULL;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) goto cleanup_and_exit;

    // Read run time blob
    size_t required_size = 0;  // value will default to 0, if not set yet in NVS
    // obtain required memory space to store blob being read from NVS
    err = nvs_get_blob(my_handle, "backup", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto cleanup_and_exit;

    if (required_size == 0) {
        ESP_LOGI(TAG,"No measurements in EEPROM!");
        goto cleanup_and_exit;
    }

    buffer = pvPortMalloc(required_size);
    if ( buffer == NULL ) goto cleanup_and_exit; // no space left

    // read from nvs
    err = nvs_get_blob(my_handle, "backup", buffer, &required_size);
    if (err != ESP_OK) goto cleanup_and_exit;

    // add messages to the queue
    message_t msg;
    
    for (size_t i = 0; i < required_size/sizeof(message_t); i++ ) {
        memcpy(&msg,&buffer[i*sizeof(message_t)],sizeof(message_t));
        if (xQueueSend(msg_queue, (void *)&msg, 10) != pdPASS) break; // break if full
    }

    // delete from eeprom
    if (required_size > 0) nvs_erase_all(my_handle);

cleanup_and_exit:
    // close
    nvs_close(my_handle);

    if (buffer != NULL) vPortFree(buffer);

    return;
}

void https_post_add_message(message_t message) {
    xQueueSend(msg_queue, (void *)&message, 10);
}

static int https_post_request(char * payload, int size, esp_tls_cfg_t cfg) {
    char buf[512];
    int ret, len;
    int retval = pdFAIL;

    struct esp_tls *tls = esp_tls_conn_http_new(WEB_SERVER, &cfg);

    if (tls != NULL) {
        ESP_LOGI(TAG, "Connection established...");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        goto exit_with_error;
    }

    /* The TLS session is successfully established, now saving the session ctx for reuse */
    if (tls_client_session == NULL) {
        tls_client_session = esp_tls_get_client_session(tls);
    }

    size_t written_bytes = 0;
    size_t expected_bytes = 0;
    char content_size[sizeof("Content-Length: 0000\r\n")+1];
    char content_host[sizeof("Host: \r\n") + sizeof(WEB_HOST) + 1];
    char content_post_endpoint[sizeof("POST /.json HTTP/1.1\r\n") + sizeof(WEB_DEVICE_NAME) + 1];

    expected_bytes = snprintf(content_post_endpoint,sizeof(content_post_endpoint),"POST /%s.json HTTP/1.1\r\n",WEB_DEVICE_NAME);
    ret = esp_tls_conn_write(tls,content_post_endpoint,expected_bytes);
    if (ret != expected_bytes) goto exit_with_error;
    
    expected_bytes = snprintf(content_host,sizeof(content_host),"Host: %s\r\n",WEB_HOST);
    ret = esp_tls_conn_write(tls,content_host,expected_bytes);
    if (ret != expected_bytes) goto exit_with_error;

    expected_bytes = strlen("Connection: close\r\n");
    ret = esp_tls_conn_write(tls,"Connection: close\r\n",expected_bytes);
    if (ret != expected_bytes) goto exit_with_error;

    expected_bytes = strlen("Content-Type: application/json\r\n");
    ret = esp_tls_conn_write(tls,"Content-Type: application/json\r\n",expected_bytes);
    if (ret != expected_bytes) goto exit_with_error;

    expected_bytes = snprintf(content_size,sizeof(content_size),"Content-Length: %u\r\n",size);
    printf("%s",content_size);
    ret = esp_tls_conn_write(tls,content_size,expected_bytes);
    if (ret != expected_bytes) goto exit_with_error;

    expected_bytes = strlen("\r\n");
    ret = esp_tls_conn_write(tls,"\r\n",expected_bytes);
    if (ret != expected_bytes) goto exit_with_error;

    written_bytes = 0;
    do {
        ret = esp_tls_conn_write(tls,payload,size);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            goto exit_with_error;
        }
    } while (written_bytes < size);

    expected_bytes = strlen("\r\n");
    ret = esp_tls_conn_write(tls,"\r\n",expected_bytes);
    if (ret != expected_bytes) goto exit;

    ESP_LOGI(TAG, "Reading HTTP response...");

    for(;;) {
        len = sizeof(buf) - 1;
        bzero(buf, sizeof(buf));
        ret = esp_tls_conn_read(tls, (char *)buf, len);

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ) {
            continue;
        }

        if (ret < 0) {
            ESP_LOGE(TAG, "esp_tls_conn_read  returned [-0x%02X](%s)", -ret, esp_err_to_name(ret));
            break;
        }

        if (ret == 0) {
            ESP_LOGI(TAG, "connection closed");
            break;
        }

        len = ret;
        ESP_LOGD(TAG, "%d bytes read", len);
        /* Print response directly to stdout as it is read */
        for (int i = 0; i < len; i++) {
            putchar(buf[i]);
        }
        putchar('\n'); // JSON output doesn't have a newline at end

        if (ret > 0) {
            // check return code
            if(strstr(buf,"HTTP/1.1 200 OK") != NULL){
                retval = pdPASS;
            }
            goto exit;
        }
    }

exit_with_error:
    ESP_LOGE(TAG,"Error while POSTING!");

exit:
    esp_tls_conn_delete(tls);

    return retval;
}

static int https_get_request_using_cacert_buf(char * payload, int size)
{
    ESP_LOGI(TAG, "https_request using cacert_buf");
    esp_tls_cfg_t cfg = {
        .cacert_buf = (const unsigned char *) server_root_cert_pem_start,
        .cacert_bytes = server_root_cert_pem_end - server_root_cert_pem_start,
    };
    return https_post_request(payload,size,cfg);
}

static int https_get_request_using_already_saved_session(char * payload, int size)
{
    ESP_LOGI(TAG, "https_request using saved client session");
    esp_tls_cfg_t cfg = {
        .client_session = tls_client_session,
    };
    return https_post_request(payload,size,cfg);
    // free(tls_client_session);
    // tls_client_session = NULL;
}

static int synchronize_timestamps(void) {

    time_t now = 0;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Current: %lu",now);
        return pdFAIL;
    }
    
    ESP_LOGI(TAG, "Time set to: %lu",now);

    // calibrate
    calibration_epoch = now;
    calibration_esp_timer = esp_timer_get_time() / 1000;

    return pdPASS;
}

static time_t calibrate_timestamp(uint32_t value) {
    ESP_LOGI(TAG, "Received timestamp: %u Calibration timestamp: %u",value,calibration_esp_timer);
    uint32_t diff = (float)(calibration_esp_timer - value) / 1000.0f;
    ESP_LOGI(TAG,"The measure was %u sec ago. EPOCH: %lu",diff,calibration_epoch);
    time_t corrected_timestamp = calibration_epoch - diff;
    return corrected_timestamp;
}

static void https_request_task(void *pvparameters)
{
    message_t rcv_msg;
    char payload[PUBLISH_SIZE];
    size_t payload_size;

    for (;;) {

        int first_message = pdTRUE;

        // pending messages ?
        if (uxQueueMessagesWaiting(msg_queue) > 0) {

            // try to connect to wifi
            if (wifi_start_sta() == pdPASS) {

                // synchronize timestamps
                int attempts = 150; // approx. 5 minute 
                do {
                    ESP_LOGI(TAG, "Waiting for adjusting time ...");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                } while ( (synchronize_timestamps() != pdPASS) && (attempts-- > 0) );

                // for each message
                while (xQueueReceive(msg_queue, (void *)&rcv_msg, pdMS_TO_TICKS(10)) == pdTRUE) {
                    // create payload
                    payload_size = snprintf(
                        payload,
                        sizeof(payload),
                        PUBLISH_TEMPLATE,
                        rcv_msg.type == BME_NEW_READING ? calibrate_timestamp(rcv_msg.timestamp) : rcv_msg.timestamp,
                        rcv_msg.bme_temp,
                        rcv_msg.bme_humi,
                        rcv_msg.bme_press,
                        rcv_msg.bme_iaq,
                        rcv_msg.bme_evoc,
                        rcv_msg.bme_co2,
                        rcv_msg.accuracy
                    );

                    // initialize return code
                    int res = pdFAIL;

                    if (first_message == pdTRUE) {
                        int attempts = 10;
                        do {
                            res = https_get_request_using_cacert_buf(payload,payload_size);
                            if (res != pdPASS) vTaskDelay(pdMS_TO_TICKS(100));
                        } while ( (res != pdPASS) && (attempts-- > 0) );
                        
                        // not the first message anymore
                        if ( res == pdPASS) {
                            first_message = pdFALSE;
                        }

                    } else {

                        int attempts = 10;
                        do {
                            res = https_get_request_using_already_saved_session(payload,payload_size);
                            if (res != pdPASS) vTaskDelay(pdMS_TO_TICKS(100));
                        } while ( (res != pdPASS) && (attempts-- > 0) );

                    }
                    
                    // 10 attempts without success ? 
                    //  * server down? 
                    //  * ESP tls issue? 
                    if (res == pdFAIL) {
                        // send back to the queue
                        xQueueSend(msg_queue, (void *)&rcv_msg, 10);
                        // backup messages
                        http_post_save_messages();
                        // reboot ESP
                        esp_restart();
                    }
                }

                // stop wifi again
                wifi_stop_sta();

            }
        }

        // cached tls session ?
        if (tls_client_session != NULL) {
            vPortFree(tls_client_session);
            tls_client_session = NULL;
        }

        // wait until next time
        vTaskSuspend(NULL);

    }

    vTaskDelete(NULL);
}

int http_post_init(void) {

    msg_queue = xQueueCreate(MESSAGE_QUEUE_LENGTH, sizeof(message_t));

    if (msg_queue == NULL) return pdFAIL;

    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(WEB_DEVICE_NAME, sizeof(WEB_DEVICE_NAME), WEB_DEVICE_NAME_TEMPLATE,
             eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);

    // load messages from eeprom if exist
    http_post_load_messages();

    if ( xTaskCreate(&https_request_task, "https_get_task", 8192, NULL, 5, &http_post_task_handler) != pdPASS ) {
        return pdFAIL;
    } 

    return pdPASS;
}

void http_post_resume(void) {
    if ((http_post_task_handler != NULL) && (eTaskGetState(http_post_task_handler) == eSuspended) ) {
        xTaskResumeFromISR(http_post_task_handler);
    }
}
