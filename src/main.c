/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * main.c
 *
 *  Created on: Oct 24, 2021
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
 * 
 * HISTORY:
 * v0.1: Passive area and zones status reporting. A bug: transition 
 *       from armed/stayarmed to disarmed not always reported, as
 *       event 22 and immediate area status request still showed
 *       previous state.
 * v0.2: Fixed reporting bug by also adding area status request on
 *       events 64 and 65 (a bit excess, but reliable).
 *       Added ARM_AWAY and ARM_HOME commands support via MQTT (No DISARM yet!)
 * v0.3: Adding utility key support, so these can be used as switches.
 * v0.4: Adding control user code support, enabling Disarm.
 * v0.5: Ditch many Area Status requests and get info from events.
 *       Add periodic area update (default 60 s).
 * v0.6: Add MQTT login/password and retain flag.
 * v0.7: Change reading to buffered, as reading by one byte causes
 *       excessive overuse of system call.
 *       Add "arming" (or Exit Delay) status handling.
 * v0.8: Fixed a bug preventing of correct reporting of Armed state after Exit Delay.
 * v0.9: Added a safety switch, when unexpected G000N000A000 crashed the daemon.
 */
#define V_MAJOR 0
#define V_MINOR 10

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <zmq.h>
#include <unistd.h>

#include "log.h"
#include "config.h"
#include "mqtt_mgr.h"
#include "para_mgr.h"
#include "para_serial.h"
#include "endpoints.h"

para_evo_config_t config = {
    .verbose = 0,
    .mqtt_server = NULL,
    .mqtt_port = 1883,
    .mqtt_topic = "darauble/paraevo",
    .mqtt_client_id = "paraevo_daemon",
    .mqtt_login = NULL,
    .mqtt_password = NULL,
    .mqtt_retain = 0,
    .user_code = NULL,
    .area_status_period = 60,
};

void *killpublisher = NULL;

/* Function headers */
void print_usage();
static int create_daemon();
static void s_catch_signals();

int main(int argc, char **argv)
{
    __label__ EXIT_MAIN;
    int return_main = 0;

    void *context = NULL;

    static int print_version = 0;
    int opt_daemon = 0;
    int opt_help = 0;

    static struct option long_options[] =
    {
        /* These options set a flag. */
        {"version",      no_argument,  &print_version, 1},
        /* These options don’t set a flag.
            We distinguish them by their indices. */
        {"mqtt_server",   required_argument, 0, 'm'},
        {"mqtt_port",     required_argument, 0, 'p'},
        {"mqtt_topic",    required_argument, 0, 't'},
        {"mqtt_login",    required_argument, 0, 'l'},
        {"mqtt_password", required_argument, 0, 'w'},
        {"mqtt_retain",   no_argument,       0, 'r'},
        {"area",          required_argument, 0, 'a'},
        {"zones",         required_argument, 0, 'z'},
        {"daemon",        no_argument,       0, 'D'},
        {"device",        required_argument, 0, 'd'},
        {"user_code",     required_argument, 0, 'u'},
        {"status_period", required_argument, 0, 'S'},
        {"help",          no_argument,       0, 'h'},
        {"verbose",       no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    para_mgr_init();

    int opt_idx = 0;
    int c = 0;
    int areanum = 0;

    int areaset = 0;
    int zonesset = 0;

    char *serialdevice = NULL;

    while(1) {
        c = getopt_long(argc, argv, "m:p:t:l:w:ra:z:Dd:u:S:hv", long_options, &opt_idx);

        if (c < 0) {
            break;
        }

        switch(c) {
            case 'm':
                config.mqtt_server = optarg;
            break;

            case 'p':
                config.mqtt_port = strtol(optarg, NULL, 10);

                if (config.mqtt_port < 1 || config.mqtt_port > 65535) {
                    log_error("PARAEVO: MQTT port %d is not valid\n", config.mqtt_port);
                    return_main = -1;
                    goto EXIT_MAIN;
                }
            break;

            case 't':
                config.mqtt_topic = optarg;
            break;

            case 'l':
                config.mqtt_login = optarg;
            break;

            case 'w':
                config.mqtt_password = optarg;
            break;

            case 'r':
                config.mqtt_retain = 1;
            break;

            case 'a':
                areanum = strtol(optarg, NULL, 10);

                if (areanum <= 0 || areanum > MAX_AREAS) {
                    log_error("PARAEVO: Area number %d is not valid!\n", areanum);
                    return_main = -2;
                    goto EXIT_MAIN;
                }

                if (para_mgr_set_area(areanum) == 0) {
                    areaset = 1;
                }
            break;

            case 'z':
                if (areanum == 0) {
                    log_error("PARAEVO: Area was not set previously, check argument order!\n");
                    return_main = -3;
                    goto EXIT_MAIN;
                }

                char *zone = strtok(optarg, ",");

                while (zone != NULL) {
                    int zoneindex = strtol(zone, NULL, 10);

                    if (zoneindex <= 0 || zoneindex > MAX_ZONES) {
                        log_error("PARAEVO: Zone number %d is not valid!\n", zoneindex);
                        return_main = -4;
                        goto EXIT_MAIN;
                    }

                    if (para_mgr_set_zone(areanum, zoneindex) == 0) {
                        zonesset = 1;
                    }

                    zone = strtok(NULL, ",");
                }

                areanum = 0;
            break;

            case 'd':
                serialdevice = optarg;
            break;

            case 'u':
                config.user_code = optarg;
            break;

            case 'S':
                config.area_status_period = strtol(optarg, NULL, 10);

                if (config.area_status_period < 60) {
                    log_error("PARAEVO: area request period cannot be shorter than 60 seconds!\n")
                    return_main = -9;
                    goto EXIT_MAIN;
                }
            break;

            case 'D':
                opt_daemon = 1;
            break;

            case 'h':
                opt_help = 1;
            break;

            case 'v':
                config.verbose = 1;
            break;
        }
    }

    if (print_version) {
        printf("PARAEVO: Paradox EVO MQTT Daemon v%d.%d\n", V_MAJOR, V_MINOR);
        return_main = 0;
        goto EXIT_MAIN;
    }

    if (opt_help) {
        print_usage();
        return_main = 0;
        goto EXIT_MAIN;
    }

    if (!areaset) {
        log_error("PARAEVO: No area was set! Exiting.\n");
        return_main = -5;
        goto EXIT_MAIN;
    }

    if (!zonesset) {
        fprintf(stderr, "PARAEVO: Not a single zone was set! Exiting.\n");
        return_main = -6;
        goto EXIT_MAIN;
    }

    if (!serialdevice) {
        fprintf(stderr, "PARAEVO: No serial device was set for PRT3! Exiting.\n");
        return_main = -7;
        goto EXIT_MAIN;
    }

    if (!config.mqtt_server) {
        fprintf(stderr, "PARAEVO: No MQTT server was provided!\n");
        return_main = -8;
        goto EXIT_MAIN;
    }

    if (opt_daemon) {
        return_main = create_daemon();
        
        if (return_main < 0) {
            goto EXIT_MAIN;
        }
    }

    log_info("PARAEVO: Starting Paradox EVO daemon v%d.%d...\n", V_MAJOR, V_MINOR);
    context = zmq_ctx_new();

    killpublisher = zmq_socket(context, ZMQ_PUB);
    int killrc = zmq_bind(killpublisher, EPT_KILL);
    
    if (killrc != 0) {
        printf("PARAEVO: Failed to start kill publisher: %d\n", killrc);
        goto EXIT_MAIN;
    }

    // Lamely give time for ZMQ PULL to initialize.
    sleep(1);
   
    s_catch_signals();

    pthread_t pstid = start_para_serial(serialdevice, context);
    pthread_t pmgrtid = para_mgr_start(context);
    pthread_t mmgrid = mqtt_mgr_start(context);

    if (pstid) {
        pthread_join(pstid, NULL);
    } else {
        log_info("PARAEVO: Error creating the SERIAL thread\n");
        goto EXIT_MAIN;
    }
    pthread_join(pmgrtid, NULL);
    pthread_join(mmgrid, NULL);

EXIT_MAIN:
    para_mgr_clean();
    
    if (killpublisher) {
        zmq_close(killpublisher);
    }

    if (context) {
        zmq_ctx_destroy (context);
    }

    log_info("PARAEVO: all done, exitting.\n")
    return return_main;
}

static int create_daemon()
{
    pid_t pid = fork();

    if (pid < 0) {
        return -1;
    }

    if (pid > 0) {
        return -3;
    }

    if (setsid() < 0) {
        return -2;
    }

    return 0;
}

static void s_signal_handler(int signal_value)
{
    log_info("Sending KILL to all subscribers\n");

    zmq_msg_t message;
    zmq_msg_init_size (&message, 0);
    zmq_msg_send (&message, killpublisher, 0);
    zmq_msg_close (&message);
}

static void s_catch_signals()
{
    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
}

void print_usage()
{
    printf(
        "Usage: paraevo -d <USART device> -a <area> -z <zone list> --mqtt_server=<server address> [options]\n"
        "\n"
        "Main options:\n"
        "  -D, --daemon                             Run application in daemon mode.\n"
        "  -d <device>   --device=<device>          Set device of PRT3 module.\n"
        "                                           E.g. paraevo -d /dev/ttyUSB0\n"
        "  -a <area>     --area=<area>              Set an area number to monitor. Several areas\n"
        "                                           can be provided, e.g. -a 1 -a 2\n"
        "  -z <zones>    --zones=<zones>            Assign zones to an area by comma-separated\n"
        "                                           list. This switch should follow appropriate -a\n"
        "                                           switch, so the zones in multi-area panel can\n"
        "                                           be monitored and reported properly.\n"
        "                                           e.g. -a 1 -z 1,3,10 -a 2 -z 4,5,8\n"
        "  -u <code>     --user_code=<code>         A panel's user code (necessary for Disarm function).\n"
        "  -m <server>   --mqtt_server=<server>     Send output to MQTT server.\n"
        "\n"
        "Additional options:\n"
        "  -p <port>     --mqtt_port=<port>         Set MQTT server's port. Default 1883.\n"
        "  -t <topic>    --mqtt_topic=<topic>       Set parent MQTT topic. Default \"darauble/paraevo\".\n"
        "  -l <login>    --mqtt_login=<login>       Set a username/login for MQTT server (if required).\n"
        "  -w <pass>     --mqtt_password=<pass>     Set a password for MQTT server (if required).\n"
        "  -r,           --mqtt_retain              If given, all messages sent by the daemon will be retained.\n"
        "  -S <seconds>  --status_period=<seconds>  An idle timeout when to request Area Status update.\n"
        "                                           Minimum is 60 s (and it's default).\n"
        "\n"
        "Other options:\n"
        "  -v, --verbose                            Print verbose output of daemon's actions.\n"
        "  -h, --help                               Print this usage message and exit.\n"
        "  --version                                Print application's version and exit.\n"
        "\n"
    ); 
}
