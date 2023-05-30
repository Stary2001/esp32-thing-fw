#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include "easy_config.h"

#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "easy_config";

static size_t num_config_infos = 0;
static easy_config_entry_info *config_infos = NULL;
static easy_config_entry *config_entries = NULL;

void easy_config_setup(easy_config_entry_info *info) {
    config_infos = info;

    size_t length = 0;
    easy_config_entry_info *inf = info;
    while(inf->type != CONFIG_TYPE_END) {
        length++;
        inf++;
    }
    num_config_infos = length;

    config_entries = malloc(sizeof(easy_config_entry) * num_config_infos);
    memset(config_entries, 0, sizeof(easy_config_entry) * num_config_infos);
}

struct buffer {
    char *data;
    size_t length;
    size_t current_offset;
};

void buffer_snprintf(struct buffer *self, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);

    if(self->data == NULL) {
        self->current_offset += vsnprintf(NULL, 0, fmt, args);
    } else {
        ssize_t remaining = self->length - self->current_offset;
        self->current_offset += vsnprintf(self->data + self->current_offset, remaining, fmt, args);
    }

    va_end(args);
}

extern const char html_header_start[] asm("_binary_index_header_html_start");
extern const char html_header_end[]   asm("_binary_index_header_html_end");
extern const size_t html_header_length;

void build_html(char *output, size_t *length) {
    /* damn */
    struct buffer buff = {
        .data = output,
        .length = *length,
        .current_offset = 0
    };

    buffer_snprintf(&buff, "%s", html_header_start);

    for(int i = 0; i < num_config_infos; i++) {
        buffer_snprintf(&buff, "<label>%s ", config_infos[i].name);
        switch(config_infos[i].type) {
            case CONFIG_TYPE_BOOL:
                buffer_snprintf(&buff, "<input name=\"%s\" type=\"checkbox\" %s></br>", config_infos[i].id, config_entries[i].bool_value ? "checked" : "");
            break;

            case CONFIG_TYPE_INT:
                buffer_snprintf(&buff, "<input name=\"%s\" type=\"number\" value=\"%i\"><br/>", config_infos[i].id, config_entries[i].int_value);
            break;

            case CONFIG_TYPE_STRING:
                {
                    const char *value = "";
                    if(config_entries[i].string_value != NULL) {
                        value = config_entries[i].string_value;
                    }
                    buffer_snprintf(&buff, "<input name=\"%s\" type=\"text\" value=\"%s\"><br/>", config_infos[i].id, value);
                }
            break;

            default:
            break;
        }
        buffer_snprintf(&buff, "</label>");
    }
    buffer_snprintf(&buff, "</form></body></html>");

    *length = buff.current_offset + 1;
}

void easy_config_setup_wifi_ap() {
    char *final_html = NULL;
    size_t length = 0;

    wifi_init_softap();

    build_html(NULL, &length);
    final_html = malloc(length);
    build_html(final_html, &length);

    start_http_server(final_html);
    start_dns_server();
}

bool easy_config_get_boolean(size_t index) {
    if(index < num_config_infos && config_infos[index].type == CONFIG_TYPE_BOOL) {
        return config_entries[index].bool_value;
    } else {
        abort();
    }
}

int easy_config_get_integer(size_t index) {
    if(index < num_config_infos && config_infos[index].type == CONFIG_TYPE_INT) {
        return config_entries[index].int_value;
    } else {
        abort();
    }
}

const char *easy_config_get_string(size_t index) {
    if(index < num_config_infos && config_infos[index].type == CONFIG_TYPE_STRING) {
        return config_entries[index].string_value;
    } else {
        abort();
    }
}

bool easy_config_load_from_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t nvs;
    ret = nvs_open("easy_config", NVS_READWRITE, &nvs);
    ESP_ERROR_CHECK(ret);

    for(size_t i = 0; i < num_config_infos; i++) {
        switch(config_infos[i].type) {
            case CONFIG_TYPE_BOOL: 
            {
                uint8_t temp = 0;
                ret = nvs_get_u8(nvs, config_infos[i].id, &temp);
                config_entries[i].bool_value = temp != 0;
            }
            break;
            
            case CONFIG_TYPE_INT:
                ret = nvs_get_i32(nvs, config_infos[i].id, &config_entries[i].int_value);
            break;
            
            case CONFIG_TYPE_STRING:
            {
                size_t length = 0;

                ret = nvs_get_str(nvs, config_infos[i].id, NULL, &length);
                if(ret == ESP_OK) {
                    char *str = malloc(length);
                    nvs_get_str(nvs, config_infos[i].id, str, &length);
                    config_entries[i].string_value = str;
                }
            }   
            break;

            default:
                abort();
            break;
        }
        
        if(ret == ESP_ERR_NVS_NOT_FOUND) {
            return false;
        } else {
            ESP_ERROR_CHECK(ret);
        }
    }

    nvs_close(nvs);
    ESP_ERROR_CHECK(ret);
    return true;
}