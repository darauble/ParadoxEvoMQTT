#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "endpoints.h"
#include "log.h"
#include "mqtt_mgr.h"
#include "paratypes.h"
#include "zmq_helpers.h"

#include "MQTTAsync.h"

extern para_evo_config_t config;

#define SERVER_PATTERN "tcp://%s:%d"
#define AREA_TOPIC_PREFIX "%s/area/"
#define MAIN_AREA_TOPIC AREA_TOPIC_PREFIX "%d"
#define LWT_TOPIC "%s/daemon"
#define AREA_CONTROL_SET "/set"
#define AREA_CONTROL_TOPIC MAIN_AREA_TOPIC AREA_CONTROL_SET
#define AREA_STATE_TOPIC MAIN_AREA_TOPIC "/state"
#define AREA_STATE_JSON "{" \
    "\"num\": %d," \
    "\"name\": \"%s\"," \
    "\"status\": \"%c\"," \
    "\"memory\": \"%c\"," \
    "\"trouble\": \"%c\"," \
    "\"ready\": \"%c\"," \
    "\"programming\": \"%c\"," \
    "\"alarm\": \"%c\"," \
    "\"strobe\": \"%c\"" \
"}"

#define UTILITY_KEY_TOPIC "%s/utilitykey"

#define ZONE_STATUS_TOPIC MAIN_AREA_TOPIC "/zone/%d"
#define ZONE_ALARM_TOPIC MAIN_AREA_TOPIC "/zone/%d/alarm"
#define ZONE_STATE_TOPIC MAIN_AREA_TOPIC "/zone/%d/state"
#define ZONE_STATE_JSON "{" \
    "\"num\": %d," \
    "\"area\": %d," \
    "\"name\": \"%s\"," \
    "\"status\": \"%c\"," \
    "\"alarm\": \"%c\"," \
    "\"fire\": \"%c\"," \
    "\"supervision\": \"%c\"," \
    "\"battery\": \"%c\"," \
    "\"bypassed\": \"%c\"" \
"}"

#define DAEMON_ONLINE "online"
#define DAEMON_OFFLINE "offline"

#define TOPIC_SIZE 256
#define PAYLOAD_SIZE 256

static char topic[TOPIC_SIZE];
static char payload[PAYLOAD_SIZE];
static char lwt_topic[TOPIC_SIZE];
static char url[TOPIC_SIZE];

static const char *mqp_states[] = {
    "disarmed",
    "armed_home",
    "armed_away",
    "armed_night",
    "armed_vacation",
    "armed_custom_bypass",
    "pending",
    "triggered",
    "arming",
    "disarming",
};

static const char *mqz_states[] = {
    "off",
    "on",
};

static MQTTAsync client;

static void *mqtt_mgr_thread(void *context);
static void mqtt_start();
static void mqtt_subscribe();
static void mqtt_area_report(void *area_report_reader);
static void mqtt_zone_report(void *zone_report_reader);
static void mqtt_send(const char *topic, const char *payload);
static void mqtt_send_lwt();
static void mqtt_stop();

static void onConnect(void* context, MQTTAsync_successData* response);
static void onConnectFailure(void* context, MQTTAsync_failureData* response);
static void onDisconnect(void* context, MQTTAsync_successData* response);
static void onDisconnectFailure(void* context, MQTTAsync_failureData* response);
// static void onSend(void* context, MQTTAsync_successData* response);
// static void onSendFail(void* context, MQTTAsync_failureData* response);
static void onSubscribe(void* context, MQTTAsync_successData* response);
static void onSubscribeFailure(void* context, MQTTAsync_failureData* response);

static void *mqtt_area_command = NULL;

pthread_t mqtt_mgr_start(void *context)
{
    pthread_t thread;
    pthread_create(&thread, NULL, mqtt_mgr_thread, context);

    return thread;
}

static void *mqtt_mgr_thread(void *context) {
    __label__ EXIT_MQTT_THREAD;

    log_info("MMGR: starting thread...\n");

    void *kill_subscriber = NULL;
    void *area_report_reader = NULL;
    void *zone_report_reader = NULL;
    int rc;

    mqtt_start();

    if ((rc = z_start_endpoint(context, &area_report_reader, ZMQ_PULL, EPT_MQTT_AREA_REPORT)) != 0) {
        log_error("MMGR: cannot start area report: %d, exiting.\n", rc);
        goto EXIT_MQTT_THREAD;
    }

    if ((rc = z_start_endpoint(context, &zone_report_reader, ZMQ_PULL, EPT_MQTT_ZONE_REPORT)) != 0) {
        log_error("MMGR: cannot start zone report: %d, exiting.\n", rc);
        goto EXIT_MQTT_THREAD;
    }

    // Lamely give time for ZMQ PULL to initialize.
    sleep(1);

    if ((rc = z_connect_endpoint(context, &kill_subscriber, ZMQ_SUB, EPT_KILL)) != 0) {
        log_error("MMGR: cannot subscribe to kill endpoint: %d, exiting.\n", rc);
        goto EXIT_MQTT_THREAD;
    }

    if ((rc = z_connect_endpoint(context, &mqtt_area_command, ZMQ_PUSH, EPT_MQTT_AREA_COMMAND)) != 0) {
        log_error("MMGR: cannot connect to area command: %d, exiting.\n", rc);
        goto EXIT_MQTT_THREAD;
    }

    zmq_setsockopt (kill_subscriber, ZMQ_SUBSCRIBE, "", 0);

    mqtt_subscribe();

    zmq_pollitem_t items[] = {
        { kill_subscriber, 0, ZMQ_POLLIN, 0 },
        { area_report_reader, 0, ZMQ_POLLIN, 0 },
        { zone_report_reader, 0, ZMQ_POLLIN, 0 },
    };

    log_info("MMGR: thread ready!\n");

    while (1) {
        rc = zmq_poll(items, 3, 60000);

        if (items[0].revents & ZMQ_POLLIN) {
            z_drop_message(kill_subscriber);
            log_info("MMGR: received KILL, exitting\n");
            break;
        } else if (items[1].revents & ZMQ_POLLIN) {
            // Area report
            mqtt_area_report(area_report_reader);
        } else if (items[2].revents & ZMQ_POLLIN) {
            // Zone report
            mqtt_zone_report(zone_report_reader);
        }

        // log_debug("MMGR: POLLED: %d\n", rc);

        if (rc == 0) {
            // Timeout every 60 s, send LWT
            log_debug("MMGR: poll timeout, send lwt\n");
            mqtt_send_lwt();
        }
    }

EXIT_MQTT_THREAD:
    mqtt_stop();

    if (kill_subscriber) {
        zmq_close(kill_subscriber);
    }

    if (area_report_reader) {
        zmq_close(area_report_reader);
    }

    if (zone_report_reader) {
        zmq_close(zone_report_reader);
    }

    if (mqtt_area_command) {
        zmq_close(mqtt_area_command);
    }

    sleep(1);

    return NULL;
}

static int mqtt_area_control(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    char topic[TOPIC_SIZE] = "";

    log_debug("MGMR: topic %s arrived\n", topicName);
    log_debug("%.*s\n", (int)message->payloadlen, (char*)message->payload);

    snprintf(topic, TOPIC_SIZE, AREA_TOPIC_PREFIX, config.mqtt_topic);

    log_debug("MMGR: compare topic %s\n", topic);
    log_debug("MMGR: postfix: %s\n", (topicName + strlen(topic) + 1));

    if (
        strncmp(topicName, topic, strlen(topic)) == 0 
        && strcmp(topicName + strlen(topic) + 1, AREA_CONTROL_SET) == 0
    ) {
        int area_num = topicName[strlen(topic)] - 48;
        log_debug("MMGR: received command for area %d\n", area_num);

        para_arm_cmd_t cmd = { .type = CMD_AREA_CONTROL, .num = area_num, .command = -1 };

        if (strcmp((char*)message->payload, HA_ARM_AWAY) == 0) {
            cmd.command = AC_ARM_AWAY;
        } else if (strcmp((char*)message->payload, HA_ARM_HOME) == 0) {
            cmd.command = AC_ARM_HOME;
        } else if (strcmp((char*)message->payload, HA_DISARM) == 0) {
            cmd.command = AC_DISARM;
        }

        if (cmd.command != -1) {
            zmq_msg_t zmessage;

            zmq_msg_init_size (&zmessage, sizeof(para_arm_cmd_t));
            memcpy(zmq_msg_data(&zmessage), &cmd, sizeof(para_arm_cmd_t));
            zmq_msg_send (&zmessage, mqtt_area_command, 0);
            zmq_msg_close (&zmessage);
        }
    } else {
        snprintf(topic, TOPIC_SIZE, UTILITY_KEY_TOPIC, config.mqtt_topic);

        if (strcmp(topicName, topic) == 0) {
            log_debug("MMGR: received Utilty Key %s\n", (char*)message->payload);
            int key_num = strtol((char*)message->payload, NULL, 10);

            para_arm_cmd_t cmd = { .type = CMD_UTILITY_KEY, .num = key_num, .command = 0 };
            zmq_msg_t zmessage;
            
            zmq_msg_init_size (&zmessage, sizeof(para_arm_cmd_t));
            memcpy(zmq_msg_data(&zmessage), &cmd, sizeof(para_arm_cmd_t));
            zmq_msg_send (&zmessage, mqtt_area_command, 0);
            zmq_msg_close (&zmessage);
        }
    }

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

static void mqtt_subscribe()
{
    snprintf(topic, TOPIC_SIZE, UTILITY_KEY_TOPIC, config.mqtt_topic);

    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

    opts.onSuccess = onSubscribe;
    opts.onFailure = onSubscribeFailure;
    opts.context = client;

    int rc = MQTTAsync_subscribe(client, topic, 1, &opts);

    if (rc == MQTTASYNC_SUCCESS) {
        log_debug("MMGR: Subscribed utility key topic.\n");
    } else {
        log_error("MMGR: Subscription utility key control failed: %d\n", rc);
    }
}

static void mqtt_area_report(void *area_report_reader)
{
    para_area_t *area = (para_area_t*) z_receive(area_report_reader);

    if (!area) {
        log_error("MMGR: area report not received!\n");
        return;
    }

    if (area->firstreport) {
        // First incoming report, subscribe to this area's control
        snprintf(topic, TOPIC_SIZE, AREA_CONTROL_TOPIC, config.mqtt_topic, area->num);

        MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

        opts.onSuccess = onSubscribe;
        opts.onFailure = onSubscribeFailure;
        opts.context = client;

        int rc = MQTTAsync_subscribe(client, topic, 1, &opts);
        if (rc == MQTTASYNC_SUCCESS) {
            log_debug("MMGR: Subscribed to area %d: %d\n", area->num, rc);
        } else {
            log_error("MMGR: Subscription to area %d control failed: %d\n", area->num, rc);
        }
    }

    const char *area_state = mqp_states[area->mqtt_state];

    snprintf(topic, TOPIC_SIZE, MAIN_AREA_TOPIC, config.mqtt_topic, area->num);
    mqtt_send(topic, area_state);

    snprintf(topic, TOPIC_SIZE, AREA_STATE_TOPIC, config.mqtt_topic, area->num);
    snprintf(payload, PAYLOAD_SIZE, AREA_STATE_JSON, 
        area->num,
        area->name,
        area->status,
        area->memory,
        area->trouble,
        area->ready,
        area->programming,
        area->alarm,
        area->strobe
    );
    mqtt_send(topic, payload);

    free(area);
}

static void mqtt_zone_report(void *zone_report_reader)
{
    para_zone_t *zone = (para_zone_t*) z_receive(zone_report_reader);

    if (!zone) {
        log_error("MMGR: zone report not received!\n");
        return;
    }

    const char *zone_state = mqz_states[zone->mqtt_state];
    snprintf(topic, TOPIC_SIZE, ZONE_STATUS_TOPIC, config.mqtt_topic, zone->area, zone->num);
    mqtt_send(topic, zone_state);

    const char *alarm_state = zone->alarm == RS_ZONE_IN_ALARM ? mqz_states[MQZ_ON] : mqz_states[MQZ_OFF];
    snprintf(topic, TOPIC_SIZE, ZONE_ALARM_TOPIC, config.mqtt_topic, zone->area, zone->num);
    mqtt_send(topic, alarm_state);

    snprintf(topic, TOPIC_SIZE, ZONE_STATE_TOPIC, config.mqtt_topic, zone->area, zone->num);
    snprintf(payload, PAYLOAD_SIZE, ZONE_STATE_JSON,
        zone->num,
        zone->area,
        zone->name,
        zone->status,
        zone->alarm,
        zone->fire,
        zone->supervision,
        zone->battery,
        zone->bypassed
    );
    // log_debug("MMGR: zone status: %s\n", payload);
    mqtt_send(topic, payload);

    free(zone);
}

static void mqtt_start()
{
    snprintf(url, TOPIC_SIZE, SERVER_PATTERN, config.mqtt_server, config.mqtt_port);

    MQTTAsync_create(&client, url, config.mqtt_client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);

    int rc;

    if ((rc = MQTTAsync_setCallbacks(client, client, NULL, mqtt_area_control, NULL)) != MQTTASYNC_SUCCESS)
    {
        log_error("Failed to set callbacks, return code %d\n", rc);
    }//*/

    MQTTAsync_willOptions will_opts = MQTTAsync_willOptions_initializer;
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;

    snprintf(lwt_topic, TOPIC_SIZE, LWT_TOPIC, config.mqtt_topic);

    conn_opts.keepAliveInterval = 60;
    conn_opts.minRetryInterval = 10;
    conn_opts.maxRetryInterval = 300;
    conn_opts.automaticReconnect = 1;
    conn_opts.cleansession = 1;
    conn_opts.onSuccess = onConnect;
    conn_opts.onFailure = onConnectFailure;
    conn_opts.context = client;

    conn_opts.username = config.mqtt_login;
    conn_opts.password = config.mqtt_password;
    
    conn_opts.will = &will_opts;
    will_opts.topicName = lwt_topic;
    will_opts.message = DAEMON_OFFLINE;
    will_opts.retained = config.mqtt_retain;
    

    MQTTAsync_connect(client, &conn_opts);
}

static int mqtt_disconnected = 0;

static void mqtt_stop()
{
    MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
    opts.onSuccess = onDisconnect;
    opts.onFailure = onDisconnectFailure;
    opts.context = client;

    int rc;

    if ((rc = MQTTAsync_disconnect(client, &opts)) == MQTTASYNC_SUCCESS) {
        log_info("MMGR: stopping MQTT client\n");
    } else {
        log_error("MMGR: error stopping MQTT client: %d\n", rc);
    }

    struct timespec tv = {
        .tv_sec = 0,
        .tv_nsec = 100000000,
    };

    while (mqtt_disconnected == 0) {
        nanosleep(&tv, NULL);
    }

    MQTTAsync_destroy(&client);
}

static void onConnect(void* context, MQTTAsync_successData* response)
{
    log_info("MMGR: Connected to MQTT server.\n");
    mqtt_send_lwt();
}

static void onConnectFailure(void* context, MQTTAsync_failureData* response)
{
    log_error("MMGR: Failed to connect to MQTT server: [%d] - %s.\n", response->code, response->message);
}

static void onDisconnect(void* context, MQTTAsync_successData* response)
{
    mqtt_disconnected = 1;
    log_info("MMGR: MQTT client successfully disconnected.\n");
}

static void onDisconnectFailure(void* context, MQTTAsync_failureData* response) {
    mqtt_disconnected = 1;
    log_error("MMGR: MQTT failed to disconnect: [%d] - %s\n", response->code, response->message);
}

// static void onSend(void* context, MQTTAsync_successData* response)
// {
    
// }

// static void onSendFail(void* context, MQTTAsync_failureData* response)
// {
//     log_error("MMGR: Message sending failed: [%d] - %s.\n", response->code, response->message);
// }

static void onSubscribe(void* context, MQTTAsync_successData* response)
{
        log_info("MMGR: Subscribe succeeded.\n");
}
 
static void onSubscribeFailure(void* context, MQTTAsync_failureData* response)
{
        log_error("MMGR: Subscribe failed: [%d] - %s\n", response->code, response->message);
}

static void mqtt_send_lwt()
{
    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = DAEMON_ONLINE;
    msg.payloadlen = strlen(msg.payload);
    msg.qos = 1;
    msg.retained = config.mqtt_retain;

    MQTTAsync_sendMessage(client, lwt_topic, &msg, NULL);
}

static void mqtt_send(const char *topic, const char *payload)
{
    MQTTAsync_message msg = MQTTAsync_message_initializer;
    msg.payload = (char *) payload;
    msg.payloadlen = strlen(msg.payload);
    msg.qos = 1;
    msg.retained = config.mqtt_retain;

    MQTTAsync_sendMessage(client, topic, &msg, NULL);
}
