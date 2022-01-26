/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * para_mgr.c
 *
 *  Created on: Jan 7, 2022
 *      Author: Darau, blė
 *
 *  This file is a part of personal use utilities developed to be used
 *  on various Linux devices.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "zmq_helpers.h"

#include "log.h"
#include "endpoints.h"
#include "para_mgr.h"
#include "paratypes.h"

static para_area_t *areas[MAX_AREAS];
static para_zone_t *zones[MAX_ZONES];

static void *para_mgr_thread(void *context);
static void *para_mgr_initial_request_thread(void *serial_sender);
static void *para_mgr_area_status_thread(void *serial_sender);
static void para_request_area_status(void *serial_sender, int areanum);
static void para_request_area_label(void *serial_sender, int areanum);
static void para_area_arm(void *serial_sender, int areanum, char arm_type, char *user_code);
static void para_area_disarm(void *serial_sender, int areanum, char *user_code);
static void para_area_quick_arm(void *serial_sender, int areanum, char arm_type);
static void para_request_zone_status(void *serial_sender, int zonenum);
static void para_request_zone_label(void *serial_sender, int zonenum);
static void para_utility_key(void *serial_sender, int utility_key);
static void para_process_prt3_event(char *prt3_string, void *serial_sender, void *mqtt_area_report, void *mqtt_zone_report);
static void para_process_prt3_response(char *prt3_string, void *mqtt_area_report, void *mqtt_zone_report);
static void para_process_command(void *mqtt_area_command, void *serial_sender);
static int  get_number_at_substring(char *str, size_t length);
static void set_label(char *dst, const char *src);
static void update_area_record(int area_num, char *prt3_string);
static void update_zone_record(int zone_num, char *prt3_string);
static void send_area_report(int area_num, void *mqtt_sender);
static void send_zone_report(int zone_num, void *mqtt_sender);

static void area_set_status(int area_num, char status);
static void area_set_memory(int area_num, char memory);
static void area_set_trouble(int area_num, char trouble);
static void area_set_ready(int area_num, char ready);
static void area_set_programming(int area_num, char programming);
static void area_set_alarm(int area_num, char alarm);
static void area_set_strobe(int area_num, char strobe);
static void area_update_mqtt_state(int area_num);

static void zone_set_status(int zone_num, char status);
static void zone_set_alarm(int zone_num, char alarm);
static void zone_set_fire(int zone_num, char fire);
static void zone_set_supervision(int zone_num, char supervision);
static void zone_set_battery(int zone_num, char battery);
static void zone_set_bypassed(int zone_num, char bypassed);
static void zone_update_mqtt_state(int zone_num);
static void zone_update_area_alarm(int zone_num);

void para_mgr_init()
{
    for (int i = 0; i < MAX_AREAS; i++) {
        areas[i] = NULL;
    }

    for (int i = 0; i < MAX_ZONES; i++) {
        zones[i] = NULL;
    }
}

/*
 * Initialize appropriate area memory.
 * NOTE: area index is 1 based!
 */
int para_mgr_set_area(int area_num) {
    log_verbose("PMGR: initialize area %d\n", area_num);

    int aidx = area_num - 1;

    if (areas[aidx] == NULL) {
        areas[aidx] = malloc(sizeof(para_area_t));
    }

    if (areas[aidx]) {
        memset(areas[aidx], 0, sizeof(para_area_t));
        areas[aidx]->num = area_num;
        areas[aidx]->firstreport = 1;
        return 0;
    } else {
        return -1;
    }
}

int para_mgr_set_zone(int area_num, int zone_num)
{
    log_verbose("PMGR: initialize zone %d on area %d\n", zone_num, area_num);

    int zidx = zone_num - 1;

    if (zones[zidx] == NULL) {
        zones[zidx] = malloc(sizeof(para_zone_t));
    }

    // Assume the area is already initialized. Otherwise crash-boom-bang.

    if (zones[zidx]) {
        memset(zones[zidx], 0, sizeof(para_zone_t));
        zones[zidx]->num = zone_num;
        zones[zidx]->area = area_num;
        zones[zidx]->bypassed = RS_OK;
        
        return 0;
    } else {
        return -1;
    }
}

pthread_t para_mgr_start(void *context)
{
    pthread_t thread;

    pthread_create(&thread, NULL, para_mgr_thread, context);

    return thread;
}

void para_mgr_clean() {
    for (int i = 0; i < MAX_AREAS; i++) {
        if (areas[i] != NULL) {
            free(areas[i]);
        }
    }

    for (int i = 0; i < MAX_ZONES; i++) {
        if (zones[i] != NULL) {
            free(zones[i]);
        }
    }
}

static void *para_mgr_thread(void *context)
{
    __label__ EXIT_PMGR_THREAD;

    log_info("PMGR: starting thread...\n");

    void *kill_subscriber = NULL;
    void *serial_receiver = NULL;
    void *serial_sender = NULL;
    void *mqtt_area_command = NULL;
    void *mqtt_area_report = NULL;
    void *mqtt_zone_report = NULL;
    int rc;

    if ((rc = z_start_endpoint(context, &serial_receiver, ZMQ_PULL, EPT_SERIAL_READ)) != 0) {
        log_error("PMGR: cannot start serial receiver: %d, exiting.\n", rc);
        goto EXIT_PMGR_THREAD;
    }

    if ((rc = z_start_endpoint(context, &mqtt_area_command, ZMQ_PULL, EPT_MQTT_AREA_COMMAND)) != 0) {
        log_error("PMGR: cannot start command receiver: %d, exiting.\n", rc);
        goto EXIT_PMGR_THREAD;
    }

    // Lamely give time for ZMQ PULL to initialize.
    sleep(1);
    
    if ((rc = z_connect_endpoint(context, &kill_subscriber, ZMQ_SUB, EPT_KILL)) != 0) {
        log_error("PMGR: cannot subscribe to kill endpoint: %d, exiting.\n", rc);
        goto EXIT_PMGR_THREAD;
    }

    zmq_setsockopt (kill_subscriber, ZMQ_SUBSCRIBE, "", 0);

    if ((rc = z_connect_endpoint(context, &serial_sender, ZMQ_PUSH, EPT_SERIAL_WRITE)) != 0) {
        log_error("PMGR: cannot start serial sender: %d, exiting\n", rc);
        goto EXIT_PMGR_THREAD;
    }

    if ((rc = z_connect_endpoint(context, &mqtt_area_report, ZMQ_PUSH, EPT_MQTT_AREA_REPORT)) != 0) {
        log_error("PMGR: cannot start serial sender: %d, exiting\n", rc);
        goto EXIT_PMGR_THREAD;
    }

    if ((rc = z_connect_endpoint(context, &mqtt_zone_report, ZMQ_PUSH, EPT_MQTT_ZONE_REPORT)) != 0) {
        log_error("PMGR: cannot start serial sender: %d, exiting\n", rc);
        goto EXIT_PMGR_THREAD;
    }
    
    zmq_pollitem_t items[] = {
        { kill_subscriber, 0, ZMQ_POLLIN, 0 },
        { serial_receiver, 0, ZMQ_POLLIN, 0 },
        { mqtt_area_command, 0, ZMQ_POLLIN, 0 },
    };

    log_info("PMGR: thread ready!\n");

    pthread_t requestthread;
    pthread_create(&requestthread, NULL, para_mgr_initial_request_thread, serial_sender);
    
    while (1) {
        int rc = zmq_poll(items, 3, config.area_status_period * 1000);

        if (items[0].revents & ZMQ_POLLIN) {
            z_drop_message(kill_subscriber);
            log_info("PMGR: received KILL, exitting\n");
            break;
        } else if (items[1].revents & ZMQ_POLLIN) {
            // Serial responses/events parsing here.
            char *prt3_string = z_receive_string(serial_receiver);

            // log_debug("PMGR: response/event received %s\n", prt3_string);

            if (prt3_string[0] == PRT3_EVENT) {
                para_process_prt3_event(prt3_string, serial_sender, mqtt_area_report, mqtt_zone_report);
            } else {
                para_process_prt3_response(prt3_string, mqtt_area_report, mqtt_zone_report);
            }

            free(prt3_string);
        } else if (items[2].revents & ZMQ_POLLIN) {
            para_process_command(mqtt_area_command, serial_sender);
        }

        if (rc == 0) {
            // Timeout, request areas
            pthread_t t;
            pthread_create(&t, NULL, para_mgr_area_status_thread, serial_sender);
        }
    }

EXIT_PMGR_THREAD:
    if (serial_sender) {
        zmq_close(serial_sender);
    }

    if (kill_subscriber) {
        zmq_close(kill_subscriber);
    }
    
    if (serial_receiver) {
        zmq_close(serial_receiver);
    }

    if (mqtt_area_command) {
        zmq_close(mqtt_area_command);
    }

    if (mqtt_area_report) {
        zmq_close(mqtt_area_report);
    }

    if (mqtt_zone_report) {
        zmq_close(mqtt_zone_report);
    }
    
    return NULL;
}

/**
 * Request status and labels from all areas and zones.
 * Needs to be in a separate thread for the main thread
 * to absorb and parse results.
 * 
 * Also needs delays between requests for serial port
 * to send request and read response in a timely manner.
 */
void *para_mgr_initial_request_thread(void *serial_sender)
{
    log_info("PMGR: Initial request started...\n");

    struct timespec tv = {
        .tv_sec = 0,
        .tv_nsec = 100000000,
    };

    // Give time for the main thread to go into loop
    nanosleep(&tv, NULL);
    // tv.tv_nsec = 20000000;

    for (int i = 0; i < MAX_AREAS; i++) {
        if (areas[i]) {
            para_request_area_label(serial_sender, areas[i]->num);
            // nanosleep(&tv, NULL);
            para_request_area_status(serial_sender, areas[i]->num);
            // nanosleep(&tv, NULL);
        }
    }

    for (int i = 0; i < MAX_ZONES; i++) {
        if (zones[i]) {
            para_request_zone_label(serial_sender, zones[i]->num);
            // nanosleep(&tv, NULL);
            para_request_zone_status(serial_sender, zones[i]->num);
            // nanosleep(&tv, NULL);
        }
    }

    log_info("PMGR: Initial request done.\n");
    return NULL;
}

static void *para_mgr_area_status_thread(void *serial_sender)
{
    log_debug("PMGR: periodic area status update\n");

    for (int i = 0; i < MAX_AREAS; i++) {
        if (areas[i]) {
            para_request_area_status(serial_sender, areas[i]->num);
        }
    }

    return NULL;
}

static void para_send_request(void *serial_sender, char *request, int size)
{
    zmq_msg_t message;
    zmq_msg_init_size(&message, size);

    memcpy(zmq_msg_data(&message), request, size);

    zmq_msg_send (&message, serial_sender, 0);
    zmq_msg_close (&message);

    // Block before next request (if sent sequentially).
    struct timespec tv = {
        .tv_sec = 0,
        .tv_nsec = 20000000,
    };

    nanosleep(&tv, NULL);
}

static void para_request_area_status(void* serial_sender, int areanum)
{
    log_debug("PMGR: request status for area %d\n", areanum);
    char req[6];
    sprintf(req, "RA%03d", areanum);

    para_send_request(serial_sender, req, 5);
}

static void para_request_area_label(void* serial_sender, int areanum)
{
    log_debug("PMGR: request label for area %d\n", areanum);
    char req[6];
    sprintf(req, "AL%03d", areanum);

    para_send_request(serial_sender, req, 5);
}

static void para_request_zone_status(void* serial_sender, int zonenum)
{
    log_debug("PMGR: request status for zone %d\n", zonenum);
    char req[6];
    sprintf(req, "RZ%03d", zonenum);

    para_send_request(serial_sender, req, 5);
}

static void para_request_zone_label(void* serial_sender, int zonenum)
{
    log_debug("PMGR: request label for zone %d\n", zonenum);
    char req[6];
    sprintf(req, "ZL%03d", zonenum);

    para_send_request(serial_sender, req, 5);
}

static void para_area_arm(void *serial_sender, int areanum, char arm_type, char *user_code)
{
    log_debug("PMGR: arm area %d to %c\n", areanum, arm_type);
    char req[13];
    snprintf(req, 13, "AA%03d%c%s", areanum, arm_type, user_code);

    para_send_request(serial_sender, req, strlen(req));
}

static void para_area_disarm(void *serial_sender, int areanum, char *user_code)
{
    log_debug("PMGR: disarm area %d\n", areanum);
    char req[12];
    snprintf(req, 12, "AD%03d%s", areanum, user_code);

    para_send_request(serial_sender, req, strlen(req));
}

static void para_area_quick_arm(void *serial_sender, int areanum, char arm_type)
{
    log_debug("PMGR: quick arm area %d to %c\n", areanum, arm_type);
    char req[7];
    sprintf(req, "AQ%03d%c", areanum, arm_type);

    para_send_request(serial_sender, req, 6);
}

static void para_utility_key(void *serial_sender, int utility_key)
{
    log_debug("PMGR: utility key: %d\n", utility_key);
    char req[6];
    sprintf(req, "UK%03d", utility_key);

    para_send_request(serial_sender, req, 5);
}

static void para_process_prt3_event(char *prt3_string, void* serial_sender, void *mqtt_area_report, void *mqtt_zone_report)
{
    int event_group = get_number_at_substring(prt3_string + 1, 3);
    int event_num = get_number_at_substring(prt3_string + 5, 3);
    int area_num = get_number_at_substring(prt3_string + 9, 3);

    switch (event_group) {
        case G_ZONE_OK:
            log_verbose("PMGR-G: zone %d on area %d OK/CLOSED\n", event_num, area_num);
            zone_set_status(event_num, RS_ZONE_CLOSED);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
        break;

        case G_ZONE_OPEN:
            log_verbose("PMGR-G: zone %d on area %d OPEN\n", event_num, area_num);
            zone_set_status(event_num, RS_ZONE_OPEN);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
        break;

        case G_ZONE_TAMPERED:
            log_verbose("PMGR-G: zone %d on area %d TAMPERED\n", event_num, area_num);
            zone_set_status(event_num, RS_ZONE_TAMPERED);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
        break;

        case G_ZONE_FIRE_LOOP:
            log_verbose("PMGR-G: zone %d on area %d FIRE_LOOP\n", event_num, area_num);
            zone_set_status(event_num, RS_ZONE_FIRE);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
        break;

        case G_ARMING_WITH_MASTER:
        case G_ARMING_WITH_USER_CODE:
        case G_ARMING_WITH_KEYSWITCH:
        case G_SPECIAL_ARMING:
            log_verbose("PMGR-G: area %d armed with event group: %d, event %d\n", area_num, event_group, event_num);
            // para_request_area_status(serial_sender, area_num);
            if (areas[area_num - 1]->status == RS_AREA_DISARMED) {
                if (event_group == G_SPECIAL_ARMING && event_num == 4) {
                    area_set_status(area_num, RS_AREA_STAY_ARMED);
                } else {
                    area_set_status(area_num, RS_AREA_ARMED);
                }
                area_update_mqtt_state(area_num);
                send_area_report(area_num, mqtt_area_report);
            }
        break;

        case G_DISARM_WITH_MASTER:
        case G_DISARM_WITH_USER_CODE:
        case G_DISARM_WITH_KEYSWITCH:
        case G_DISARM_AFTER_ALARM_WITH_MASTER:
        case G_DISARM_AFTER_ALARM_WITH_USER_CODE:
        case G_DISARM_AFTER_ALARM_WITH_KEYSWITCH:
        case G_ALARM_CANCELLED_WITH_MASTER:
        case G_ALARM_CANCELLED_WITH_USER_CODE:
        case G_ALARM_CANCELLED_WITH_KEYSWITCH:
        case G_SPECIAL_DISARM:
            log_verbose("PMGR-G: disarm group %d, event %d, area %d\n", event_group, event_num, area_num);
            // para_request_area_status(serial_sender, area_num);
            area_set_status(area_num, RS_AREA_DISARMED);
            area_update_mqtt_state(area_num);
            send_area_report(area_num, mqtt_area_report);
        break;


        case G_ZONE_BYPASSED:
            log_verbose("PMGR-G: zone %d on area %d BYPASSED\n", event_num, area_num);
        break;

        case G_ZONE_IN_ALARM:
            log_verbose("PMGR-G: zone %d on area %d ALARM\n", event_num, area_num);
            zone_set_alarm(event_num, RS_ZONE_IN_ALARM);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
            // para_request_area_status(serial_sender, area_num);
            area_set_alarm(area_num, RS_AREA_IN_ALARM);
            area_update_mqtt_state(area_num);
            send_area_report(area_num, mqtt_area_report);
        break;

        case G_ZONE_FIRE_ALARM:
            log_verbose("PMGR-G: zone %d on area %d FIRE_ALARM\n", event_num, area_num);
            zone_set_fire(event_num, RS_ZONE_FIRE);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
            // para_request_area_status(serial_sender, area_num);
            area_set_alarm(area_num, RS_AREA_IN_ALARM);
            area_update_mqtt_state(area_num);
            send_area_report(area_num, mqtt_area_report);
        break;

        case G_ZONE_ALARM_RESTORE:
            log_verbose("PMGR-G: zone %d on area %d ALARM_RESTORE\n", event_num, area_num);
            zone_set_alarm(event_num, RS_OK);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
            // para_request_area_status(serial_sender, area_num);
        break;

        case G_ZONE_FIRE_RESTORE:
            log_verbose("PMGR-G: zone %d on area %d FIRE_RESTORE\n", event_num, area_num);
            zone_set_fire(event_num, RS_OK);
            zone_update_mqtt_state(event_num);
            send_zone_report(event_num, mqtt_zone_report);
            // para_request_area_status(serial_sender, area_num);
        break;

        case G_ZONE_SHUTDOWN:
            log_verbose("PMGR-G: zone %d on area %d SHUTDOWN\n", event_num, area_num);
        break;

        case G_ZONE_TAMPER:
            log_verbose("PMGR-G: zone %d on area %d TAMPER\n", event_num, area_num);
        break;

        case G_ZONE_TAMPER_RESTORE:
            log_verbose("PMGR-G: zone %d on area %d TAMPER_RESTORE\n", event_num, area_num);
        break;

        case G_SPECIAL_TAMPER:
            log_verbose("PMGR-G: zone %d on area %d SPECIAL_TAMPER\n", event_num, area_num);
        break;

        case G_TROUBLE_EVENT:
            log_verbose("PMGR-G: TROUBLE_EVENT %d on area %d\n", event_num, area_num);
        break;

        case G_TROUBLE_RESTORE:
            log_verbose("PMGR-G: TROUBLE_RESTORE %d on area %d\n", event_num, area_num);
        break;

        case G_STATUS_1:
            log_verbose("PMGR-G: STATUS_1 %d on area %d\n", event_num, area_num);
            
            if (area_num > 0 && area_num <= MAX_AREAS) {
                // para_request_area_status(serial_sender, area_num);
                switch(event_num) {
                    case 2:
                        area_set_status(area_num, RS_AREA_STAY_ARMED);
                        area_update_mqtt_state(area_num);
                        send_area_report(area_num, mqtt_area_report);
                    break;

                    case 0:
                    case 1:
                    case 3:
                        area_set_status(area_num, RS_AREA_ARMED);
                        area_update_mqtt_state(area_num);
                        send_area_report(area_num, mqtt_area_report);
                    break;

                    case 4:
                    case 5:
                    case 6:
                    case 7:
                        area_set_alarm(area_num, RS_AREA_IN_ALARM);
                        area_update_mqtt_state(area_num);
                        send_area_report(area_num, mqtt_area_report);
                    break;
                }
            }
        break;

        case G_STATUS_2:
            log_verbose("PMGR-G: STATUS_2 %d on area %d\n", event_num, area_num);
            
            if (area_num > 0 && area_num <= MAX_AREAS) {
                // para_request_area_status(serial_sender, area_num);
                switch(event_num) {
                    case 3:
                        area_set_trouble(area_num, RS_AREA_TROUBLE);
                        area_update_mqtt_state(area_num);
                        send_area_report(area_num, mqtt_area_report);
                    break;

                    case 4:
                        area_set_memory(area_num, RS_AREA_ZONE_IN_MEMORY);
                        area_update_mqtt_state(area_num);
                        send_area_report(area_num, mqtt_area_report);
                    break;
                }
            }
        break;

        case G_STATUS_3:
            log_verbose("PMGR-G: STATUS_3 %d on area %d\n", event_num, area_num);
        break;

        default:
            log_verbose("PMGR: event group %d/%d/%d not supported yet!\n", event_group, event_num, area_num);
        break;
    }
}

static void para_process_prt3_response(char *prt3_string, void *mqtt_area_report, void *mqtt_zone_report)
{
    switch (prt3_string[0]) {
        case PRT3_AREA:
            if (prt3_string[1] == PRT3_LABEL) {
                int area_num = get_number_at_substring(prt3_string + 2, 3);

                if (area_num > 0 && area_num < MAX_AREAS && areas[area_num - 1] != NULL) {
                    set_label(areas[area_num - 1]->name, prt3_string + 5);
                    log_debug("PMGR-AL: area label set: [%s]\n", areas[area_num - 1]->name);
                } else {
                    log_error("PMGR-AL: ignoring label of area %d\n", area_num);
                }
            } else if (prt3_string[1] == PRT3_DISARM) {
                int area_num = get_number_at_substring(prt3_string + 2, 3);

                if (area_num > 0 && area_num < MAX_AREAS && areas[area_num - 1] != NULL) {
                    if (prt3_string[6] == 'o' && prt3_string[7] == 'k') {
                        log_debug("PMGR-AD: area %d disarmed\n", area_num);
                        
                        area_set_status(area_num, RS_AREA_DISARMED);
                        area_update_mqtt_state(area_num);
                        send_area_report(area_num, mqtt_area_report);
                    }
                }
            } else {
                log_error("PMGR: area response %c not supported\n", prt3_string[1]);
            }
        break;

        case PRT3_REQ_RESP:
            switch (prt3_string[1]) {
                case PRT3_AREA: {
                    int area_num = get_number_at_substring(prt3_string + 2, 3);
                    
                    if (area_num > 0 && area_num < MAX_AREAS && areas[area_num - 1] != NULL) {
                        update_area_record(area_num, prt3_string);

                        log_debug("PMGR-RA: area %d updated\n", area_num);

                        send_area_report(area_num, mqtt_area_report);
                    } else {
                        log_error("PMGR: ignoring response of area %d\n", area_num);
                    }
                }
                break;

                case PRT3_ZONE: {
                    int zone_num = get_number_at_substring(prt3_string + 2, 3);
                    
                    if (zone_num > 0 && zone_num < MAX_ZONES && zones[zone_num - 1] != NULL) {
                        update_zone_record(zone_num, prt3_string);

                        log_debug("PMGR-RZ: zone %d updated\n", zone_num);

                        send_zone_report(zone_num, mqtt_zone_report);
                        send_area_report(zones[zone_num - 1]->area, mqtt_area_report);
                    } else {
                        log_error("PMGR: ignoring response of zone %d\n", zone_num);
                    }
                }
                break;
            }
        break;

        case PRT3_ZONE:
            if (prt3_string[1] == PRT3_LABEL) {
                int zone_num = get_number_at_substring(prt3_string + 2, 3);

                if (zone_num > 0 && zone_num < MAX_ZONES && zones[zone_num - 1] != NULL) {
                    set_label(zones[zone_num - 1]->name, prt3_string + 5);
                    log_debug("PMGR-ZL: zone label set: [%s]\n", zones[zone_num - 1]->name);
                } else {
                    log_error("PMGR: ignoring label of zone %d\n", zone_num);
                }
            } else {
                log_error("PMGR: area response %c not supported\n", prt3_string[1]);
            }
        break;

        default:
            log_error("PMGR: response type not supported: %s\n", prt3_string);
        break;
    }
}

static void para_process_command(void *mqtt_area_command, void *serial_sender)
{
    para_arm_cmd_t *cmd = (para_arm_cmd_t*) z_receive(mqtt_area_command);

    if (!cmd) {
        log_error("PMGR: command not received!\n");
        return;
    }

    if (cmd->type == CMD_AREA_CONTROL) {
        if (cmd->num > 0 && cmd->num <= MAX_AREAS && areas[cmd->num - 1] != NULL) {
            switch (cmd->command) {
                case AC_ARM_AWAY:
                    if (config.user_code == NULL) {
                        para_area_quick_arm(serial_sender, cmd->num, RS_AREA_ARMED);
                    } else {
                        para_area_arm(serial_sender, cmd->num, RS_AREA_ARMED, config.user_code);
                    }
                break;

                case AC_ARM_HOME:
                    // !!! For some reason Stay Arm does not work with user code on PRT3!
                    /*if (config.user_code == NULL) { //*/
                        para_area_quick_arm(serial_sender, cmd->num, RS_AREA_STAY_ARMED);
                    /*} else {
                        para_area_arm(serial_sender, cmd->num, RS_AREA_STAY_ARMED, config.user_code);
                    } //*/
                break;

                case AC_DISARM:
                    if (config.user_code != NULL) {
                        para_area_disarm(serial_sender, cmd->num, config.user_code);
                    } else {
                        log_info("PMGR: DISARM cannot be performed without user code!\n");
                    }
                break;

                default:
                    log_error("PMGR: wrong area command: %d\n", cmd->command);
                break;
            }
        } else {
            log_error("PMGR: incoming command's area %d is not valid.\n", cmd->num);
        }
    } else if (cmd->type == CMD_UTILITY_KEY && cmd->num > 0 && cmd->num <= MAX_UTILITY_KEY) {
        para_utility_key(serial_sender, cmd->num);
    }

    free(cmd);
}

static void update_area_record(int area_num, char *prt3_string)
{
    area_set_status(area_num, prt3_string[RA_STATUS]);
    area_set_memory(area_num, prt3_string[RA_MEMORY]);
    area_set_trouble(area_num, prt3_string[RA_TROUBLE]);
    area_set_ready(area_num, prt3_string[RA_READY]);
    area_set_programming(area_num, prt3_string[RA_PROGRAMMING]);
    area_set_alarm(area_num, prt3_string[RA_ALARM]);
    area_set_strobe(area_num, prt3_string[RA_STROBE]);
    area_update_mqtt_state(area_num);
}

static void update_zone_record(int zone_num, char *prt3_string)
{
    zone_set_status(zone_num, prt3_string[RZ_STATUS]);
    zone_set_alarm(zone_num, prt3_string[RZ_ALARM]);
    zone_set_fire(zone_num, prt3_string[RZ_FIRE]);
    zone_set_supervision(zone_num, prt3_string[RZ_SUPERVISION]);
    zone_set_battery(zone_num, prt3_string[RZ_LOW_BAT]);
    zone_update_mqtt_state(zone_num);
    zone_update_area_alarm(zone_num);
}

static void send_area_report(int area_num, void *mqtt_area_report)
{
    para_area_t *area = areas[area_num - 1];

    if (area->updated == RECORD_CLEAR) {
        // Nothing to do
        return;
    }

    log_debug("PMGR: sending area report to MQTT\n");

    // Send to MQTT endpoint
    zmq_msg_t message;

    zmq_msg_init_size (&message, sizeof(para_area_t));
    memcpy(zmq_msg_data(&message), area, sizeof(para_area_t));
    zmq_msg_send (&message, mqtt_area_report, 0);
    zmq_msg_close (&message);

    // Clear the record
    area->updated = RECORD_CLEAR;
    area->firstreport = 0;
}

static void send_zone_report(int zone_num, void *mqtt_zone_report)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->updated == RECORD_CLEAR) {
        // Nothing to do
        return;
    }

    // TODO: send to MQTT endpoint
    zmq_msg_t message;

    zmq_msg_init_size (&message, sizeof(para_zone_t));
    memcpy(zmq_msg_data(&message), zone, sizeof(para_zone_t));
    zmq_msg_send (&message, mqtt_zone_report, 0);
    zmq_msg_close (&message);
    
    // Clear the record
    zone->updated = RECORD_CLEAR;
}

static int get_number_at_substring(char *str, size_t length)
{
    char *buff = malloc(length + 1);

    strncpy(buff, str, length);
    buff[length] = 0;
    int ret = strtol(buff, NULL, 10);

    free(buff);
    return ret;
}

static void set_label(char *dst, const char *src) {
    strncpy(dst, src, LABEL_LENGTH);
    dst[LABEL_LENGTH] = 0;

    int end = LABEL_LENGTH - 1;

    while (dst[end] == PARA_SERIAL_SPACE || dst[end] == 0) {
        dst[end--] = 0;
    }
}

static void area_set_status(int area_num, char status) {
    para_area_t *area = areas[area_num - 1];

    if (area->status != status) {
        area->status = status;
        area->updated = RECORD_UPDATED;
    }
}

static void area_set_memory(int area_num, char memory)
{
    para_area_t *area = areas[area_num - 1];
    
    if (area->memory != memory) {
        area->memory = memory;
        area->updated = RECORD_UPDATED;
    }
}

static void area_set_trouble(int area_num, char trouble)
{
    para_area_t *area = areas[area_num - 1];
    
    if (area->trouble != trouble) {
        area->trouble = trouble;
        area->updated = RECORD_UPDATED;
    }
}

static void area_set_ready(int area_num, char ready)
{
    para_area_t *area = areas[area_num - 1];
    
    if (area->ready != ready) {
        area->ready = ready;
        area->updated = RECORD_UPDATED;
    }
}

static void area_set_programming(int area_num, char programming)
{
    para_area_t *area = areas[area_num - 1];
    
    if (area->programming != programming) {
        area->programming = programming;
        area->updated = RECORD_UPDATED;
    }
}

static void area_set_alarm(int area_num, char alarm)
{
    para_area_t *area = areas[area_num - 1];
    
    if (area->alarm != alarm) {
        area->alarm = alarm;
        area->updated = RECORD_UPDATED;
    }
}

static void area_set_strobe(int area_num, char strobe)
{
    para_area_t *area = areas[area_num - 1];
    
    if (area->strobe != strobe) {
        area->strobe = strobe;
        area->updated = RECORD_UPDATED;
    }
}

static void area_update_mqtt_state(int area_num)
{
    para_area_t *area = areas[area_num - 1];

    if (area->alarm != RS_AREA_IN_ALARM) {
        switch (area->status) {
            case RS_AREA_DISARMED:
                area->mqtt_state = MQP_DISARMED;
            break;

            case RS_AREA_STAY_ARMED:
                // TODO: check bypassed zones to set to MQP_ARMED_CUSTOM_BYPASS
                area->mqtt_state = MQP_ARMED_HOME;
            break;
            
            case RS_AREA_ARMED:
            case RS_AREA_FORCE_ARMED:
            case RS_AREA_INSTANT_ARMED:
                // TODO: check bypassed zones to set to MQP_ARMED_CUSTOM_BYPASS
                area->mqtt_state = MQP_ARMED_AWAY;
            break;
        }
    } else {
        // Alarm!
        area->mqtt_state = MQP_TRIGGERED;
    }
}

static void zone_set_status(int zone_num, char status)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->status != status) {
        zone->status = status;
        zone->updated = RECORD_UPDATED;
    }
}

static void zone_set_alarm(int zone_num, char alarm)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->alarm != alarm) {
        zone->alarm = alarm;
        zone->updated = RECORD_UPDATED;
    }
}

static void zone_set_fire(int zone_num, char fire)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->fire != fire) {
        zone->fire = fire;
        zone->updated = RECORD_UPDATED;
    }
}

static void zone_set_supervision(int zone_num, char supervision)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->supervision != supervision) {
        zone->supervision = supervision;
        zone->updated = RECORD_UPDATED;
    }
}

static void zone_set_battery(int zone_num, char battery)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->battery != battery) {
        zone->battery = battery;
        zone->updated = RECORD_UPDATED;
    }
}

static void zone_set_bypassed(int zone_num, char bypassed)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->bypassed != bypassed) {
        zone->bypassed = bypassed;
        zone->updated = RECORD_UPDATED;
    }
}

static void zone_update_mqtt_state(int zone_num)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (
        zone->status == RS_ZONE_CLOSED
        && zone->alarm == RS_OK
        && zone->fire == RS_OK
        )
    {
        zone->mqtt_state = MQZ_OFF;
    } else {
        zone->mqtt_state = MQZ_ON;
    }
}

static void zone_update_area_alarm(int zone_num)
{
    para_zone_t *zone = zones[zone_num - 1];

    if (zone->alarm == RS_ZONE_IN_ALARM && areas[zone->area - 1]->alarm == RS_OK) {
        // Update area to alarm, as zone is in alarm
        areas[zone->area - 1]->alarm = RS_AREA_IN_ALARM;
        areas[zone->area - 1]->updated = RECORD_UPDATED;
    }
}
