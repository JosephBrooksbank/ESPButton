
#include "dirigera.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "ENV.c"
#include "wifi.h"
#include "led.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h" 
///// config


#define TAG "main"

// gpio stuff
#define BUTTON_PIN 7
#define GPIO_INPUT_PIN_SEL (1ULL << BUTTON_PIN) 
static QueueHandle_t gpio_evt_queue = NULL;
#define ESP_INTR_FLAG_DEFAULT 0



///// begin function declarations
static void wake_up();
static void print_light_ids();
static void turn_lights_off();

static void deep_sleep_task(void *args) {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT1: {
      uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_pin_mask != 0) {
        // ffsll finds the least significant 1 (+1), or 0 if no fields are set
        int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
        ESP_LOGI(TAG, "wake up from GPIO %d", pin);
        turn_lights_off();
      } else {
        ESP_LOGI(TAG, "wake up from GPIO");
      }
      break;
    }
    default:
    break;
  }
  
  ESP_LOGI(TAG, "entering deep sleep");
  esp_deep_sleep_start();
}

void deep_sleep_register_ext1(void) {
  const int wakeup_pin = BUTTON_PIN;
  const uint64_t wakeup_pin_mask = (1ULL << wakeup_pin);
  ESP_LOGI(TAG, "enabling wake up on pin %d", BUTTON_PIN);

  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(wakeup_pin_mask, 0));

  // enabling pull up resistor
  ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(wakeup_pin));
  ESP_ERROR_CHECK(rtc_gpio_pullup_en(wakeup_pin));
}


static void print_light_ids() {
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
}

static void turn_lights_off() {
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

static void wake_up() {
  initialize_nvs();
  wifi_init_sta();
}

void app_main(void) {
  ESP_LOGI(TAG, "hello world!");
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wake_up();
  deep_sleep_register_ext1();
  xTaskCreate(&deep_sleep_task, "deep_sleep_task", 4096, NULL, 6, NULL);

  while (1) {
    /* blink_led(); */
  }
}
