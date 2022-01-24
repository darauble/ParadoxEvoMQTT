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
