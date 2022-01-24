#ifndef PARA_ZMQ_HELPERS_H
#define PARA_ZMQ_HELPERS_H

#include <zmq.h>

int z_start_endpoint(void *context, void **socket, const int type, const char *endpoint);

int z_connect_endpoint(void *context, void **socket, const int type, const char *endpoint);

void z_drop_message(void *socket);

char *z_receive_string(void *socket);

void *z_receive(void *socket);

#endif /* PARA_ZMQ_HELPERS_H */