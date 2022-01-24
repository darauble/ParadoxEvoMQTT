#ifndef PARA_SERIAL_H
#define PARA_SERIAL_H

#include <termios.h>
#include <pthread.h>

#define PARA_SERIAL_SPEED B57600 // Default value
#define PARA_SERIAL_BUFF_LEN 32 // Should be actually enough of 22, but just in case
#define PARA_SERIAL_EOL 0x0D

pthread_t start_para_serial(char*, void *);

#endif /* PARA_SERIAL_H */