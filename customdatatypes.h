#ifndef CUSTOMDATATYPES_H
#define CUSTOMDATATYPES_H
#include <stdint.h>
#include <time.h>
#include <bluetooth/bluetooth.h>

typedef struct {
    char        address[128];
    uint16_t    port;
    uint16_t    sensors;
} mqtt_th_arg_t;


typedef struct {
    char    topic[128];
    char    payload[512];
    time_t  last;
} sensor_mqtt_t;

typedef struct {
    uint16_t id;
    bdaddr_t mac;
    char smac[20];
    char name[32];
    uint16_t polltime;
} sensor_t;

#endif // CUSTOMDATATYPES_H
