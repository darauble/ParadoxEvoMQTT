/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * log.h
 *
 *  Created on: Jan 13, 2022
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
#ifndef PARA_LOG_H
#define PARA_LOG_H

#include <stdio.h>
#include <time.h>

#include "config.h"

extern para_evo_config_t config;

#define log_error(s, ...) { \
    time_t t = time(NULL); \
    struct tm *d = localtime(&t); \
    fprintf(stderr, "[%d-%02d-%02d %02d:%02d:%02d ERROR] " s, d->tm_year + 1900, d->tm_mon + 1, d->tm_mday, d->tm_hour, d->tm_min, d->tm_sec, ##__VA_ARGS__); \
}

#define log_info(s, ...) { \
    time_t t = time(NULL); \
    struct tm *d = localtime(&t); \
    printf("[%d-%02d-%02d %02d:%02d:%02d INFO] " s, d->tm_year + 1900, d->tm_mon + 1, d->tm_mday, d->tm_hour, d->tm_min, d->tm_sec, ##__VA_ARGS__); \
}

#define log_verbose(s, ...) if (config.verbose) { \
    time_t t = time(NULL); \
    struct tm *d = localtime(&t); \
    printf("[%d-%02d-%02d %02d:%02d:%02d VERB] " s, d->tm_year + 1900, d->tm_mon + 1, d->tm_mday, d->tm_hour, d->tm_min, d->tm_sec, ##__VA_ARGS__); \
}

#ifdef DEBUG

#define log_debug(s, ...) if (config.verbose) { \
    time_t t = time(NULL); \
    struct tm *d = localtime(&t); \
    printf("[%d-%02d-%02d %02d:%02d:%02d DBG] " s, d->tm_year + 1900, d->tm_mon + 1, d->tm_mday, d->tm_hour, d->tm_min, d->tm_sec, ##__VA_ARGS__); \
}

#else
// No debug
#define log_debug(s, ...)
#endif /* DEBUG */

#endif /* PARA_LOG_H */