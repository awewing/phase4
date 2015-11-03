#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <provided_prototypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
/**************************************************************************************************
 * phase4.c
 *
 * Authors: Alex Ewing
 *          Andre Takagi
 *
 * Date:    11/2/2015
 *************************************************************************************************/

/************************************************
 * Prototypes
 ***********************************************/
static int      ClockDriver(char *);
static int      DiskDriver(char *);

static void sleep(systemArgs *args);
static void diskRead(systemArgs *args);
static void diskWrite(systemArgs *args);
static void diskSize(systemArgs *args);
static void termRead(systemArgs *args);
static void termWrite(systemArgs *args);

int sleepReal(int seconds);
int diskReadReal(int unit, int track, int first, int sectors, void *buffer);
int diskWriteReal(int unit, int track, int first, int sectors, void *buffer);
int diskSizeReal(int unit, int *sector, int *track, int *disk);
int termReadReal(int unit, int size, char *buffer);
int termWriteReal(int unit, int size, char *text);
/***********************************************/

/************************************************
 * Globals
 ***********************************************/
 int running; // the semaphore thats running
 process ProcTable[MAXPROC];
 procPtr waitQ;
/***********************************************/

void start3(void) {
    char	name[128];
    char        termbuf[10];
    int		clockPID;
    int		pid;
    int		status;
    /*
     * Check kernel mode here.
     */

    // init sysvec
    systemCallVec[SYS_SLEEP] = sleep;
    systemCallVec[SYS_DISKREAD] = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE] = diskSize;
    systemCallVec[SYS_TERMREAD] = termRead;
    systemCallVec[SYS_TERMWRITE] = termWrite;

    // initialize procTable
    for (int i = 0; i < MAXPROC; i++) {
        ProcTable[i].wakeUpTime = -1;
        ProcTable[i].nextWakeUp = NULL;
    }

    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    running = semcreateReal(0);
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
	    USLOSS_Console("start3(): Can't create clock driver\n");
	    USLOSS_Halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "running" once it is running.
     */

    sempReal(running);

    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    char buf[DISK_UNITS];
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        sprintf(buf, "%d", i);
        pid = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }
    }
    sempReal(running);
    sempReal(running);

    /*
     * Create terminal device drivers.
     */


    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters.
     */
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    pid = waitReal(&status);

    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver

    // eventually, at the end:
    quit(0);
    
}

static int ClockDriver(char *arg) {
    int result;
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(!isZapped()) {
	    result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
	    if (result != 0) {
	        return 0;
	    }
	    /*
	     * Compute the current time and wake up any processes
	     * whose time has come.
	     */
         
    }

    return 0;
}

static int DiskDriver(char *arg) {
    int unit = atoi( (char *) arg); 	// Unit is passed as arg.
    return 0;
}

static void sleep(systemArgs *args) {}
static void diskRead(systemArgs *args) {}
static void diskWrite(systemArgs *args) {}
static void diskSize(systemArgs *args) {}
static void termRead(systemArgs *args) {}
static void termWrite(systemArgs *args) {}

int sleepReal(int seconds) {
    if (seconds < 0) {
        return 1;
    }

    int pid = getpid();
    long wakeTime = USLOSS_Clock() + (seconds * 1000000);
    ProcTable[pid].wakeUpTime = wakeTime;

    // Insert this proc into the queue of procs to be woken up by clock driver
    if (waitQ == NULL) {
        waitQ = &(ProcTable[pid]);
    }
    else {
        procPtr curr = waitQ;
        while (curr->nextWakeUp != NULL && curr->wakeUpTime < wakeTime) {
            curr = curr->nextWakeUp;
        }

        curr->nextWakeUp = &(ProcTable[pid]);
    }
    // switch to user mode

    return 0;
}

int diskReadReal(int unit, int track, int first, int sectors, void *buffer) {
    return 0;
}

int diskWriteReal(int unit, int track, int first, int sectors, void *buffer) {
    return 0;
}

int diskSizeReal(int unit, int *sector, int *track, int *disk) {
    return 0;
}

int termReadReal(int unit, int size, char *buffer) {
    return 0;
}

int termWriteReal(int unit, int size, char *text) {
    return 0;
}
