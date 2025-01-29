#include "dirigera.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "ENV.c"
#include "esp_tls.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdint.h>
#include <stdlib.h>

#define HTTPTAG "http_client"
#define MAX_HTTP_OUTPUT_BUFFER 15000
static char response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
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
            ESP_LOGD(HTTPTAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(HTTPTAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(HTTPTAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(HTTPTAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(HTTPTAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
  }

esp_http_client_handle_t create_client(const char* url) {
  esp_http_client_config_t config = {
    .url = url,
    .buffer_size_tx = 2142,
    .event_handler = _http_event_handler,
    .user_data = response_buffer,
    .disable_auto_redirect = true,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_header(client, "Authorization", "Bearer "TOKEN);
  return client;
};

cJSON* get_json(esp_http_client_handle_t client ) {
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        // PRId64 is a macro that expands to the printf formatter for 64 bit ints on this architecture
        ESP_LOGI(HTTPTAG, "HTTP GET Status = %d, content_length = %"PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        cJSON *json = cJSON_Parse(response_buffer);
        return json;
    } else {
        ESP_LOGE(HTTPTAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    return NULL;
}


#define URL "https://"IP":8443/v1"

struct Light* get_lights(uint8_t *size) {
    *size = 0;
    esp_http_client_handle_t client = create_client(URL"/devices");
    cJSON* data = get_json(client);
    if (data == NULL || !cJSON_IsArray(data)) {
        ESP_LOGE(DIRIGERATAG, "Failed to convert response to json");
        *size = 0;
        cJSON_Delete(data);
        esp_http_client_cleanup(client);
        return NULL;
    } 
    cJSON* element = NULL;
    cJSON_ArrayForEach(element, data) {
        cJSON *type = cJSON_GetObjectItem(element, "type");
        if (strcmp(type->valuestring, "light") == 0) {
            (*size)++;
        }
    }

    if (*size == 0) {
        ESP_LOGW(DIRIGERATAG, "did not find any lights");
        cJSON_Delete(data);
        esp_http_client_cleanup(client);
        return NULL;
    }
    struct Light* returnValue = malloc(*size * sizeof(struct Light));
    if (!returnValue) {
        cJSON_Delete(data);
        ESP_LOGE(DIRIGERATAG, "failed to allocate memory for light array!");
        return NULL;
    }

    uint8_t i = 0;
    cJSON_ArrayForEach(element, data) {
        cJSON* type = cJSON_GetObjectItem(element, "type");
        if (type && type->valuestring && strcmp(type->valuestring, "light") == 0) {
            cJSON* id = cJSON_GetObjectItem(element, "id");
            if (id && id->valuestring) {
                returnValue[i].id = strdup(id->valuestring);
                i++;
            }
        }
    }
    cJSON_Delete(data);
    esp_http_client_cleanup(client);
    return returnValue;
}

void free_lights(struct Light* lights, uint8_t size) {
    if (!lights) return;
    for (uint8_t i = 0; i < size; i++) {
        free(lights[i].id);
    }
    free(lights);
}
