#ifndef DIRIGERAH
#define DIRIGERAH

#include "esp_http_client.h"
#include <stdbool.h>
#include <stdint.h>
#define DIRIGERATAG "ikea system"

struct Light {
  char *id; 
};


struct Light* get_lights(uint8_t *size);
void free_lights(struct Light* lights, uint8_t size);

void set_light_on(struct Light *light, bool isOn );

esp_http_client_handle_t create_client(const char* url); 

#endif
