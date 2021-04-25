#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"
#include "soc/adc_channel.h"
#include "esp_adc_cal.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "DHT22.h"

//#define I2C_BUS       0
//#define I2C_SCL_PIN   GPIO_NUM_26
//#define I2C_SDA_PIN   GPIO_NUM_25

//#define I2C_MASTER_RX_BUF_DISABLE 0
//#define I2C_MASTER_TX_BUF_DISABLE 0
//#define WRITE_BIT I2C_MASTER_WRITE              /*!< I2C master write */
//#define READ_BIT I2C_MASTER_READ                /*!< I2C master read */
//#define ACK_CHECK_EN 0x1                        /*!< I2C master will check ack from slave*/
//#define ACK_CHECK_DIS 0x0                       /*!< I2C master will not check ack from slave */
//#define ACK_VAL 0x0                             /*!< I2C ack value */
//#define NACK_VAL 0x1                            /*!< I2C nack value */

/*static esp_err_t i2c_master_init(void)
{
    printf("i2c init\n");
    int i2c_master_port = 0;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        // .clk_flags = 0,          ///!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here.
    };
    esp_err_t err = i2c_param_config(i2c_master_port, &conf);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

static esp_err_t i2c_master_sensor_test(i2c_port_t i2c_num, uint8_t *data_h, uint8_t *data_l)
{
    int ret;
    uint8_t read_id[2] = {0xef, 0xc8};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, 0x70 << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write(cmd, read_id, 2, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        printf("failed to send %s\n", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(30 / portTICK_RATE_MS);
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, 0x70 << 1 | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data_h, ACK_VAL);
    i2c_master_read_byte(cmd, data_l, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    printf("done %d\n",ret);
    return ret;
}*/

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
