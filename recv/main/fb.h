#ifndef HTTP_POST_H_
#define HTTP_POST_H_

#include <stdint.h>

typedef enum {
    BME_NEW_READING = 1,
    BME_READING_FROM_EEPROM = 2
} type_of_reading_t;

typedef struct {
    uint8_t type;
    float bme_temp;
    float bme_humi;
    float bme_co2;
    float bme_evoc;
    float bme_iaq;
    float bme_press;
    uint8_t accuracy;
    uint32_t timestamp;
} message_t;

int http_post_init(void);

void https_post_add_message(message_t message);

void http_post_resume(void);

void http_post_erase(void);

#endif