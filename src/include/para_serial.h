/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * para_serial.h
 *
 *  Created on: Jan 5, 2022
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
#ifndef PARA_SERIAL_H
#define PARA_SERIAL_H

#include <termios.h>
#include <pthread.h>

#define PARA_SERIAL_SPEED B57600 // Default value
#define PARA_SERIAL_BUFF_LEN 32 // Should be actually enough of 22, but just in case
#define PARA_SERIAL_EOL 0x0D

pthread_t start_para_serial(char*, void *);

#endif /* PARA_SERIAL_H */