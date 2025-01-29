#include "led.h"
#include "freertos/idf_additions.h"
#include "led_strip.h"
#include  "driver/gpio.h"
#include "portmacro.h"

// led stuff!
#define TAG "mine!"
#define LED_PIN 23
#define LED_PWR_PIN 22
#define BLINK_PERIOD 10 // ms
static led_strip_handle_t led_strip;
static uint8_t colors[3] = {120, 0, 0};
static uint8_t currently_incrementing = 0;

void cycle_color() {

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

void blink_led(void) {

  cycle_color();
  led_strip_set_pixel(led_strip, 0, colors[0], colors[1], colors[2]);
  led_strip_refresh(led_strip);
  vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
}

void configure_led(void) {
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
