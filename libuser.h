/*
 * This file contains the function definitions for the library interfaces
 * to the USLOSS system calls.
 */
#ifndef _LIBUSER_H
#define _LIBUSER_H

// Phase 4 -- User Function Prototypes
extern int Sleep(int seconds);
extern int DiskRead(void *address, int sectors, int startTrack, int startSector, int units);
extern int DiskWrite(void *address, int sectors, int startTrack, int startSector, int units);
#endif
//TODO more later
