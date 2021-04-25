#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"
#include "soc/adc_channel.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_adc_cal.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "DHT22.h"

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC1_GPIO34_CHANNEL;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   4

float get_battery_voltage() {
    uint32_t raw = 0;

    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        raw += adc1_get_raw((adc1_channel_t)channel);
    }

    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw/NO_OF_SAMPLES, adc_chars);
    return (voltage*2)/1000.0;
}

void wifi_init_sta(void);
void post_to_influx(const char *post_data);
void send_to_mqtt(float battery_voltage, float temp, float humidity);

char influx_data[128];
void app_main(void)
{
    adc1_config_width(width);
    adc1_config_channel_atten(channel, atten);

    // characterize adc
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    float temp, humidity;
    setDHTgpio(GPIO_NUM_25);

    int dht_err = readDHT(&temp, &humidity);
    if(dht_err == 0) {
        float battery_voltage = get_battery_voltage();
        printf("battery_voltage: %0.4f\n", battery_voltage);

        snprintf(influx_data, 128, "conservatory battery=%0.2f,temperature=%0.2f,humidity=%0.2f", battery_voltage, temp, humidity);

        wifi_init_sta();
        post_to_influx(influx_data);

        float battery_percent = (battery_voltage - 3.4040) / 0.008;

        send_to_mqtt(battery_percent, temp, humidity);
        esp_wifi_stop();
    }
    else {
        printf("dht fail!\n");
    }

    esp_sleep_enable_timer_wakeup(300e6);
    rtc_gpio_isolate(GPIO_NUM_12);
    esp_deep_sleep_start();
}
