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
 static int      TermDriver(char *);
 static int      TermReader(char *);
 static int      TermWriter(char *);

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

 void setUserMode();
 void check_kernel_mode(char* name);
/***********************************************/

/************************************************
 * Globals
 ***********************************************/
 int debugflag4;

 int running; // the semaphore that waits for drivers to start
 process ProcTable[MAXPROC];
 procPtr waitQ;

 int charReceiveBox[USLOSS_TERM_UNITS]; // mailboxs for receiving a character
 int charSendBox[USLOSS_TERM_UNITS];    // mailboxs for sending a character
 int lineReadBox[USLOSS_TERM_UNITS];    // mailboxs for reading a line
 int lineWriteBox[USLOSS_TERM_UNITS];   // mailboxs for writing a line
 int diskReadBox
 int diskWriteBox
/***********************************************/

void start3(void) {
    char	name[128];
    int		clockPID;
    int		pid;
    int		status;

    /*
     * Check kernel mode here.
     */
    check_kernel_mode("start3");

    // init sysvec
    systemCallVec[SYS_SLEEP]     = sleep;
    systemCallVec[SYS_DISKREAD]  = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE]  = diskSize;
    systemCallVec[SYS_TERMREAD]  = termRead;
    systemCallVec[SYS_TERMWRITE] = termWrite;

    // initialize procTable
    for (int i = 0; i < MAXPROC; i++) {
        ProcTable[i].wakeUpTime = -1;
        ProcTable[i].sleepSem = semcreateReal(0);
        ProcTable[i].nextWakeUp = NULL;
    }

    // create the terminal charReceive and charSend mailboxes
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        charReceiveBox[i] = MboxCreate(1, 1);
        charSendBox[i]    = MboxCreate(1, 1);
        lineReadBox[i]    = MboxCreate(10, MAXLINE);
        lineWriteBox[i]   = MboxCreate(10, MAXLINE); // TODO does this need to have 10 slots?
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
    char diskbuf[10];
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        // specify which disk unit this is
        sprintf(diskbuf, "%d", i);

        // create the disk driver for this unit
        pid = fork1(name, DiskDriver, diskbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i);
            USLOSS_Halt(1);
        }

        // wait for disk driver to start
        sempReal(running);
    }

    /*
     * Create terminal device drivers.
     */
    char termbuf[10];
    for (int i = 0; i < USLOSS_TERM_UNITS; i++ ) {
        // specify which terminal unit this is
        sprintf(termbuf, "%d", i);

        // create the terminal driver for this unit
        pid = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }

        // create terminal reader
        pid = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term reader %d\n", i);
            USLOSS_Halt(1);
        }

        // create terminal writer
        pid = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term writer %d\n", i);
            USLOSS_Halt(1);
        }
        
        // wait for all to start
        sempReal(running);
        sempReal(running);
        sempReal(running);
    }

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
    int status;

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // wait to run, if something is wrong quit
        int result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
	if (result != 0) {
            return 0;
        }

        /*
         * Compute the current time and wake up any processes
         * whose time has come.
         */
        while (waitQ != NULL && waitQ.wakeUpTime < USLOSS_Clock()) {
            // remove curr from Q and look at next element
            procPtr temp = waitQ.nextWakeUp;
            waitQ.nextWakeUp = NULL;
            waitQ.wakeUpTime = -1;
            waitQ = temp;
        }
    }

    return 0;
}

static int DiskDriver(char *arg) {
    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // unit passed as arg
    int unit = atoi( (char *) arg);

    // probably need to initialize disk stuff for this unit

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // block on disk sem

        // take request from Q

        // writ or read loop for sending/receving data to/from disk

        // send data and wake user 

    }

    return 0;
}

static int TermDriver(char *arg) {
    int unit = (long) arg;
    int status;

    // TODO enable interrupts????

    // Let the parent know we are running and enable interrupts.
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while(!isZapped()) {
        // wait to run, if something is wrong quit
        int result = waitDevice(USLOSS_TERM_INT, 0, &status);
        if (result != 0) {
            quit(0);
        }

        // check for receive character
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
            // send message saying that a char needs to be received
            MboxSend(charReceiveBox[unit], status, sizeof(int));
        }

        // check for send character
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            // send message saying that a char needs to be sent
            MboxSend(charSendBox[unit], status, sizeof(int));
        }
    }

    return 0;
}

static int TermReader(char *arg) {
    int unit = (long) arg;
    int pos = 0;  // position in the line to write a character
    char lines[MAXLINE];

    // Let the parent know we are running
    semvReal(running);
    
    // Infinite loop until we are zap'd
    while (!isZapped()) {
        char *receive; // to hold the receive character

        // wait until there is a character to read in
        MboxReceive(charReceiveBox[unit], receive, sizeof(int));

        // place the character in the line and inc pos
        line[pos] = receive;
        pose++;
        
        // check to see if its time to send the line
        if (receive == '\n' || pos == MAXLINE) {
            // send the line to the mailbox for reading lines
            MboxCondSend(lineReadBox, line, sizeof(lines));

            // clean out line
            for (int i = 0; i < MAXLINE; i++) {
                line[i] = '\0';
            }

            // reset pos back to 0
            pos = 0;
        }
    }

    return 0;
}

static int TermWriter(char *arg) {
    int unit = (long) arg;

    // Let the parent know we are running
    semvReal(running);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        char *receive; // to hold the receive character

        // wait until there is a character to read in
        MboxReceive(charReceiveBox[unit], receive, sizeof(int));

        // place the character in the line and inc pos
        line[pos] = receive;
        pose++;

        // check to see if its time to send the line
        if (receive == '\n' || pos == MAXLINE) {
            // send the line to the mailbox for reading lines
            MboxCondSend(lineReadBox, line, sizeof(lines));

            // clean out line
            for (int i = 0; i < MAXLINE; i++) {
                line[i] = '\0';
            }

            // reset pos back to 0
            pos = 0;
        }
    }

    return 0;
}

static void sleep(systemArgs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: sleep\n", getpid());
    }

    // get value from args
    int seconds = (long) args->arg1;

    // sleep
    int status = sleepReal(seconds);

    // check if sleep was successful
    if (status == 1) {
        args->arg4 = (void *) -1L;
    }
    else {
        args->arg4 = (void *) 0L;
    }
}

static void diskRead(systemArgs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskRead\n", getpid());
    }

    // variables
    int unit     = (long) args->arg5;
    int track    = (long) args->arg3;
    int first    = (long) args->arg4;
    int sectors  = (long) args->arg2;
    void *buffer = args->arg1;

    // do the read
    long status = diskReadReal(unit, track, first, sectors, buffer);

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // set the args
    args->arg1 = (void *) status;
    args->arg4 = (void *) 0L;
}

static void diskWrite(systemArgs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskWrite\n", getpid());
    }

    // variables
    int unit     = (long) args->arg5;
    int track    = (long) args->arg3;
    int first    = (long) args->arg4;
    int sectors  = (long) args->arg2;
    void *buffer = args->arg1;

    // do the read
    long status = diskReadReal(unit, track, first, sectors, buffer);

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // set the args
    args->arg1 = (void *) status;
    args->arg4 = (void *) 0L;
}

static void diskSize(systemArgs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskSize\n", getpid());
    }

    // variables
    int unit = (long) args->arg1;
    int sector;
    int track;
    int disk;

    // do the size check
    int status = diskSizeReal(unit, &sector, &track, &disk);

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // convert ints to long
    long sectorL = sector;
    long trackL  = track;
    long diskL   = disk;

    // set the args
    args->arg1 = (void *) sectorL;
    args->arg2 = (void *) trackL;
    args->arg3 = (void *) diskL;
    args->arg4 = (void *) 0L;
}

static void termRead(systemArgs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: termRead\n", getpid());
    }

    // variables
    char *buffer = (char *) args->arg1;
    int size     = (long) args->arg2;
    int unit     = (long) args->arg3;

    // do the size check
    long status = termReadReal(unit, size, buffer);

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // set the args
    args->arg2 = (void *) status;
    args->arg4 = (void *) 0L;
}

static void termWrite(systemArgs *args) {
    if (debugflag4) {
        USLOSS_Console("process %d: termWrite\n", getpid());
    }

    // variables
    char *buffer = (char *) args->arg1;
    int size     = (long) args->arg2;
    int unit     = (long) args->arg3;

    // do the size check
    long status = termReadReal(unit, size, buffer);

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // set the args
    args->arg2 = (void *) status;
    args->arg4 = (void *) 0L;
}

int sleepReal(int seconds) {
    if (debugflag4) {
        USLOSS_Console("process %d: sleepReal\n", getpid());
    }

    // check for invalid input
    if (seconds < 0) {
        return 1;
    }

    // set the time to wake up
    int pid = getpid() % MAXPROC;
    long wakeTime = USLOSS_Clock() + (seconds * 1000000);
    ProcTable[pid].wakeUpTime = wakeTime;

    // Insert this proc into the queue of procs to be woken up by clock driver
    if (waitQ == NULL) {
        waitQ = &(ProcTable[pid]);
    }
    else if (waitQ->wakeUpTime > wakeTime) {
        procPtr temp = waitQ->nextWakeUp;
        waitQ->nextWakeUp = &(ProcTable[pid]);
        ProcTable[pid].nextWakeUp = temp;
    }
    else {
        procPtr curr = waitQ;
        while (curr->nextWakeUp != NULL && curr->nextWakeUp->wakeUpTime < wakeTime) {
            curr = curr->nextWakeUp;
        }

        // put proc into Q
        procPtr temp = curr->nextWakeUp;
        curr->nextWakeUp = &(ProcTable[pid]);
        ProcTable[pid].nextWakeUp = temp;
    }

    // switch to user mode
    setUserMode();

    // put the process to sleep by blocking until driver wakes it up
    sempReal(ProcTable[pid].sleepSem);

    return 0;
}

int diskReadReal(int unit, int track, int first, int sectors, void *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskReadReal\n", getpid());
    }


    // create request for all sectors we want to read from
    for (int i = 0; i < sectors; i++) {
        // create request
        USLOSS_DeviceRequest request;
        request.opr = USLOSS_DISK_READ;

        // track we read and where we stor information in buffer determined by i
        request.reg1 = (first + i) % 16;
        request.reg2 = buffer + (512 * i);

        // put request on queue
        
    }
    return 0;
}

int diskWriteReal(int unit, int track, int first, int sectors, void *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskWriteReal\n", getpid());
    }

    return 0;
}

int diskSizeReal(int unit, int *sector, int *track, int *disk) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskSizeReal\n", getpid());
    }

    // check if unit is valid
        // if not return -1

    // unit valid so set the variables
    sector = (int *)USLOSS_DISK_SECTOR_SIZE;
    track = (int *)USLOSS_DISK_TRACK_SIZE;
    disk = (int *)(long)ProcTable[unit].numTracks;
    return 0;
}

int termReadReal(int unit, int size, char *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: termReadReal\n", getpid());
    }

    // check for a line to read
    int result = MboxReceive(lineReadBox[unit], buffer, size);

    // if bad input was given
    if (result < 0) {
        return -1;
    }

    return size;
}

int termWriteReal(int unit, int size, char *text) {
    if (debugflag4) {
        USLOSS_Console("process %d: termWriteReal\n", getpid());
    }

    // check for a line to wrie
    result = MboxSend(lineWriteBox[unit], text, size);

    // if bad input
    if (result < 0) {
        return -1;
    }

    return 0;
}

/*
 * setUserMode:
 *  kernel --> user
 */
void setUserMode() {
    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

/*
 * Checks if we are in Kernel mode
 */
void check_kernel_mode(char *name) {
    if ((USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0) {
        USLOSS_Console("%s(): Called while in user mode by process %d. Halting...\n",name, getpid());
        USLOSS_Halt(1);
    }
}
