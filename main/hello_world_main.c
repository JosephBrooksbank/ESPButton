/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "dirigera.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_tls.h"
#include "cJSON.h"
#include "ENV.c"
#include "cJSON_Utils.h"
///// config

// led stuff!
#define TAG "mine!"
#define LED_PIN 23
#define LED_PWR_PIN 22
#define BLINK_PERIOD 10 // ms
static led_strip_handle_t led_strip;
static uint8_t colors[3] = {120, 0, 0};
static uint8_t currently_incrementing = 0;

// wifi stuff!
#define WIFITAG "wifi"
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_RETRY 5
static int retry_num = 0;

// request stuff!
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 15000
#define HTTPTAG "http_client"


// light stuff!
#define LIGHTTAG "lights!"

///// begin function declarations

esp_err_t __http_event_handler(esp_http_client_event_t *evt) {
  static char *output_buffer; // buffer to store response of http request from
                              // event handler
  static int output_len;      // store number of bytes read
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(HTTPTAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(HTTPTAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(HTTPTAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(HTTPTAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
             evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGD(HTTPTAG, "HTTPEVENT_ON_DATA, len=%d", evt->data_len);
        // clean the buffer in case of a new request
        if (output_len == 0 && evt->user_data) {
                // sets every byte to 0 in the user_data field
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
        }

        /*
         * Check for chunked encoding, in case of binary data in the response.
         */
        if (!esp_http_client_is_chunked_response(evt->client)) {
                int copy_len = 0;
                if (evt->user_data) {
                    // the last byte in env->user_data is kept for the NULL character in case of out-of-bound access
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer ==  NULL) {
                        ESP_LOGI(HTTPTAG, "allocating memory for output buffer...");
                        output_buffer = (char*) calloc(content_len+1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(HTTPTAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }
        break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
  }


static void http_rest_with_url(void) {

    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
    /**
     * NOTE: All the configuration parameters for http_client must be specified either in URL or as host and path parameters.
     * If host and path parameters are not set, query parameter will be ignored. In such cases,
     * query parameter should be specified in URL.
     *
     * if URL as well as host and path are specified, host & path take precedence
     */
    esp_http_client_config_t config = {
        .url = "https://192.168.0.29:8443/v1/devices",
        /* .host = "https:", */
        /* .path = "/devices", */
        /* .port = 8443, */
        .buffer_size_tx = 2142,
        /* .query = "esp", */
        .event_handler = __http_event_handler,
        .user_data = local_response_buffer,
        .disable_auto_redirect = true,

    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer "TOKEN);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        // PRId64 is a macro that expands to the printf formatter for 64 bit ints on this architecture
        ESP_LOGI(HTTPTAG, "HTTP GET Status = %d, content_length = %"PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        cJSON *json = cJSON_Parse(local_response_buffer);
        cJSON *element = NULL;
        
        if (cJSON_IsArray(json)) {
            ESP_LOGI(HTTPTAG, "json is array!");
            cJSON_ArrayForEach(element, json) {
                cJSON *type = cJSON_GetObjectItem(element, "type");
                if (strcmp(type->valuestring, "light") == 0) {
                    cJSON *attributes = cJSON_GetObjectItem(element, "attributes");
                    cJSON* name = cJSON_GetObjectItem(attributes, "customName");
                    ESP_LOGI(LIGHTTAG, "found light %s", name->valuestring );
                } else {
                    ESP_LOGW(LIGHTTAG, "found element of type %s", type->valuestring);
                }
                
            } 

        } else {
            ESP_LOGI(HTTPTAG, "json is not array?");
        }

    } else {
        ESP_LOGE(HTTPTAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    // this is why we add 1 byte to the buffer size. if the whole buffer was filled by the response, strlen would overflow
    /* ESP_LOG_BUFFER_HEX(HTTPTAG, response_buffer, strlen(response_buffer)); */

    esp_http_client_cleanup(client);
}

static void http_task_get_data(void *pvParameters) {
    http_rest_with_url();
    ESP_LOGI(HTTPTAG, "Finished http request");
    vTaskDelete(NULL);
}

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

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  }
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retry_num < MAX_RETRY) {
      esp_wifi_connect();
      retry_num++;
      ESP_LOGI(WIFITAG, "retry connecting wifi");
    } else {
      xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(WIFITAG, "failed to connect to wifi");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(WIFITAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    retry_num = 0;
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void) {
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {.sta = {.ssid = "generic network name",
                                       .password = "MirrorWindowWall",
                                       .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                                       .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
                                       .sae_h2e_identifier = ""}};
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(WIFITAG, "wifi_init_sta finished");

  // wait for either connection or fail event to occur
  EventBits_t bits =
      xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(WIFITAG, "connected to wifi");
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(WIFITAG, "failed to connect to wifi :(");
  } else {
    ESP_LOGE(WIFITAG, "UNEXPECTED EVENT");
  }
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

    xTaskCreate(&http_task_get_data, "http_task_get_data", 20000, NULL, 5, NULL);
    xTaskCreate(&print_light_ids, "lights_get_data", 8192, NULL, 5, NULL);

  while (1) {
    /* ESP_LOGI(TAG, "Turning the LED %d %d %d!", colors[0], colors[1],
     * colors[2]); */
    blink_led();
    vTaskDelay(BLINK_PERIOD / portTICK_PERIOD_MS);
  }
}
