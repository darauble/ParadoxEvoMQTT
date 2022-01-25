/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * paratypes.h
 *
 *  Created on: Jan 4, 2022
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
#ifndef PARA_TYPES_H
#define PARA_TYPES_H

#include <stdint.h>

#define MAX_UTILITY_KEY 251

#define LABEL_LENGTH 17
#define RECORD_CLEAR 0
#define RECORD_UPDATED 1

#define HA_ARM_AWAY "ARM_AWAY"
#define HA_ARM_HOME "ARM_HOME"
#define HA_DISARM "DISARM"

typedef enum {
    MQP_DISARMED = 0,
    MQP_ARMED_HOME,
    MQP_ARMED_AWAY,
    MQP_ARMED_NIGHT,
    MQP_ARMED_VACATION,
    MQP_ARMED_CUSTOM_BYPASS,
    MQP_PENDING,
    MQP_TRIGGERED,
    MQP_ARMING,
    MQP_DISARMING,
} mqtt_panel_state_t;

typedef enum {
    MQZ_OFF = 0,
    MQZ_ON,
} mqtt_zone_state_t;

typedef struct {
    int num; // 1-based Area in the panel
    char name[LABEL_LENGTH];
    char status;
    mqtt_panel_state_t mqtt_state; // Pre-calculated state for MQTT HA compatibility
    char memory;
    char trouble;
    char ready;
    char programming;
    char alarm;
    char strobe;
    int firstreport; // On first report a MQTT control subscription is performed (as otherwise area number is not known)
    int updated;
} para_area_t;

typedef struct {
    int num;
    int area;
    char name[LABEL_LENGTH];
    mqtt_zone_state_t mqtt_state;
    char status;
    char alarm;
    char fire;
    char supervision;
    char battery;
    char bypassed;
    int updated;
} para_zone_t;

typedef enum {
    PRT3_EVENT = 'G',
    PRT3_REQ_RESP = 'R',
    PRT3_AREA = 'A',
    PRT3_ZONE = 'Z',
    //PRT3_USER = 'U', // TBD
    PRT3_LABEL = 'L',
} para_prt3_input_t;

typedef enum {
    RA_STATUS = 5,
    RA_MEMORY,
    RA_TROUBLE,
    RA_READY,
    RA_PROGRAMMING,
    RA_ALARM,
    RA_STROBE,
} para_ra_status_bytes_t;

typedef enum {
    RS_OK = 'O',
    RS_AREA_DISARMED = 'D',
    RS_AREA_ARMED = 'A',
    RS_AREA_FORCE_ARMED = 'F',
    RS_AREA_STAY_ARMED = 'S',
    RS_AREA_INSTANT_ARMED = 'I',
    RS_AREA_ZONE_IN_MEMORY = 'M',
    RS_AREA_TROUBLE = 'T',
    RS_AREA_NOT_READY = 'N',
    RS_AREA_PROGRAMMING = 'P',
    RS_AREA_IN_ALARM = 'A',
    RS_AREA_STROBE = 'S',
    RS_ZONE_OPEN = 'O',
    RS_ZONE_CLOSED = 'C',
    RS_ZONE_TAMPERED = 'T',
    RS_ZONE_IN_ALARM = 'A',
    RS_ZONE_FIRE = 'F',
    RS_ZONE_SUPERVISION_LOST = 'S',
    RS_ZONE_LOW_BAT = 'L',
    RS_ZONE_BYPASSED = 'B', // NOTE: Not official/present in PRT3, my invention!
} para_states_t;

typedef enum {
    RZ_STATUS = 5,
    RZ_ALARM,
    RZ_FIRE,
    RZ_SUPERVISION,
    RZ_LOW_BAT,
} para_rz_status_bytes_t;

typedef enum {
    G_ZONE_OK = 0,
    G_ZONE_OPEN = 1,
    G_ZONE_TAMPERED = 2,
    G_ZONE_FIRE_LOOP = 3,

    G_ARMING_WITH_MASTER = 9,
    G_ARMING_WITH_USER_CODE = 10,
    G_ARMING_WITH_KEYSWITCH = 11,
    G_SPECIAL_ARMING = 12,

    G_DISARM_WITH_MASTER = 13,
    G_DISARM_WITH_USER_CODE = 14,
    G_DISARM_WITH_KEYSWITCH = 15,
    G_DISARM_AFTER_ALARM_WITH_MASTER = 16,
    G_DISARM_AFTER_ALARM_WITH_USER_CODE = 17,
    G_DISARM_AFTER_ALARM_WITH_KEYSWITCH = 18,
    G_ALARM_CANCELLED_WITH_MASTER = 19,
    G_ALARM_CANCELLED_WITH_USER_CODE = 20,
    G_ALARM_CANCELLED_WITH_KEYSWITCH = 21,
    G_SPECIAL_DISARM = 22,

    G_ZONE_BYPASSED = 23,
    G_ZONE_IN_ALARM = 24,
    G_ZONE_FIRE_ALARM = 25,
    G_ZONE_ALARM_RESTORE = 26,
    G_ZONE_FIRE_RESTORE = 27,

    G_ZONE_SHUTDOWN = 32,
    G_ZONE_TAMPER = 33,
    G_ZONE_TAMPER_RESTORE = 34,
    G_SPECIAL_TAMPER = 35,
    G_TROUBLE_EVENT = 36,
    G_TROUBLE_RESTORE = 37,

    G_STATUS_1 = 64,
    G_STATUS_2 = 65,
    G_STATUS_3 = 66,
} para_event_group_t;

typedef enum {
    T_TLM_TROUBLE = 0,
    T_AC_FAILURE,
    T_BATTERY_FAILURE,
    T_AUXILIARY_CURRENT_LIMIT,
    T_BELL_CURRENT_LIMIT,
    T_BELL_ABSENT,
    T_CLOCK_TROUBLE,
    T_GLOBAL_FIRE_LOOP,
} para_trouble_t;

typedef enum {
    S1_ARMED = 0,
    S1_FORCE_ARMED,
    S1_STAY_ARMED,
    S1_INSTANT_ARMED,
    S1_STROBE_ALARM,
    S1_SILENT_ALARM,
    S1_AUDIBLE_ALARM,
    S1_FIRE_ALARM,
} para_status_1_t;

typedef enum {
    S2_READY = 0,
    S2_EXIT_DELAY,
    S2_ENTRY_DELAY,
    S2_SYSTEM_IN_TROUBLE,
    S2_ALARM_IN_MEMORY,
    S2_ZONES_BYPASSED,
    S2_BYPASS_MASTER_INSTALLER_PROGRAMMING,
    S2_KEYPAD_LOCKOUT,
} para_status_2_t;

typedef enum {
    CMD_AREA_CONTROL = 0,
    CMD_UTILITY_KEY,
    CMD_VIRTUAL_INTPUT,
    CMD_VIRTUAL_PGM,
} para_command_type_t;

typedef enum {
    AC_ARM_AWAY = 0,
    AC_ARM_HOME,
    AC_DISARM,
} para_arm_cmd_e_t;

typedef struct {
    para_command_type_t type;
    int num;
    para_arm_cmd_e_t command;
} para_arm_cmd_t;

#endif /* PARA_TYPES_H */
