/*
 * The source of the MQTT daemon interacting with Paradox EVO control panel
 * via their's PRT3 module.
 *
 * para_mgr.h
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
#ifndef PARA_MGR_H
#define PARA_MGR_H

// Comment out for EVO48, but only to save some memory
// Otherwise works in the same manner.
#define EVO192

#ifdef EVO192
#define MAX_AREAS 8
#define MAX_ZONES 96
#else
#define MAX_AREAS 4
#define MAX_ZONES 48
#endif /* EVO192 */

#define PARA_SERIAL_SPACE 0x20

#include <pthread.h>

// Call this first!
void para_mgr_init();

int para_mgr_set_area(int);

int para_mgr_set_zone(int, int);

pthread_t para_mgr_start(void*);

void para_mgr_clean();

#endif /* PARA_MGR_H */
