/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * zmq_helpers.c
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
#include <stdlib.h>
#include <string.h>
#include "zmq_helpers.h"

int z_start_endpoint(void *context, void **socket, const int type, const char *endpoint)
{
    *socket = zmq_socket(context, type);
    return zmq_bind(*socket, endpoint);
}


int z_connect_endpoint(void *context, void **socket, const int type, const char *endpoint)
{
    *socket = zmq_socket(context, type);
    return zmq_connect(*socket, endpoint);
}

void z_drop_message(void *socket)
{
    zmq_msg_t message;

    zmq_msg_init (&message);
    zmq_msg_recv (&message, socket, 0);
    zmq_msg_close (&message);
}

char *z_receive_string(void *socket)
{
    zmq_msg_t message;
    char *ret = NULL;

    zmq_msg_init (&message);
    int size = zmq_msg_recv (&message, socket, 0);
    
    if (size > 0) {
        ret = malloc(size + 1);
        memcpy (ret, zmq_msg_data(&message), size);
        ret[size] = 0;
    }

    zmq_msg_close (&message);

    return ret;
}

void *z_receive(void *socket)
{
    zmq_msg_t message;
    void *ret = NULL;

    zmq_msg_init (&message);
    int size = zmq_msg_recv (&message, socket, 0);
    
    if (size > 0) {
        ret = malloc(size);
        memcpy (ret, zmq_msg_data(&message), size);
    }

    zmq_msg_close (&message);

    return ret;
}