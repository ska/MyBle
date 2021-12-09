#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <mosquitto.h>
#include <time.h>
#include <bluetooth/bluetooth.h>
//#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "utils.h"
#include "configfile.h"
#include "customdatatypes.h"

#define TEMP 0x0012
#define HUM  0x0015
#define BAT  0x000E

volatile bool run;
pthread_mutex_t bt_lock;
pthread_mutex_t sensor_data_lock;/**/
sensor_mqtt_t *sens_mqtt;
struct mosquitto * mosq = NULL;

void connect_callback(struct mosquitto * mosq, void * obj, int result)
{
    debug("connect callback, rc=%d\n", result);
}
void message_callback(struct mosquitto * mosq, void * obj,
                      const struct mosquitto_message * message) {
    debug("message callback\n");
}
void publish_callback(struct mosquitto * mosq, void * userdata, int mid)
{
    debug("message published\n");
}

void* mqtt_th(void *arg)
{
    mqtt_th_arg_t *mqtt_args = ((mqtt_th_arg_t *) arg);
    char clientid[24];
    int rc = 0;
    time_t nextrun;


    if (0 == mqtt_args->address[0]) {
        error("Args error");
        pthread_exit ((void*)(-1));
    }
    debug("MQTT TH. Broker info: %s:%d \n", mqtt_args->address, mqtt_args->port);

    mosquitto_lib_init();
    memset(clientid, 0, 24);
    snprintf(clientid, 23, "%d", getpid());
    mosq = mosquitto_new(clientid, true, 0);
    if (NULL == mosq) {
        error("mosquitto_new error:  %s\n", strerror(errno));
        pthread_exit ((void*)(-1));
    }
    mosquitto_connect_callback_set(mosq, connect_callback);
    mosquitto_message_callback_set(mosq, message_callback);
    mosquitto_publish_callback_set(mosq, publish_callback);
    //rc = mosquitto_connect(mosq, MQTT_BROKER_IP, MQTT_BROKER_PORT, 60);
    rc = mosquitto_connect(mosq, mqtt_args->address, mqtt_args->port, 60);
    if (NULL == mosq) {
        printf("mosquitto_connect error:  %s\n", strerror(errno));
        pthread_exit ((void*)(-2));
    }

    nextrun = time(NULL);
    while(run)
    {
        if(nextrun > time(NULL))
        {
            sleep(1);
            continue;
        }

        debug("mqtt thread waked up!\n");
        pthread_mutex_lock(&sensor_data_lock);
        rc = mosquitto_loop(mosq, -1, 1);
        if(run && rc){
            error("MQTT connection error!\n");
            sleep(10);
            mosquitto_reconnect(mosq);
        }

        for(int i=0; i<mqtt_args->sensors; i++)
        {
            if(sens_mqtt[i].topic[0] != 0)
            {
                rc = mosquitto_publish(mosq,
                                       NULL,
                                       sens_mqtt[i].topic,
                                       strlen(sens_mqtt[i].payload),
                                       (void * ) sens_mqtt[i].payload, 1,
                                       false);
                debug("mosquitto_publish %d\n", rc);
            }
            /*100mS*/
            usleep(100000);
        }
        pthread_mutex_unlock(&sensor_data_lock);
        nextrun = (time(NULL)+30);
        debug("MQTT TH; going to sleep!\n");
    }
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    debug("MQTT disconnected\n");
    run = 0;
    pthread_exit ((void*)(0));
}

#ifdef CLASSIC_BT_MODE
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

    debug("Start connect to %02X:%02X:%02X:%02X:%02X:%02X\n",
           dst->b[5], dst->b[4], dst->b[3], dst->b[2], dst->b[1], dst->b[0] );

    err = connect(sock, (struct sockaddr *) &addr, sizeof(addr));

    debug("Connected \n");
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

    //printf("%s\n", __func__);
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
    return 0;
}
#if 0
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
    time_t nextrun;

    flag = 0x00;

    if(NULL == local_sens)
        pthread_exit ((void*)(-1));

    nextrun = time(NULL);
    while(run)
    {
        if(nextrun > time(NULL))
        {
            if(!run)
                break;
            sleep(1);
            continue;
        }

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
            if(flag==0x07)
            {
                pthread_mutex_lock(&sensor_data_lock);
                seconds = time(NULL);
                sens_mqtt[local_sens->id].last = seconds;
                sprintf(sens_mqtt[local_sens->id].topic, "sossai/appart/%s/sensor", local_sens->name);
                /* Gui ha problemi a interpretare le virgole, le sostituisco con # */
                /*
                sprintf(sens_mqtt[local_sens->id].payload, "{\"id\":%d,\"name\":\"%s\",\"mac\":\"%s\",\"poll\":%d,\"ts\":%ld,"
                                                           "\"temperature\":%.1f,\"humidity\":%.1f,\"battery\": %d}",
                                                            local_sens->id,   local_sens->name,
                                                            local_sens->smac, local_sens->polltime,
                                                            seconds, temp, hum, batt);
                */
                sprintf(sens_mqtt[local_sens->id].payload, "\'{\"id\":%d#\"name\":\"%s\"#\"mac\":\"%s\"#\"poll\":%d#\"ts\":%ld#"
                                                           "\"temperature\":%.1f#\"humidity\":%.1f#\"battery\":%d}\'",
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
        debug("Thread for sensor %s sleeping for %d seconds\n", local_sens->name, (err_div*local_sens->polltime) );
        nextrun = (err_div*(time(NULL)+local_sens->polltime));
    }
    debug("Closing %d (%s) sensor thread\n", local_sens->id, local_sens->name);
    run = 0;
    pthread_exit ((void*)(0));
}
#else

void* sensors_poll_th(void *arg)
{
    sensor_t **local_sens = ((sensor_t **) arg);
    uint8_t i;
    time_t nextrun;
    uint16_t val;
    uint8_t err_div;
    float temp, hum;
    uint8_t batt;
    int sock, err;
    uint8_t buf[20];
    ssize_t len;
    uint8_t flag, read_cicle;
    struct pollfd ufds[1];
    int rv;
    int timeout;
    time_t seconds;

    nextrun = time(NULL);
    while(run)
    {
        /*Check howto*/
        if(nextrun > time(NULL))
        {
            if(!run)
            {
                debug("Exit on break run: %d\n", run);
                break;
            }
            sleep(1);
            continue;
        }
        /*Time to run*/
        for(i=0; i<4 && run; i++)
        {
            debug("START Thread for sensor %s running\n", (*local_sens)[i].name);
            pthread_mutex_lock(&bt_lock);
            debug("Try to connect at %s\n", (*local_sens)[i].name);
            err = bt_io_connect(&sock, (*local_sens)[i].mac);
            if(err < 0)
            {
                fprintf(stderr, "bt_io_connect ERROR %d\n", err);
            }
#if 0
            err = bt_write_start_cmd(sock);
            if(err <= 0)
            {
                fprintf(stderr, "bt_write_start_cmd ERROR %d\n", err);
            }
#endif
            memset(buf, 0, sizeof(buf));
#if 0
            for(int j=0; j<3; j++)
            {
                debug("read from socker j: %d\n", j);
                len = read(sock, buf, sizeof(buf));
                debug("END read from socker len: %d\n", len);
            }
#endif

            ufds[0].fd = sock;
            ufds[0].events = POLLIN;
            ufds[0].revents = 0;
            timeout = 0;
            while(run)
            {
                debug("timeout: %d | flag: %02X\n", timeout, flag);
                rv = poll(ufds, 1, 2000);
                debug("timeout: %d | flag: %02X | rv: %d\n", timeout, flag, rv);
                if(rv == -1)
                {
                    printf("Poll  error!\n");
                    //perror("poll");
                } else if (rv == 0)
                {
                    printf("Timeout occurred!\n");
                    timeout ++;
                } else {
                    debug("ufds[0].revents: %d\n", ufds[0].revents);
                    if (ufds[0].revents & POLLIN)
                    {
                        printf("Poll  read!\n");
                        len = read(sock, buf, sizeof(buf));
                        fprintf(stdout, "Read len: %d - ", len);
                        for(int j=0; j<len; j++)
                            fprintf(stdout, "buf[%d]: 0x%02X ", j, buf[j] );
                        fprintf(stdout, "\n", len);


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
                    } else
                        break;

                }

                if(flag==0x07)
                    break;

                if(timeout>10)
                    break;
            }
            flag = 0x00;
            shutdown(sock, SHUT_RDWR);
            debug("shutdown socket for sensor %s\n", (*local_sens)[i].name);


            pthread_mutex_lock(&sensor_data_lock);
            seconds = time(NULL);
            sens_mqtt[(*local_sens)[i].id].last = seconds;
            sprintf(sens_mqtt[(*local_sens)[i].id].topic, "sossai/appart/%s/sensor", (*local_sens)[i].name);
            /* Gui ha problemi a interpretare le virgole, le sostituisco con # */
            /*
            sprintf(sens_mqtt[local_sens->id].payload, "{\"id\":%d,\"name\":\"%s\",\"mac\":\"%s\",\"poll\":%d,\"ts\":%ld,"
                                                       "\"temperature\":%.1f,\"humidity\":%.1f,\"battery\": %d}",
                                                        local_sens->id,   local_sens->name,
                                                        local_sens->smac, local_sens->polltime,
                                                        seconds, temp, hum, batt);
            */
            sprintf(sens_mqtt[(*local_sens)[i].id].payload, "\'{\"id\":%d#\"name\":\"%s\"#\"mac\":\"%s\"#\"poll\":%d#\"ts\":%ld#"
                                                       "\"temperature\":%.1f#\"humidity\":%.1f#\"battery\":%d}\'",
                                                        (*local_sens)[i].id,   (*local_sens)[i].name,
                                                        (*local_sens)[i].smac, (*local_sens)[i].polltime,
                                                        seconds, temp, hum, batt);
            debug("Topic %s\n", sens_mqtt[(*local_sens)[i].id].topic);
            debug("Payload %s\n", sens_mqtt[(*local_sens)[i].id].payload);
            debug("TS %ld\n", sens_mqtt[(*local_sens)[i].id].last);

            pthread_mutex_unlock(&sensor_data_lock);
            flag = 0;

            debug("STOP  Thread for sensor %s running\n", (*local_sens)[i].name);
            pthread_mutex_unlock(&bt_lock);
            sleep(2);
        }   /*for(i=0; i<3 && run; i++)*/

        nextrun = time(NULL) + 120;
    }
    pthread_exit ((void*)(0));
}
#endif
#endif
struct hci_request ble_hci_request(uint16_t ocf, int clen, void * status, void * cparam)
{
    struct hci_request rq;
    memset(&rq, 0, sizeof(rq));
    rq.ogf = OGF_LE_CTL;
    rq.ocf = ocf;
    rq.cparam = cparam;
    rq.clen = clen;
    rq.rparam = status;
    rq.rlen = 1;
    return rq;
}

void* sensors_poll_th(void *arg)
{
    sensor_t **local_sens = ((sensor_t **) arg);
    int ret, status;
    int sens;
    time_t seconds;
    uint16_t val, temp, battmv;
    uint8_t pc, hum, batt;
    int8_t  rssi;
    word_misto tmpw;

    int device = hci_open_dev(1);
    if ( device < 0 ) {
        device = hci_open_dev(0);
        if (device >= 0) {
            printf("Using hci0\n");
        }
    }
    else {
        printf("Using hci1\n");
    }

    if ( device < 0 ) {
        perror("Failed to open HCI device.");
        pthread_exit ((void*)(-1));
    }

    // Set BLE scan parameters.
    le_set_scan_parameters_cp scan_params_cp;
    memset(&scan_params_cp, 0, sizeof(scan_params_cp));
    scan_params_cp.type 			= 0x00;
    scan_params_cp.interval 		= htobs(0x0010);
    scan_params_cp.window 			= htobs(0x0010);
    scan_params_cp.own_bdaddr_type 	= 0x00; // Public Device Address (default).
    scan_params_cp.filter 			= 0x00; // Accept all.

    struct hci_request scan_params_rq = ble_hci_request(OCF_LE_SET_SCAN_PARAMETERS, LE_SET_SCAN_PARAMETERS_CP_SIZE, &status, &scan_params_cp);

    ret = hci_send_req(device, &scan_params_rq, 1000);
    if ( ret < 0 ) {
        hci_close_dev(device);
        perror("Failed to set scan parameters data.");
        pthread_exit ((void*)(-1));
    }
    // Set BLE events report mask.
    le_set_event_mask_cp event_mask_cp;
    memset(&event_mask_cp, 0, sizeof(le_set_event_mask_cp));
    int i = 0;
    for ( i = 0 ; i < 8 ; i++ )
        event_mask_cp.mask[i] = 0xFF;

    struct hci_request set_mask_rq = ble_hci_request(OCF_LE_SET_EVENT_MASK, LE_SET_EVENT_MASK_CP_SIZE, &status, &event_mask_cp);
    ret = hci_send_req(device, &set_mask_rq, 1000);
    if ( ret < 0 ) {
        hci_close_dev(device);
        perror("Failed to set event mask.");
        pthread_exit ((void*)(-1));
    }

    // Enable scanning.
    le_set_scan_enable_cp scan_cp;
    memset(&scan_cp, 0, sizeof(scan_cp));
    scan_cp.enable 		= 0x01;	// Enable flag.
    scan_cp.filter_dup 	= 0x00; // Filtering disabled.

    struct hci_request enable_adv_rq = ble_hci_request(OCF_LE_SET_SCAN_ENABLE, LE_SET_SCAN_ENABLE_CP_SIZE, &status, &scan_cp);

    ret = hci_send_req(device, &enable_adv_rq, 1000);
    if ( ret < 0 ) {
        hci_close_dev(device);
        perror("Failed to enable scan.");
        pthread_exit ((void*)(-1));
    }

    // Get Results.
    struct hci_filter nf;
    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    if ( setsockopt(device, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0 ) {
        hci_close_dev(device);
        perror("Could not set socket options\n");
        pthread_exit ((void*)(-1));
    }

    uint8_t buf[HCI_MAX_EVENT_SIZE];
    evt_le_meta_event * meta_event;
    le_advertising_info * info;
    int len;

    int count = 0;
    unsigned now = (unsigned)time(NULL);
    unsigned last_detection_time = now;
    // Keep scanning until we see nothing for 10 secs or we have seen lots of advertisements.  Then exit.
    // We exit in this case because the scan may have failed or stopped. Higher level code can restart
    //while ( last_detection_time - now < 10 && count < 1000 )
    while ( run )
    {
        len = read(device, buf, sizeof(buf));
        if ( len >= HCI_EVENT_HDR_SIZE )
        {
            count++;
            last_detection_time = (unsigned)time(NULL);
            meta_event = (evt_le_meta_event*)(buf+HCI_EVENT_HDR_SIZE+1);
            if ( meta_event->subevent == EVT_LE_ADVERTISING_REPORT )
            {
                uint8_t reports_count = meta_event->data[0];
                void * offset = meta_event->data + 1;
                while ( reports_count-- )
                {
                    info = (le_advertising_info *)offset;

                    if(info->length!=17)
                    {
                        //printf("continue: L:%d\n", info->length);
                        continue;
                    }

                    char addr[18];
                    ba2str(&(info->bdaddr), addr);

                    for(sens=0; sens<5; sens++)
                    {
                        if( 0==strcmp(&addr, &(*local_sens)[sens].smac ) )
                        {
/*
                            printf("Sensore %d: %s - %s\n", sens, (*local_sens)[sens].smac, (*local_sens)[sens].name);
                            //printf("Mac: %s RSSI: %d\n", addr, (int8_t)info->data[info->length]);

                            printf("Data: L:%d - ", info->length);
                            for (int i = 0; i<info->length; i++) {
                                printf(" %02X", (unsigned char)info->data[i]);
                            }
                            printf("\n");


                            printf("Mac: ");
                            for (int i = 4; i<info->length && i<10; i++) {
                                printf("%02X%c", (unsigned char)info->data[i], ((i<9)?':':' ') );
                            }
*/
                            tmpw.alto = (unsigned char)info->data[10];
                            tmpw.basso = (unsigned char)info->data[11];
                            temp = tmpw.completo;
                            tmpw.alto = (unsigned char)info->data[14];
                            tmpw.basso = (unsigned char)info->data[15];
                            battmv = tmpw.completo;

                            rssi = info->data[17];
                            hum  = info->data[12];
                            batt = info->data[13];
                            pc   = info->data[16];
/*
                            printf(" -> RSSI: %d | PC: %d | Temp: %ddC | Hum: %d%% | Batt: %d%% | Batt: %dmV",
                                   rssi, pc, temp, hum, batt, battmv);
                            printf("\n\n");
*/
                            pthread_mutex_lock(&sensor_data_lock);
                            seconds = time(NULL);
                            sens_mqtt[(*local_sens)[sens].id].last = seconds;
                            sprintf(sens_mqtt[(*local_sens)[sens].id].topic, "sossai/appart/%s/sensor", (*local_sens)[sens].name);
                            /* Gui ha problemi a interpretare le virgole, le sostituisco con # */
                            /*
                            sprintf(sens_mqtt[local_sens->id].payload, "{\"id\":%d,\"name\":\"%s\",\"mac\":\"%s\",\"poll\":%d,\"ts\":%ld,"
                                                                       "\"temperature\":%.1f,\"humidity\":%d,\"battery\": %d}",
                                                                        local_sens->id,   local_sens->name,
                                                                        local_sens->smac, local_sens->polltime,
                                                                        seconds, temp, hum, batt);
                            */
                            sprintf(sens_mqtt[(*local_sens)[sens].id].payload, "\'{\"id\":%d#\"name\":\"%s\"#\"mac\":\"%s\"#\"poll\":%d#\"ts\":%ld#"
                                                                       "\"temperature\":%.1f#\"humidity\":%d#\"tempdc\":%d#\"battery\":%d#\"battmv\":%d}\'",
                                                                        (*local_sens)[sens].id,   (*local_sens)[sens].name,
                                                                        (*local_sens)[sens].smac, (*local_sens)[sens].polltime,
                                                                        seconds, (float)temp/(10.0), hum, temp, batt, battmv);
                            debug("Topic %s\n", sens_mqtt[(*local_sens)[sens].id].topic);
                            debug("Payload %s\n", sens_mqtt[(*local_sens)[sens].id].payload);
                            debug("TS %ld\n", sens_mqtt[(*local_sens)[sens].id].last);

                            pthread_mutex_unlock(&sensor_data_lock);


                        }
                    }
                    offset = info->data + info->length + 2;
                }
            }
        }
        now = (unsigned)time(NULL);
    }


    // Disable scanning.
    memset(&scan_cp, 0, sizeof(scan_cp));
    scan_cp.enable = 0x00;	// Disable flag.

    struct hci_request disable_adv_rq = ble_hci_request(OCF_LE_SET_SCAN_ENABLE, LE_SET_SCAN_ENABLE_CP_SIZE, &status, &scan_cp);
    ret = hci_send_req(device, &disable_adv_rq, 1000);
    if ( ret < 0 ) {
        hci_close_dev(device);
        perror("Failed to disable scan.");
        return 0;
    }

    hci_close_dev(device);

    run = 0;
    pthread_exit ((void*)(0));
}


void signal_callback_handler(int signum) {
   debug("Exit %d\n", run);
   run = false;
}

int main(int argc, char **argv)
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

    /*
     * START!!!
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    printf("Hello from %s - PID: %d\n", argv[0], getpid() );

    /*
     * Prereq check
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    if(0!=ucUtilsOpenLockFile())
    {
        error("%s already running!\n", argv[0]);
        return -1;
    }

    /*
     * Signals
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    signal(SIGINT, signal_callback_handler);

    /*
     * Read Config file/Setup Data
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    sensors     = read_sensor_config_file(&s);
    debug("Found %d sensors\n", sensors );
    sens_tid    = malloc(sensors * sizeof(pthread_t));
    sens_mqtt   = malloc(sensors * sizeof(sensor_mqtt_t));
    memset(sens_mqtt, 0, sensors * sizeof(sensor_mqtt_t));
    memset(&mqtt_args, 0, sizeof(mqtt_th_arg_t));
    read_mqtt_config_file(&mqtt_args);
    mqtt_args.sensors = sensors;
    run = true;

    /*
     * Mutex
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    if (pthread_mutex_init(&bt_lock, NULL) != 0) {
        printf("\n bt_lock init has failed\n");
        return 1;
    }

    if (pthread_mutex_init(&sensor_data_lock, NULL) != 0) {
        printf("\n sensor_data_lock init has failed\n");
        return 1;
    }

    /*
     * BLE/Sensors Threads
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#ifdef CLASSIC_BT_MODE
#if 0
    for(int i=0; i< sensors; i++)
    {
        err = pthread_create( &sens_tid[0], NULL, &sensor_poll_th, (void *) &s[i] );
        if (err != 0)
            printf("\nCan't create thread :[%s]", strerror(err));
    }
#else

    err = pthread_create( &sens_tid[0], NULL, &sensors_poll_th, (void *) &s );
    if (err != 0)
        printf("\nCan't create thread :[%s]", strerror(err));
#endif
#endif
    err = pthread_create( &sens_tid[0], NULL, &sensors_poll_th, (void *) &s );
    if (err != 0)
        printf("\nCan't create thread :[%s]", strerror(err));


    /*
     * MQTT Thread
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    err = pthread_create( &mqtt_tid, NULL, &mqtt_th, (void *) &mqtt_args );
    if (err != 0)
        printf("\nCan't create thread :[%s]", strerror(err));

    uint8_t c = 0;
    while(run)
    {
        sleep(2);
    }

    /*
     * Wait for Exit
     * * * * * * * * * * * * * */
    #if 0
    for(int i=0; i< sensors; i++)
    {
        debug("Wait join sens_tid: %d \n", i);
        pthread_join( sens_tid[i], NULL);
    }
#else
    pthread_join( sens_tid[0], NULL);
#endif
    debug("Wait join mqtt_tid\n");
    pthread_join( mqtt_tid, NULL);
    free(sens_tid);
    free(s);
    free(sens_mqtt);
    pthread_mutex_destroy(&bt_lock);
    pthread_mutex_destroy(&sensor_data_lock);


    ucUtilsCloseUnlockFile();

    debug("END END END\n");
    return 0;
}
