
#include "dirigera.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ENV.c"
#include "wifi.h"
///// config

// led stuff!
#define TAG "mine!"
#define LED_PIN 23
#define LED_PWR_PIN 22
#define BLINK_PERIOD 10 // ms
static led_strip_handle_t led_strip;
static uint8_t colors[3] = {120, 0, 0};
static uint8_t currently_incrementing = 0;



// light stuff!
#define LIGHTTAG "lights!"

///// begin function declarations

static void print_light_ids(void *pvParameters) {
    uint8_t size = 0;
    struct Light* lights = get_lights(&size);
    if (size == 0 || !lights ) {
        ESP_LOGE(TAG, "failed to find any lights!");
    }
    else {
        for (uint8_t i = 0; i < size; i++) {
            ESP_LOGI(TAG, "Light id: %s", lights[i].id);
        } 
    }
    free_lights(lights, size);
    vTaskDelete(NULL);
}

static void turn_light_off(void *pvParameters) {
    uint8_t size = 0;
    struct Light* lights = get_lights(&size);
    if (size == 0 || !lights ) {
        ESP_LOGE(TAG, "failed to find any lights!");
    }
    else {
        for (uint8_t i = 0; i < size; i++) {
            set_light_on(&lights[i], false);
        } 
    }

    free_lights(lights, size);
    vTaskDelete(NULL);
}


static void cycle_color() {

  // at max value for current color
  if (colors[currently_incrementing] == 255) {
    colors[currently_incrementing]++; // reset current color back to 0
    currently_incrementing++;
    if (currently_incrementing >= 3) {
      currently_incrementing = 0;
    }
  }
  colors[currently_incrementing]++; // increase current color by 1
}

static void blink_led(void) {

  cycle_color();
  led_strip_set_pixel(led_strip, 0, colors[0], colors[1], colors[2]);
  led_strip_refresh(led_strip);
}

static void configure_led(void) {
  // enable rgb led on board
  gpio_reset_pin(LED_PWR_PIN);
  gpio_set_direction(LED_PWR_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_PWR_PIN, 1);
  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_PIN,
      .max_leds = 1,
  };

  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000, // 10 MHz
      .flags.with_dma = false,
  };

  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);
}

static void initialize_nvs() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

void app_main(void) {
  ESP_LOGI(TAG, "hello world!");
  configure_led();
  initialize_nvs();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_sta();
    xTaskCreate(&print_light_ids, "lights_get_data", 8192, NULL, 5, NULL);
    xTaskCreate(&turn_light_off, "turn_light_off", 8192, NULL, 5, NULL);

  while (1) {
    blink_led();
    vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
  }
}
