#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <provided_prototypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 void diskRequest(request req, int unit);
 int diskSizeReal(int unit, int *sector, int *track, int *disk);
 void diskSeek(int unit, int track);
 int termReadReal(int unit, int size, char *buffer);
 int termWriteReal(int unit, int size, char *text);

 void diskRequest(request req, int unit);

 void check_kernel_mode(char* name);
/***********************************************/

/************************************************
 * Globals
 ***********************************************/
 int debugflag4 = 0;

 int running; // the semaphore that waits for drivers to start
 process ProcTable[MAXPROC];
 procPtr waitQ;

 int charReceiveBox[USLOSS_TERM_UNITS]; // mailboxs for receiving a character
 int charSendBox[USLOSS_TERM_UNITS];    // mailboxs for sending a character
 int lineReadBox[USLOSS_TERM_UNITS];    // mailboxs for reading a line
 int lineWriteBox[USLOSS_TERM_UNITS];   // mailboxs for writing a line
 int pidBox[USLOSS_TERM_UNITS];         // mailboxs for sending a pid

int diskSem[USLOSS_DISK_UNITS];
reqPtr topQ[USLOSS_DISK_UNITS];
reqPtr bottomQ[USLOSS_DISK_UNITS];
int diskReq[USLOSS_DISK_UNITS];
int diskArm[USLOSS_DISK_UNITS];
long numTracks[USLOSS_DISK_UNITS];
/***********************************************/

void start3(void) {
    char	name[128];
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
        ProcTable[i].termSem = semcreateReal(0);
        ProcTable[i].nextWakeUp = NULL;
    }

    // create the terminal charReceive and charSend mailboxes
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        charReceiveBox[i] = MboxCreate(1, 1);
        charSendBox[i]    = MboxCreate(1, 1);
        lineReadBox[i]    = MboxCreate(10, MAXLINE);
        lineWriteBox[i]   = MboxCreate(10, MAXLINE);
        pidBox[i]         = MboxCreate(10, MAXLINE);
    }

    /*
     * Create clock device driver 
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    running = semcreateReal(0);
    int clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
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
    int diskpid[USLOSS_DISK_UNITS];
    char diskbuf[10];
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        // specify which disk unit this is
        sprintf(diskbuf, "%d", i);

        // create the disk driver for this unit
        diskpid[i] = fork1(name, DiskDriver, diskbuf, USLOSS_MIN_STACK, 2);
        if (diskpid[i] < 0) {
            USLOSS_Console("start3(): Can't create disk driver %d\n", i);
            USLOSS_Halt(1);
        }

        // wait for disk driver to start
        sempReal(running);
    }

    /*
     * Create terminal device drivers.
     */
    int termpid[USLOSS_TERM_UNITS][3];
    char termbuf[10];
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        // specify which terminal unit this is
        sprintf(termbuf, "%d", i);

        // create the terminal driver for this unit
        termpid[i][0] = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
        if (termpid[i][0] < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }

        // create terminal reader
        termpid[i][1] = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        if (termpid[i][1] < 0) {
            USLOSS_Console("start3(): Can't create term reader %d\n", i);
            USLOSS_Halt(1);
        }

        // create terminal writer
        termpid[i][2] = fork1(name, TermWriter, termbuf, USLOSS_MIN_STACK, 2);
        if (termpid[i][2] < 0) {
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
    spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    waitReal(&status);

    // TODO terminate the terminals somehow mysteriouslly?????

    /*
     * Zap the device drivers
     */
    zap(clockPID);
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        zap(diskpid[i]);
    }
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        zap(termpid[i][0]); // zap the driver
        zap(termpid[i][1]); // zap the reader
        zap(termpid[i][2]); // zap the writer
    }

    // eventually, at the end:
    quit(0);    
}

static int ClockDriver(char *arg) {
    if (debugflag4) {
        USLOSS_Console("process %d: ClockDriver started\n", getpid());
    }

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
        while (waitQ != NULL && waitQ->wakeUpTime < USLOSS_Clock()) {
            // wake up the sleeper
            semvReal(waitQ->sleepSem);

            // remove curr from Q and look at next element
            procPtr temp = waitQ->nextWakeUp;
            waitQ->nextWakeUp = NULL;
            waitQ->wakeUpTime = -1;
            waitQ = temp;
        }
    }

    return 0;
}

static int DiskDriver(char *arg) {
    // unit passed as arg
    int unit = atoi( (char *) arg);

    if (debugflag4) {
        USLOSS_Console("process %d: DiskDriver%d started\n", getpid(), unit);
    }

    // Let the parent know we are running and enable interrupts. (should this be below init?)
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // initialize global variables
    topQ[unit] = NULL;
    bottomQ[unit] = NULL;
    diskSem[unit] = semcreateReal(0);
    diskReq[unit] = semcreateReal(0);
    diskArm[unit] = 0;

    // create request to get numTracks
    USLOSS_DeviceRequest req;
    req.opr = USLOSS_DISK_TRACKS;
    req.reg1 = &numTracks[unit];
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);

    // wait device idk why
    int status;
    waitDevice(USLOSS_DISK_DEV, unit, &status);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // block on disk sem
        sempReal(diskSem[unit]);

        // take request from Q
        reqPtr req = topQ[unit];
        topQ[unit] = topQ[unit]->nextReq;

        // Move head
        diskSeek(unit, req->track);

        // execute series of actual requests to USLOSSS_DISK_DEV
        for (int i = 0; i < req->numSectors; i++) {
            USLOSS_DeviceRequest singleRequest;
            singleRequest.opr = req->reqType;
            
            // sector changes depending on i
            singleRequest.reg1 = (void *) ((req->startSector + i) % 16);

            // address changes depending on which sector we are visiting.
            singleRequest.reg2 = &(req->buffer) + (512 * i);
        }

        // wake process waiting on the disk action 
        semvReal(ProcTable[req->waitingPID].sleepSem);
    }

    return 0;
}

static int TermDriver(char *arg) {
    int unit = atoi(arg);
    int status;

    if (debugflag4) {
        USLOSS_Console("process %d: TermDriver%d started\n", getpid(), unit);
    }

    // Let the parent know we are running
    semvReal(running);

    // turn on read interrupts
    long control = 0;
    control = USLOSS_TERM_CTRL_RECV_INT(control);
    int res = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) control);
    if (res != USLOSS_DEV_OK) {
        if (debugflag4) {
            USLOSS_Console("process %d: TermDriver quit unexpectadly. Res: %d\n", getpid(), res);
        }
        USLOSS_Halt(0);
    }

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // wait to run, if something is wrong quit
        int result = waitDevice(USLOSS_TERM_INT, 0, &status);
        if (result != 0) {
            quit(0);
        }

        // check for receive character
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
            char c = USLOSS_TERM_STAT_CHAR(status);
            // send message saying that a char needs to be received
            MboxSend(charReceiveBox[unit], &c, sizeof(int));
        }

        // check for send character
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            char c = USLOSS_TERM_STAT_CHAR(status);
            // send message saying that a char needs to be sent
            MboxSend(charSendBox[unit], &c, sizeof(int));
        }
    }

    return 0;
}

static int TermReader(char *arg) {
    int unit = atoi(arg);
    int pos = 0;  // position in the line to write a character
    char line[MAXLINE];

    // Let the parent know we are running
    semvReal(running);
    
    // Infinite loop until we are zap'd
    while (!isZapped()) {
        char *receive = '\0'; // to hold the receive character

        // wait until there is a character to read in
        MboxReceive(charReceiveBox[unit], receive, sizeof(int));

        // place the character in the line and inc pos
        line[pos] = (char) receive;
        pos++;
        
        // check to see if its time to send the line
        if ((char) receive == '\n' || pos == MAXLINE) {
            // send the line to the mailbox for reading lines
            MboxCondSend(lineReadBox[unit], line, sizeof(line));

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
    int unit = atoi(arg);

    // Let the parent know we are running
    semvReal(running);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        char *receive = '\0'; // to hold the received line

        // turn on interrupts
        long cntr = 0;
        cntr = USLOSS_TERM_CTRL_XMIT_INT(cntr);
        cntr = USLOSS_TERM_CTRL_RECV_INT(cntr);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) cntr);

        // wait until temWriteReal sends a line
        MboxReceive(lineWriteBox[unit], receive, MAXLINE);

        // iterate through the line
        for (int i = 0; i < strlen(receive); i++) {
            // get the character from term driver
            char *c;
            MboxReceive(charReceiveBox[unit], c, sizeof(int));

            // transmit the character
            long control = 0;
            control = USLOSS_TERM_CTRL_CHAR(control, c[0]);
            control = USLOSS_TERM_CTRL_XMIT_INT(control);
            control = USLOSS_TERM_CTRL_XMIT_CHAR(control);

            // check if returned properly
            int result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) control);
            if (result != USLOSS_DEV_OK) {
                if (debugflag4) {
                    USLOSS_Console("process %d: TermWriter quit unexpectadly\n", getpid());
                }
                USLOSS_Halt(0);
            }
        }

        // wait until termWriterReal send its pid
        char pidC[10];
        MboxReceive(pidBox[unit], pidC, sizeof(int));
        int pid = atoi((char *) pidC);

        // wake up the waiting process
        semvReal(ProcTable[pid % MAXPROC].termSem);
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

    // put the process to sleep by blocking until driver wakes it up
    sempReal(ProcTable[pid].sleepSem);
    return 0;
}

int diskReadReal(int unit, int track, int first, int sectors, void *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskReadReal\n", getpid());
    }
    
    // create request struct
    request req;
    req.track = track;
    req.startSector = first;
    req.numSectors = sectors;
    req.waitingPID = getpid();
    req.buffer = &buffer;
    req.reqType = USLOSS_DISK_READ;
    req.nextReq = NULL;

    // add request to Q
    diskRequest(req, unit);

    // block calling process
    sempReal(ProcTable[getpid() % MAXPROC].sleepSem);

    return 0;
}

int diskWriteReal(int unit, int track, int first, int sectors, void *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskWriteReal\n", getpid());
    }

    // create request struct
    request req;
    req.track = track;
    req.startSector = first;
    req.numSectors = sectors;
    req.waitingPID = getpid();
    req.buffer = &buffer;
    req.reqType = USLOSS_DISK_WRITE;
    req.nextReq = NULL;

    // add request to Q
    diskRequest(req, unit);

    // block calling process
    sempReal(ProcTable[getpid() % MAXPROC].sleepSem);
    
    return 0;
}

void diskRequest(request req, int unit) {
    // find q to insert into
    reqPtr Q;
    if (req.track > diskArm[unit]) {
        Q = topQ[unit];
    }
    else {
        Q = bottomQ[unit];
    }

    reqPtr curr = Q;
    reqPtr prev = NULL;

    // case 1, q is empty
    if (curr == NULL) {
        Q = &(req);
        return;
    }

    while (curr != NULL && curr->track < req.track) {
        prev = curr;
        curr = curr->nextReq;
    }
    prev->nextReq = &req;
    req.nextReq = curr;


}

int diskSizeReal(int unit, int *sector, int *track, int *disk) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskSizeReal\n", getpid());
    }

    // check if unit is valid
    if (unit < 0 || unit > USLOSS_DISK_UNITS) {
        return -1;
    }

    // unit valid so set the variables
    sector = (int * )USLOSS_DISK_SECTOR_SIZE;
    track = (int *) USLOSS_DISK_TRACK_SIZE;
    disk = (int *) numTracks[unit];

    return 0;
}

void diskSeek(int unit, int track) {
    if ( track >= numTracks[unit]) {
        USLOSS_Halt(0);
        return;
    }

    USLOSS_DeviceRequest req;
    req.opr = USLOSS_DISK_SEEK;
    req.reg1 = track;

    // TODO sorry alex, this line was broke so i comented it out 
    //USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, req);
}

int termReadReal(int unit, int size, char *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: termReadReal\n", getpid());
    }

    char in[MAXLINE];

    // check for a line to read
    int result = MboxReceive(lineReadBox[unit], in, size);

    memcpy(buffer, in, size);

    // if bad input was given
    if (result < 0) {
        return -1;
    }

    return result;
}

int termWriteReal(int unit, int size, char *text) {
    if (debugflag4) {
        USLOSS_Console("process %d: termWriteReal\n", getpid());
    }

    // send its pid
    char pidC[10];
    sprintf(pidC, "%d", getpid());
    MboxSend(pidBox[unit], pidC, sizeof(int));

    // send its text
    int result = MboxSend(lineWriteBox[unit], text, size);

    // if bad input
    if (result < 0) {
        return -1;
    }

    // block until termwriter is done
    sempReal(ProcTable[getpid() % MAXPROC].termSem);

    return result;
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
