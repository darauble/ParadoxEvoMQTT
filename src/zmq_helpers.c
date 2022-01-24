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