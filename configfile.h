#ifndef CONFIGFILE_H
#define CONFIGFILE_H
#include "customdatatypes.h"

int read_mqtt_config_file(mqtt_th_arg_t *mq);
int read_sensor_config_file(sensor_t **s);

#endif // CONFIGFILE_H
