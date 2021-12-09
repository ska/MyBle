#include <stdlib.h>
#include <libconfig.h>

#include "utils.h"
#include "configfile.h"
#include "customdatatypes.h"


int read_mqtt_config_file(mqtt_th_arg_t *mq)
{
    config_t cfg;
    config_setting_t *setting;
    const char *str;
    int tmp_int;

    config_init(&cfg);
    if(! config_read_file(&cfg, CONFIG_FILE))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }

    if(config_lookup_string(&cfg, "broker_addr", &str))
        debug("Store broker_addr: %s\n\n", str);
    else
        error("No 'broker_addr' setting in configuration file.\n");


    if(config_lookup_int(&cfg, "broker_port", &tmp_int))
        debug("Store broker_port: %d\n\n", tmp_int);
    else
        error("No 'broker_port' setting in configuration file.\n");
    strcpy((*mq).address, str);
    (*mq).port = tmp_int;

    config_destroy(&cfg);
    return 0;
}


int read_sensor_config_file(sensor_t **s)
{
    config_t cfg;
    config_setting_t *setting;
    const char *str;
    int count;
    const char *tmp_mac, *tmp_name;
    int tmp_poll;

    config_init(&cfg);
    if(! config_read_file(&cfg, CONFIG_FILE))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }
/*
    if(config_lookup_string(&cfg, "name", &str))
        fprintf(stdout,"Store name: %s\n\n", str);
    else
        fprintf(stderr, "No 'name' setting in configuration file.\n");
*/

    setting = config_lookup(&cfg, "sensors");
    if(setting != NULL)
    {
        count = config_setting_length(setting);
        int i;
        //s = (sensor_t *) malloc(count * sizeof(sensor_t) );
        *s = malloc(count * sizeof(sensor_t));

        for(i=0; i < count; i++)
            (*s)[i].polltime=i;

        fprintf(stdout,"%-20s  %-20s   %s\n", "MAC", "NAME", "POLLTIME");
        for(i = 0; i < count; ++i)
        {
            config_setting_t *sensor = config_setting_get_elem(setting, i);
            const char *mac, *name;
            int polltime;

            memset( &(*s)[i], 0, sizeof(sensor_t));
            if(!(config_setting_lookup_string(sensor, "mac",      &tmp_mac)
                 && config_setting_lookup_string(sensor, "name",  &tmp_name)
                 && config_setting_lookup_int(sensor, "polltime", &tmp_poll)))
                continue;

            (*s)[i].id = i;
            (*s)[i].polltime=tmp_poll;
            memcpy((*s)[i].name, tmp_name, strlen(tmp_name));
            memcpy((*s)[i].smac, tmp_mac,  strlen(tmp_mac));
            str2ba( tmp_mac, &(*s)[i].mac);
        }

        for(i = 0; i < count; ++i)
            printf("%-20s  %-20s  %3d\n", (*s)[i].smac, (*s)[i].name, (*s)[i].polltime);

        putchar('\n');
    }
    config_destroy(&cfg);

    return count;
}
