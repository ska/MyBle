#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <mosquitto.h>
#include <libconfig.h>
#include <time.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include "utils.h"

#define ATT_OP_HANDLE_NOTIFY		0x1B
#if defined(__x86_64__)
#define BT_MAC  "A4:C1:38:D0:FE:97"
#define SRC_MAC "00:00:00:00:00:00"
#else
#define BT_MAC  "A4:C1:38:39:64:C7"
#define SRC_MAC "00:00:00:00:00:00"
#endif

#define MQTT_BROKER_IP      "localhost"
#define MQTT_BROKER_PORT    (1883)

#define TEMP 0x0012
#define HUM  0x0015
#define BAT  0x000E

volatile bool run;
pthread_mutex_t bt_lock;
pthread_mutex_t sensor_data_lock;/**/

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

sensor_mqtt_t *sens_mqtt;
struct mosquitto * mosq = NULL;

void connect_callback(struct mosquitto * mosq, void * obj, int result) {
    printf("connect callback, rc=%d\n", result);
}
void message_callback(struct mosquitto * mosq, void * obj,
                      const struct mosquitto_message * message) {
    printf("message callback\n");
}
void publish_callback(struct mosquitto * mosq, void * userdata, int mid) {

    printf("message published\n");
}

void* mqtt_th(void *arg)
{
    mqtt_th_arg_t *mqtt_args = ((mqtt_th_arg_t *) arg);
    char clientid[24];
    int rc = 0;

    mosquitto_lib_init();
    memset(clientid, 0, 24);
    snprintf(clientid, 23, "%d", getpid());
    mosq = mosquitto_new(clientid, true, 0);
    if (NULL == mosq) {
        printf("mosquitto_new error:  %s\n", strerror(errno));
        pthread_exit(-1);
    }
    mosquitto_connect_callback_set(mosq, connect_callback);
    mosquitto_message_callback_set(mosq, message_callback);
    mosquitto_publish_callback_set(mosq, publish_callback);
    //rc = mosquitto_connect(mosq, MQTT_BROKER_IP, MQTT_BROKER_PORT, 60);
    rc = mosquitto_connect(mosq, mqtt_args->address, mqtt_args->port, 60);
    if (NULL == mosq) {
        printf("mosquitto_connect error:  %s\n", strerror(errno));
        pthread_exit(-2);
    }

    while(run)
    {
        printf("mqtt th\n");
        pthread_mutex_lock(&sensor_data_lock);
        rc = mosquitto_loop(mosq, -1, 1);
        printf("mosquitto_loop %d\n", rc);

        for(int i=0; i<mqtt_args->sensors; i++)
        {
            printf("PRE mosquitto_publish\n");
            if(sens_mqtt[i].topic[0] != 0)
            {
                rc = mosquitto_publish(mosq, NULL, sens_mqtt[i].topic, strlen(sens_mqtt[i].payload), (void * ) sens_mqtt[i].payload, 1, false);
                printf("mosquitto_publish %d\n", rc);
            }
            sleep(1);
        }

        pthread_mutex_unlock(&sensor_data_lock);
        sleep(10);
    }
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    printf("disconnected\n");
}
int read_mqtt_config_file(mqtt_th_arg_t *mq)
{
    config_t cfg;
    config_setting_t *setting;
    const char *str;
    int tmp_int;

    config_init(&cfg);
    if(! config_read_file(&cfg, "example.cfg"))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(EXIT_FAILURE);
    }


    if(config_lookup_string(&cfg, "broker_addr", &str))
        fprintf(stdout,"Store broker_addr: %s\n\n", str);
    else
        fprintf(stderr, "No 'name' setting in configuration file.\n");


    if(config_lookup_int(&cfg, "broker_port", &tmp_int))
        fprintf(stdout,"Store broker_port: %d\n\n", tmp_int);
    else
        fprintf(stderr, "No 'name' setting in configuration file.\n");
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
    if(! config_read_file(&cfg, "example.cfg"))
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

static int bt_write_start_cmd(int sock)
{
    uint8_t buf[10];
    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    return write(sock, buf, 8);/*comando lungo 8byte*/
}

static int l2cap_connect(int sock,
                        const bdaddr_t *dst,
                        uint8_t dst_type,
                        uint16_t psm,
                        uint16_t cid)
{
    int err;
    struct sockaddr_l2 addr;

    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    bacpy(&addr.l2_bdaddr, dst);
    if (cid)
        addr.l2_cid = htobs(cid);
    else
        addr.l2_psm = htobs(psm);

    addr.l2_bdaddr_type = dst_type;
    err = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
    if (err < 0 && !(errno == EAGAIN || errno == EINPROGRESS))
        return -errno;

    return 0;
}


int bt_io_connect(int *sock, bdaddr_t dst)
{
    int err, i;
    uint8_t dst_type;
    uint16_t psm = 0;
    uint16_t cid = 4;
    //bdaddr_t dst;
    bdaddr_t src;
    struct sockaddr_l2 addr;

    printf("%s\n", __func__);
    //str2ba(BT_MAC,  &dst);
    str2ba(SRC_MAC, &src);
    dst_type = BDADDR_LE_PUBLIC;
    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    addr.l2_bdaddr = *BDADDR_ANY;

    if (cid)
        addr.l2_cid = htobs(cid);
    else
        addr.l2_psm = htobs(psm);
    addr.l2_bdaddr_type = dst_type;

    *sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    err = bind(*sock, (struct sockaddr*) &addr, sizeof(addr));
    if(err < 0)
    {
        perror("ERROR on binding");
        return -1;
    }

    struct bt_security bt_sec;
    memset(&bt_sec, 0, sizeof(bt_sec));
    bt_sec.level = BT_SECURITY_LOW;
    err = setsockopt(*sock, SOL_BLUETOOTH, BT_SECURITY, &bt_sec, sizeof(bt_sec));
    if(err < 0)
    {
        perror("ERROR on setsockopt");
        return -2;
    }

#if 0
    printf("%s ", __func__);
    for(i=0; i<sizeof(bdaddr_t); i++)
        printf("%02X%c", dst.b[i], (i<(sizeof(bdaddr_t)-1)) ? ':' : ' ');
    printf("\n");
    printf("%s dest_type:   %d\n", __func__, dst_type);
    printf("%s psm:         %d\n", __func__, psm);
    printf("%s cid:         %d\n", __func__, cid);
#endif

    err = l2cap_connect(*sock, &dst, dst_type, psm, cid);
    if (err < 0) {
        fprintf(stderr, "%s: Err: %d\n", __func__, -err);
        return -3;
    }
}

void* sensor_poll_th(void *arg)
{
    sensor_t *local_sens = ((sensor_t *) arg);
    pthread_t id = pthread_self();
    int sock, err;
    uint8_t flag, read_cicle;
    uint8_t buf[20];
    ssize_t len;
    uint16_t val;
    uint8_t err_div;
    float temp, hum;
    uint8_t batt;
    time_t seconds;

    flag = 0x00;
    while(run)
    {
        err_div = 1;
        debug("Thread for sensor %s running\n", local_sens->name);
        pthread_mutex_lock(&bt_lock);
        debug("Try to connect at %s\n", local_sens->name);
        err = bt_io_connect(&sock, local_sens->mac);
        if(err < 0)
        {
            fprintf(stderr, "bt_io_connect ERROR %d\n", err);
            err_div = 0;
            goto onerror;
        }
        err = bt_write_start_cmd(sock);
        if(err <= 0)
        {
            fprintf(stderr, "bt_write_start_cmd ERROR %d\n", err);
            err_div = 0;
            goto onerror;
        }
        read_cicle = 1;
        while(read_cicle)
        {
            memset(buf, 0, sizeof(buf));
            len = read(sock, buf, sizeof(buf));

            if (ATT_OP_HANDLE_NOTIFY == buf[0])
            {
                memcpy(&val, &buf[1], sizeof(val));/*u8 to u16*/
                switch(val)
                {
                    case TEMP:
                        memcpy(&val, &buf[3], sizeof(val));
                        temp = (float)val/10.0;
                        debug("TEMP: %d | %02f\n", val, temp);
                        flag |= 0x01;
                        break;
                    case HUM:
                        memcpy(&val, &buf[3], sizeof(val));
                        hum = (float)val/100.0;
                        debug("HUM:  %d | %02f\n", val, hum);
                        flag |= 0x02;
                        break;
                    case BAT:
                        batt = buf[3];
                        debug("BAT:  %d \n", batt);
                        flag |= 0x04;
                        break;
                }
            }
            printf("flag %02X\n", flag );
            if(flag==0x07)
            {
                pthread_mutex_lock(&sensor_data_lock);
                seconds = time(NULL);
                sens_mqtt[local_sens->id].last = seconds;
                sprintf(sens_mqtt[local_sens->id].topic, "sossai/appart/%s/sensor", local_sens->name);
                sprintf(sens_mqtt[local_sens->id].payload, "{\"id\":%d,\"name\":\"%s\",\"mac\":\"%s\",\"poll\":%d,\"ts\":%ld,"
                                                           "\"temperature\":%.1f,\"humidity\":%.1f,\"battery\": %d}",
                                                            local_sens->id,   local_sens->name,
                                                            local_sens->smac, local_sens->polltime,
                                                            seconds, temp, hum, batt);
                debug("Topic %s\n", sens_mqtt[local_sens->id].topic);
                debug("Payload %s\n", sens_mqtt[local_sens->id].payload);
                debug("TS %ld\n", sens_mqtt[local_sens->id].last);

                pthread_mutex_unlock(&sensor_data_lock);
                flag = 0;
                read_cicle--;
            }
        } /*while(read_cicle)*/
        shutdown(sock, SHUT_RDWR);
        debug("shutdown socket for sensor %s\n", local_sens->name);
onerror:
        pthread_mutex_unlock(&bt_lock);
        debug("Thread for sensor %s sleeping for %d seconds\n", local_sens->name, local_sens->polltime);
        sleep(local_sens->polltime*err_div);
    }
}

void signal_callback_handler(int signum) {
   debug("Exit %d\n", run);
   run = false;
}

int main()
{
    int sock;
    uint8_t buf[100];
    uint16_t val;
    ssize_t len;
    uint8_t flag, count, count2;
    int sensors, err;
    pthread_t *sens_tid, mqtt_tid;
    sensor_t *s;
    mqtt_th_arg_t mqtt_args;

    signal(SIGINT, signal_callback_handler);

    sensors     = read_sensor_config_file(&s);
    sens_tid    = malloc(sensors * sizeof(pthread_t));
    sens_mqtt   = malloc(sensors * sizeof(sensor_mqtt_t));
    memset(sens_mqtt, 0, sensors * sizeof(sensor_mqtt_t));
    memset(&mqtt_args, 0, sizeof(mqtt_th_arg_t));

    read_mqtt_config_file(&mqtt_args);
    mqtt_args.sensors = sensors;

    run = true;
    if (pthread_mutex_init(&bt_lock, NULL) != 0) {
        printf("\n bt_lock init has failed\n");
        return 1;
    }

    if (pthread_mutex_init(&sensor_data_lock, NULL) != 0) {
        printf("\n sensor_data_lock init has failed\n");
        return 1;
    }

    for(int i=0; i< sensors; i++)
    {
        err = pthread_create( &sens_tid[0], NULL, &sensor_poll_th, (void *) &s[i] );
        if (err != 0)
            printf("\nCan't create thread :[%s]", strerror(err));
    }

    err = pthread_create( &mqtt_tid, NULL, &mqtt_th, (void *) &mqtt_args );
    if (err != 0)
        printf("\nCan't create thread :[%s]", strerror(err));

    while(run)
    {
        sleep(10);
    }

    /*
     * Wait for Exit
     * * * * * * * * * * * * * */
    for(int i=0; i< sensors; i++)
    {
        pthread_join( sens_tid[i], NULL);
    }
    pthread_join( mqtt_tid, NULL);
    free(sens_tid);
    free(s);
    free(sens_mqtt);
    pthread_mutex_destroy(&bt_lock);
    pthread_mutex_destroy(&sensor_data_lock);

    return 0;
}
