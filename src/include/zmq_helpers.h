/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * zmq_helpers.h
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
#ifndef PARA_ZMQ_HELPERS_H
#define PARA_ZMQ_HELPERS_H

#include <zmq.h>

int z_start_endpoint(void *context, void **socket, const int type, const char *endpoint);

int z_connect_endpoint(void *context, void **socket, const int type, const char *endpoint);

void z_drop_message(void *socket);

char *z_receive_string(void *socket);

void *z_receive(void *socket);

#endif /* PARA_ZMQ_HELPERS_H */