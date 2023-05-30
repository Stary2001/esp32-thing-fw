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

extern void easy_config_setup(easy_config_entry_info *info);
extern bool easy_config_load_from_nvs();
extern void easy_config_setup_wifi_ap();

extern bool easy_config_get_boolean();
extern int easy_config_get_integer();
extern const char *easy_config_get_string();

// these are internal but. shh
extern void wifi_init_softap(void);
extern void start_http_server(char *html_page);
extern void start_dns_server(void);