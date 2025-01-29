
#include "dirigera.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ENV.c"
#include "wifi.h"
#include "led.h"
///// config


#define TAG "main"

// gpio stuff
#define BUTTON_PIN 15
#define GPIO_INPUT_PIN_SEL (1ULL << BUTTON_PIN) 
static QueueHandle_t gpio_evt_queue = NULL;
#define ESP_INTR_FLAG_DEFAULT 0

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
static void IRAM_ATTR gpio_isr_handler(void* arg) {
  uint32_t gpio_num = (uint32_t) arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task(void* arg) {
  uint32_t io_num;
  for (;;) {
    if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {

      int level1 = gpio_get_level(io_num);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      if (gpio_get_level(io_num) == level1) {
        printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
      }
    }
  }
}

static void configure_gpio() {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_POSEDGE;
  io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = 1;
  gpio_config(&io_conf);

  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);
  gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
  
  gpio_isr_handler_add(BUTTON_PIN, gpio_isr_handler, (void*) BUTTON_PIN);
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
  configure_gpio();
    wifi_init_sta();
    xTaskCreate(&print_light_ids, "lights_get_data", 8192, NULL, 5, NULL);
    /* xTaskCreate(&turn_light_off, "turn_light_off", 8192, NULL, 5, NULL); */

  while (1) {
    blink_led();
  }
}
