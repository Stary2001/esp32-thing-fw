#include <stddef.h>
#include <stdbool.h>
#include "easy_config.h"

#include <string.h>
#include "nvs_flash.h"

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
}

void easy_config_setup_wifi_ap() {
    extern void wifi_init_softap(void);
    extern void start_http_server(void);
    extern void start_dns_server(void);
    wifi_init_softap();
    start_http_server();
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