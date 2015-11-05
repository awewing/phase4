/*
 * Process structures
 */
typedef struct procStruct process;
typedef struct procStruct * procPtr;

struct procStruct {
    int     wakeUpTime;
    int     sleepSem;
    procPtr nextWakeUp;	// points to next process in the queue of processes to be woken up.
    int     numTracks; 
};

/*
 * These are the definitions for phase 4 of the project (support level, part 2).
 */

#ifndef _PHASE4_H
#define _PHASE4_H

#define MAXLINE         80
#define DISK_UNITS      10 //TODO what to really initialize as
/*
 * Function prototypes for this phase.
 */
extern  int  Sleep(int seconds);
extern  int  DiskRead (void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern  int  DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern  int  DiskSize (int unit, int *sector, int *track, int *disk);
extern  int  TermRead (char *buffer, int bufferSize, int unitID,int *numCharsRead);
extern  int  TermWrite(char *buffer, int bufferSize, int unitID, int *numCharsRead);

extern  int  start4(char *);

#define ERR_INVALID             -1
#define ERR_OK                  0

#endif /* _PHASE4_H */
