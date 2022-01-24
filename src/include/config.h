#ifndef PARA_EVO_CONFIG_H
#define PARA_EVO_CONFIG_H

typedef struct {
    int verbose;
    char *mqtt_server;
    int mqtt_port;
    char *mqtt_topic;
    char *mqtt_client_id;
} para_evo_config_t;

#endif /* PARA_EVO_CONFIG_H */