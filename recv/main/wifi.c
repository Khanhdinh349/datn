#include "wifi.h"

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event){
    switch(event->event_id) {
		
    case SYSTEM_EVENT_STA_START:
         ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    
	case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    
	case SYSTEM_EVENT_STA_DISCONNECTED:
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        // And then, try to reconnect again...
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    
	default:
        break;
    }
   
	return ESP_OK;
}
void wifi_connection_begin(void){
	ESP_ERROR_CHECK(nvs_flash_init());
	
	wifi_event_group = xEventGroupCreate();
		
	tcpip_adapter_init();

	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	wifi_config_t wifi_config = {
        .sta = {
        .ssid = SSID,
        .password = PASS,
        },
    };
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_connection_start(void){
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, 30000/portTICK_RATE_MS);

	// print the local IP address
	tcpip_adapter_ip_info_t ip_info;
	ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
	printf("IP Address:  %s\n", ip4addr_ntoa(&ip_info.ip));
	printf("Subnet mask: %s\n", ip4addr_ntoa(&ip_info.netmask));
	printf("Gateway:     %s\n", ip4addr_ntoa(&ip_info.gw));
}

void wifi_connection_end(void){
    ESP_ERROR_CHECK(esp_wifi_disconnect());
}