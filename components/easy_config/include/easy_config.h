#include <stdint.h>

typedef enum easy_config_entry_type {
	CONFIG_TYPE_STRING,
	CONFIG_TYPE_BOOL,
	CONFIG_TYPE_INT,
	CONFIG_TYPE_END
} easy_config_entry_type;

typedef struct easy_config_entry_info {
	const char *name;
	const char *id;
	easy_config_entry_type type;
} easy_config_entry_info;

typedef struct easy_config_entry {
	// etc
	union {
		const char *string_value;
		int32_t int_value;
		bool bool_value;
	}
} easy_config_entry;

void easy_config_setup(easy_config_entry_info *info);
bool easy_config_load_from_nvs();
void easy_config_setup_wifi_ap();

bool easy_config_get_boolean();
int easy_config_get_integer();
const char *easy_config_get_string();