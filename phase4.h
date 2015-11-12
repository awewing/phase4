/*
 * Process structures
 */
typedef struct procStruct process;
typedef struct procStruct * procPtr;
typedef struct request request;
typedef struct request * reqPtr;

struct procStruct {
    int     wakeUpTime;
    int     sleepSem;
    int     termSem;
    int     procDiskSem;
    procPtr nextWakeUp;	// points to next process in the queue of processes to be woken up.
};

struct request {
	int track;
	long startSector;
	int numSectors;
	int waitingPID;
	void * buffer;
	int reqType;
	reqPtr nextReq;
};

/*
 * These are the definitions for phase 4 of the project (support level, part 2).
 */

#ifndef _PHASE4_H
#define _PHASE4_H

#define MAXLINE         80
#define MAXLINES        10
/*
 * Function prototypes for this phase.
 */
extern  int  Sleep(int seconds);
extern  int  DiskRead (void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern  int  DiskWrite(void *diskBuffer, int unit, int track, int first, int sectors, int *status);
extern  int  DiskSize (int unit, int *sector, int *track, int *disk);
extern  int  TermRead (char *buffer, int bufferSize, int unitID, int *numCharsRead);
extern  int  TermWrite(char *buffer, int bufferSize, int unitID, int *numCharsRead);

extern  int  start4(char *);

#define ERR_INVALID             -1
#define ERR_OK                  0

#endif /* _PHASE4_H */
