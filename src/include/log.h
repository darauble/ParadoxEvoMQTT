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