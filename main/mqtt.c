#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "mqtt_client.h"
#include "cJSON.h"

#include "settings.h"

static const char *TAG = "MQTT";

typedef struct sensor {
	const char *id;
	const char *name;
	const char *state_topic;
	const char *unit;
	const char *class;
	float value;
} sensor;

sensor sensors[] = {
	{ .id = "battery",  .name = "Battery",     .state_topic = "conservatory/battery",     .unit = "%", .value = 0.0f, .class="battery"},
	{ .id = "temperature",     .name = "Temperature", .state_topic = "conservatory/temperature", .unit = "°C", .value = 0.0f, .class="temperature"},
	{ .id = "humidity", .name = "Humidity",    .state_topic = "conservatory/humidity",    .unit = "%", .value = 0.0f, .class="humidity"},
};

int num_pending_topic_ids = 0;
int pending_topic_ids[(sizeof(sensors) / sizeof(sensor)) * 2] = {0};

const int num_sensors = sizeof(sensors) / sizeof(struct sensor);

void sensor_set_value(const char *id, float value) {
	for(int i = 0; i < num_sensors; i++) {
		if(strcmp(id, sensors[i].id) == 0) {
			sensors[i].value = value;
		}
	}
}

const char * get_mac_address_str() {
	static char mac_address_str[13] = {0};
	if(mac_address_str[0] == 0) {
		uint8_t mac_address[6];
		esp_efuse_mac_get_default(mac_address);
		snprintf(mac_address_str, 13, "%02x%02x%02x%02x%02x%02x", mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
	}

	return mac_address_str;
}

char *sensor_build_config(sensor *s, char **config_topic) {
	const char *mac_str = get_mac_address_str();
	char unique_id[64];

	snprintf(unique_id, 64, "%s_%s", mac_str, s->id);
	int config_topic_len = snprintf(NULL, 0, "homeassistant/sensor/%s/config", unique_id);
	*config_topic = malloc(config_topic_len+1);
	snprintf(*config_topic, config_topic_len+1, "homeassistant/sensor/%s/config", unique_id);

	cJSON *json = cJSON_CreateObject();
	cJSON *device_json = cJSON_CreateObject();

	// device->name, device-> identifiers
	cJSON_AddStringToObject(device_json, "name", "Some Device");
	cJSON_AddStringToObject(device_json, "identifiers", mac_str);

	cJSON_AddStringToObject(json, "name", s->name);
	cJSON_AddStringToObject(json, "unique_id", unique_id);
	cJSON_AddStringToObject(json, "state_topic", s->state_topic);
	cJSON_AddStringToObject(json, "unit_of_measurement", s->unit);
	cJSON_AddItemToObject(json, "device", device_json);
	cJSON_AddStringToObject(json, "device_class", s->class);

	char *string = cJSON_Print(json);
	cJSON_Delete(json);
	return string;
}

void mqtt_publish_config(esp_mqtt_client_handle_t client) {
    // TODO: don't push config every time!
    for(int i = 0; i < num_sensors; i++) {
    	sensor *s = &sensors[i];

    	char *config_topic = NULL;
    	char *config = sensor_build_config(s, &config_topic);

    	int topic_id = esp_mqtt_client_publish(client, config_topic, config, 0, 1, 1);
    	pending_topic_ids[num_pending_topic_ids++] = topic_id;
    	free(config);
    	free(config_topic);
    }
}

void mqtt_publish_data(esp_mqtt_client_handle_t client) {
    for(int i = 0; i < num_sensors; i++) {
    	sensor *s = &sensors[i];

    	char buff[32];
    	snprintf(buff, 32, "%0.2f", s->value);
    	int topic_id = esp_mqtt_client_publish(client, s->state_topic, buff, 0, 1, 0);
    	pending_topic_ids[num_pending_topic_ids++] = topic_id;
    }
}

TaskHandle_t waiting_task_handle;
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    //int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        // todo: don't publish config every time!
        mqtt_publish_config(client);
        mqtt_publish_data(client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xTaskNotifyGive(waiting_task_handle);
        break;

    /*case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;*/

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        // Clear the ID out of the array.
        for(int i = 0; i < num_pending_topic_ids; i++) {
        	if(pending_topic_ids[i] == event->msg_id) {
        		pending_topic_ids[i] = 0;
        	}
        }

        bool are_we_done_yet = true;
        for(int i = 0; i < num_pending_topic_ids; i++) {
        	if(pending_topic_ids[i] != 0) {
        		are_we_done_yet = false;
        	}
        }

        if(are_we_done_yet) {
        	esp_mqtt_client_disconnect(client);
        }
        break;
    /*case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;*/
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void send_to_mqtt(float battery, float temperature, float humidity) {
    // XXX todo: pull mqtt url at least from nvs

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URL,
    };

    sensor_set_value("battery", battery);
    sensor_set_value("temperature", temperature);
    sensor_set_value("humidity", humidity);

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    /* wait */
    waiting_task_handle = xTaskGetCurrentTaskHandle();
    ulTaskNotifyTake(pdTRUE, 0xFFFFFFFF);
}
