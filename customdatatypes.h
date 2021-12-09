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


typedef union{
    uint16_t completo;
    struct{
        uint8_t basso;
        uint8_t alto;
    }__attribute__ ((packed));
    struct{
        uint16_t bit0		:1;
        uint16_t bit1		:1;
        uint16_t bit2		:1;
        uint16_t bit3		:1;
        uint16_t bit4		:1;
        uint16_t bit5		:1;
        uint16_t bit6		:1;
        uint16_t bit7		:1;
        uint16_t bit8		:1;
        uint16_t bit9		:1;
        uint16_t bit10		:1;
        uint16_t bit11		:1;
        uint16_t bit12		:1;
        uint16_t bit13		:1;
        uint16_t bit14		:1;
        uint16_t bit15		:1;
    }__attribute__ ((packed))bit;
}__attribute__ ((packed)) word_misto;



#endif // CUSTOMDATATYPES_H
