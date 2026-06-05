#ifndef CONF_H_
#define CONF_H_

/**
 * Initialises the libconfig code, called once at the
 * start of nwipe, prior to any attempts to access
 * nwipe's config file /etc/nwipe/nwipe.conf
 * @param none
 * @return int
 *   0  = success
 *   -1 = error
 */
int nwipe_conf_init();

/**
 * Before exiting nwipe, this function should be called
 * to free up libconfig's memory usage
 * @param none
 * @return void
 */
void nwipe_conf_close();

void save_selected_customer( char** );

/**
 * int nwipe_conf_update_setting( char *, char * );
 * Use this function to update a setting in nwipe.conf
 * @param char * this is the group name and setting name separated by a period '.'
 *               i.e "PDF_Certificate.PDF_Enable"
 * @param char * this is the setting, i.e ENABLED
 * @return int 0 = Success
 *             1 = Unable to update memory copy
 *             2 = Unable to write new configuration to /etc/nwipe/nwipe.conf
 */
int nwipe_conf_update_setting( char*, char* );

/**
 * int nwipe_conf_read_setting( char *, char *, const char ** )
 * Use this function to read a setting value in nwipe.conf
 * @param char * this is the group name
 * @param char * this is the setting name
 * @param char ** this is a pointer to the setting value
 * @return int 0 = Success
 *             -1 = Unable to find the specified group name
 *             -2 = Unable to find the specified setting name
 */
int nwipe_conf_read_setting( char*, const char** );

int nwipe_conf_populate( char* path, char* value );

#define FIELD_LENGTH 256
#define NUMBER_OF_FIELDS 6
#define MAX_GROUP_DEPTH 4

#define NWIPE_PERSISTENT_CONFIG_DIR "/mnt/wyos-store/config"
#define NWIPE_FALLBACK_CONFIG_DIR "/etc/wyoswipe"
#define NWIPE_PERSISTENT_REPORT_DIR "/mnt/wyos-store/reports"
#define NWIPE_FALLBACK_REPORT_DIR "/tmp/wyoswipe-reports"
#define PATHNAME_MAX 2048

extern char nwipe_config_directory[PATHNAME_MAX];
extern char nwipe_config_file[PATHNAME_MAX];
extern char nwipe_customers_file[PATHNAME_MAX];
extern char nwipe_customers_file_backup[PATHNAME_MAX];
extern char nwipe_customers_file_backup_tmp[PATHNAME_MAX];

extern int using_persistent_storage;

#endif /* CONF_H_ */
