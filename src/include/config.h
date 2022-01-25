/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * config.h
 *
 *  Created on: Jan 10, 2022
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
#ifndef PARA_EVO_CONFIG_H
#define PARA_EVO_CONFIG_H

typedef struct {
    int verbose;
    char *mqtt_server;
    int mqtt_port;
    char *mqtt_topic;
    char *mqtt_client_id;
    char *user_code;
} para_evo_config_t;

#endif /* PARA_EVO_CONFIG_H */