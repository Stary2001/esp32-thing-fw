#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"
#include "driver/uart.h"
#include "soc/adc_channel.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_adc_cal.h"
#include "esp_wifi.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"

#include "sdkconfig.h"

#include "config.h"

#include "DHT22.h"

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC1_CHANNEL_4;
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   4

RTC_DATA_ATTR int sleep_counter = 0;

float get_battery_voltage() {
    uint32_t raw = 0;

    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        raw += adc1_get_raw((adc1_channel_t)channel);
    }

    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw/NO_OF_SAMPLES, adc_chars);
    return (voltage*2)/1000.0;
}

easy_config_entry_info config_entries[] = {
    /* device id */
    [CONFIG_DEVICE_ID] = { "Device ID", "device_id", CONFIG_TYPE_STRING },
    [CONIG_DEVICE_FRIENDLY_NAME] = { "Friendly name", "friendly_name", CONFIG_TYPE_STRING },
    [CONFIG_HAS_BATTERY] = { "Has battery", "has_battery", CONFIG_TYPE_BOOL },
    [CONFIG_SLEEP_INTERVAL] = { "Sleep interval (seconds)", "sleep_interval", CONFIG_TYPE_INT },

    [CONFIG_USE_INFLUX] = { "Use InfluxDB", "use_influx", CONFIG_TYPE_BOOL },
    [CONFIG_INFLUX_USERNAME] = { "Influx username", "influx_username", CONFIG_TYPE_STRING },
    [CONFIG_INFLUX_PASSWORD] = { "Influx password", "influx_password", CONFIG_TYPE_STRING },

    [CONFIG_USE_MQTT] = { "Use MQTT", "use_mqtt", CONFIG_TYPE_BOOL, },
    [CONFIG_MQTT_URI] = { "MQTT URI", "mqtt_uri", CONFIG_TYPE_STRING, },

    [CONFIG_WIFI_SSID_1] = { "WiFi SSID 1", "wifi_ssid_1", CONFIG_TYPE_STRING },
    [CONFIG_WIFI_PSK_1] = { "WiFi PSK 1", "wifi_psk_1", CONFIG_TYPE_STRING },
    [CONFIG_WIFI_SSID_2] = { "WiFi SSID 2", "wifi_ssid_2", CONFIG_TYPE_STRING },
    [CONFIG_WIFI_PSK_2] = { "WiFi PSK 2", "wifi_psk_2", CONFIG_TYPE_STRING },

    [CONFIG_END] = { NULL, NULL, CONFIG_TYPE_END },
};

const int BUTTON_GPIO = 5;
const int LED_GPIO = 10;
const int MQTT_CONFIG_INTERVAL = 5; // totally arbitrary

extern bool wifi_init_sta(void);
extern void post_to_influx(const char *post_data);
extern void send_to_mqtt(float battery_voltage, float temp, float humidity);

char influx_data[128];
void app_main(void)
{
    printf("Slept %i times.\n", sleep_counter);
    sleep_counter++;

    adc1_config_width(width);
    adc1_config_channel_atten(channel, atten);

    // characterize adc
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);

    easy_config_setup(config_entries);

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 1);
    
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_GPIO);

    if(!easy_config_load_from_nvs() || !gpio_get_level(BUTTON_GPIO)) {
        easy_config_setup_wifi_ap();
    }

    float temp, humidity;
    setDHTgpio(GPIO_NUM_9);

    int dht_err = readDHT(&temp, &humidity);
    if(dht_err == 0) {
        if(wifi_init_sta()) {
            float battery_percent = 100.0f;
            if(easy_config_get_boolean(CONFIG_HAS_BATTERY)) {
                float battery_voltage = get_battery_voltage();
                battery_percent = (battery_voltage - 3.4040) / 0.008;
                snprintf(influx_data, 128, "%s battery=%0.2f,temperature=%0.2f,humidity=%0.2f", easy_config_get_string(CONFIG_DEVICE_ID), battery_voltage, temp, humidity);
            } else {
                snprintf(influx_data, 128, "%s temperature=%0.2f,humidity=%0.2f", easy_config_get_string(CONFIG_DEVICE_ID), temp, humidity);
            }

            if(easy_config_get_boolean(CONFIG_USE_INFLUX)) {
                post_to_influx(influx_data);
            }

            if(easy_config_get_boolean(CONFIG_USE_MQTT)) {
                send_to_mqtt(battery_percent, temp, humidity);
            }

            esp_wifi_stop();
        }
    }
    else {
        printf("dht fail %i!\n", dht_err);
    }

    esp_sleep_enable_timer_wakeup(easy_config_get_integer(CONFIG_SLEEP_INTERVAL) * 1e6);
    esp_deep_sleep_start();
}
