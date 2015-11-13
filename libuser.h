/*
 * This file contains the function definitions for the library interfaces
 * to the USLOSS system calls.
 */
#ifndef _LIBUSER_H
#define _LIBUSER_H

// Phase 3 -- User Function Prototypes
extern int  Spawn(char *name, int (*func)(char *), char *arg, int stack_size,
                  int priority, int *pid);
extern int  Wait(int *pid, int *status);
extern void Terminate(int status);
extern void GetTimeofDay(int *tod);
extern void CPUTime(int *cpu);
extern void GetPID(int *pid);
extern int  SemCreate(int value, int *semaphore);
extern int  SemP(int semaphore);
extern int  SemV(int semaphore);
extern int  SemFree(int semaphore);

extern  int  Sleep(int seconds);
extern  int  DiskRead (void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern  int  DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern  int  DiskSize (int unit, int *sector, int *track, int *disk);
extern  int  TermRead (char *buffer, int bufferSize, int unitID, int *numCharsRead);
extern  int  TermWrite(char *buffer, int bufferSize, int unitID, int *numCharsRead);
#endif
