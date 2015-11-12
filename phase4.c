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
 int debugflag4 = 1;
 int terminateClock;
 int terminateDisk;
 int terminateTerm;

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
int numTracks[USLOSS_DISK_UNITS];
/***********************************************/

void start3(void) {
    char	name[128];
    int		status;

    /*
     * Check kernel mode here.
     */
    check_kernel_mode("start3");

    // init terminateFlag
    terminateClock = 1;
    terminateDisk = 1;
    terminateTerm = 1;

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
        ProcTable[i].procDiskSem = semcreateReal(0);
        ProcTable[i].nextWakeUp = NULL;
    }

    // create the terminal charReceive and charSend mailboxes
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        charReceiveBox[i] = MboxCreate(1, MAXLINE);
        charSendBox[i]    = MboxCreate(1, MAXLINE);
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

    /*
     * Zap the device drivers
     */
    // status's for joining
    int sts = 0;
    int *stsp = &sts;

    // clock
    terminateClock = 0;
    zap(clockPID);
    join(stsp);
    if (debugflag4) {
        USLOSS_Console("closed clock\n");
    }

    // disk
    terminateDisk = 0;
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        semvReal(diskSem[i]);
        zap(diskpid[i]);
        join(stsp);
        if (debugflag4) {
            USLOSS_Console("closed disk %d\n", i);
        }
    }

    // TODO: more than one term flag? one for each loop
    terminateTerm = 0;
/*
    // close out the term files
    FILE *file = fopen("term0.in", "a");
    fprintf(file, "\x03");
    fflush(file);
    fclose(file);
    file = fopen("term1.in", "a");
    fprintf(file, "\x03");
    fflush(file);
    fclose(file);
    file = fopen("term2.in", "a");
    fprintf(file, "\x03");
    fflush(file);
    fclose(file);
    file = fopen("term3.in", "a");
    fprintf(file, "\x03");
    fflush(file);
    fclose(file);
    if (debugflag4) {
        USLOSS_Console("wrote end of text characters\n");
    }
*/
    // term reader
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(charReceiveBox[i], "c", MAXLINE);
        join(stsp);
        if (debugflag4) {
            USLOSS_Console("closed term reader %d\n", i);
        }
    }

    // term writers
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(lineWriteBox[i], "c", MAXLINE);
        join(stsp);
        if (debugflag4) {
            USLOSS_Console("closed term writer %d\n", i);
        }
    }

    // term drivers
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        zap(termpid[i][0]);
        join(stsp);
        if (debugflag4) {
            USLOSS_Console("closed term driver %d\n", i);
        }
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
    while (!isZapped() && terminateClock) {

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
    // do we quit/terminate here?
    return 0;
}

static int DiskDriver(char *arg) {
    // unit passed as arg
    int unit = atoi( (char *) arg);

    if (debugflag4) {
        USLOSS_Console("process %d: DiskDriver%d started\n", getpid(), unit);
    }

    // initialize global variables
    topQ[unit] = NULL;
    bottomQ[unit] = NULL;
    diskSem[unit] = semcreateReal(0);
    diskReq[unit] = semcreateReal(0);
    diskArm[unit] = 0;

    // create request to get numTracks
    int *tracks;

    USLOSS_DeviceRequest req;
    req.opr = USLOSS_DISK_TRACKS;
    req.reg1 = &tracks;

    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
    if (debugflag4) {
        USLOSS_Console("DiskDriver(): numtracks[%d] = %d\n", unit, tracks);
    }

    int status;
    int result = waitDevice(USLOSS_DISK_DEV, unit, &status);
    
    if (result != 0) {
        USLOSS_Console("DiskDriver(): waitDevice returned a non-zero value\n");
        return 0;
    }

    numTracks[unit] = tracks;
    if (debugflag4) {
        USLOSS_Console("DiskDriver(): numTracks is %d, and tracks is %d\n", numTracks[unit], tracks);
    }

    // Let the parent know we are running and enable interrupts. (should this be below init?)
    semvReal(running);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Infinite loop until we are zap'd
    while (!isZapped() && terminateDisk) {
        // block for interrupt
        sempReal(diskSem[unit]);

        if (!terminateDisk) {
            if (debugflag4)
                USLOSS_Console("disk %d breaking loop\n", unit);
            break;
        }

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
        semvReal(ProcTable[req->waitingPID].procDiskSem);
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
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY && !terminateTerm) {
            char c = USLOSS_TERM_STAT_CHAR(status);
            // send message saying that a char needs to be received
            MboxSend(charReceiveBox[unit], &c, sizeof(int));
        }

        // check for send character
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY && !terminateTerm) {
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
        char receive[1]; // to hold the receive character

        // wait until there is a character to read in
        MboxReceive(charReceiveBox[unit], receive, MAXLINE);

        if (!terminateTerm) {
            break;
        }

        // place the character in the line and inc pos
        line[pos] = (char) receive[0];
        pos++;
        
        // check to see if its time to send the line
        if ((char) receive[0] == '\n' || pos == MAXLINE) {
            // send the line to the mailbox for reading lines
            MboxCondSend(lineReadBox[unit], line, MAXLINE);

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
        char receive[MAXLINE]; // to hold the received line

        // turn on interrupts
        long cntr = 0;
        cntr = USLOSS_TERM_CTRL_XMIT_INT(cntr);
        cntr = USLOSS_TERM_CTRL_RECV_INT(cntr);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) cntr);

        // wait until temWriteReal sends a line
        MboxReceive(lineWriteBox[unit], receive, MAXLINE);

        if (!terminateTerm) {
            break;
        }

        // iterate through the line
        for (int i = 0; i < strlen(receive); i++) {
            // get the character from term driver
            char c[1];
            MboxReceive(charReceiveBox[unit], c, MAXLINE);

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

        // TODO disable write interrupts

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

    // do the write
    long status = diskWriteReal(unit, track, first, sectors, buffer);

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
        USLOSS_Console("diskSize(): Bad Status in diskSizeReal call\n");
        args->arg4 = (void *) -1L;
        return;
    }

    // convert ints to long
    long sectorL = sector;
    long trackL  = track;
    long diskL   = disk;

    if (debugflag4) {
        USLOSS_Console("diskSize(): sector size: %d, %d\n", sector, sectorL);
        USLOSS_Console("diskSize(): num sectors: %d, %d\n", track, trackL);
        USLOSS_Console("diskSize(): num tracks: %d, %d\n", disk, diskL);
    }

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
    if (waitQ == NULL || waitQ->wakeUpTime > wakeTime) {
    	if (waitQ == NULL){
        	waitQ = &(ProcTable[pid]);
        } else {
        	ProcTable[pid].nextWakeUp = waitQ;
        	waitQ = &(ProcTable[pid]);
        }
    }
   	else{
    	procPtr curr = waitQ;
    	procPtr prev = NULL;

    	// iterate
    	while (curr != NULL && curr->wakeUpTime < wakeTime){
    		prev = curr;
    		curr = curr->nextWakeUp;
    	}

    	// insert
    	prev->nextWakeUp = &(ProcTable[pid]);
    	ProcTable[pid].nextWakeUp = curr;
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
    if (debugflag4) {
        USLOSS_Console("diskReadReal(): adding to Q\n");
    }
    diskRequest(req, unit);

    // block calling process
    if (debugflag4) {
        USLOSS_Console("diskReadReal(): blocking calling process\n");
    }
    sempReal(ProcTable[getpid() % MAXPROC].procDiskSem);

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
    if (debugflag4) {
        USLOSS_Console("diskWriteReal(): adding to Q\n");
    }
    diskRequest(req, unit);

    // block calling process
    if (debugflag4) {
        USLOSS_Console("diskWriteReal(): blocking calling process\n");
    }
    sempReal(ProcTable[getpid() % MAXPROC].procDiskSem);
    
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

    // wake up disk
    semvReal(diskSem[unit]);
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
    *sector = USLOSS_DISK_SECTOR_SIZE;
    *track = USLOSS_DISK_TRACK_SIZE;
    *disk = numTracks[unit];
    if (debugflag4) {
        USLOSS_Console("diskSizeReal(): sector size: %d, %d\n", USLOSS_DISK_SECTOR_SIZE, sector);
        USLOSS_Console("diskSizeReal(): num sectors: %d, %d\n", USLOSS_DISK_TRACK_SIZE, track);
        USLOSS_Console("diskSizeReal(): num tracks: %d, %d\n", numTracks[unit], disk);
    }

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

    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
    int status;
    int result = waitDevice(USLOSS_DISK_DEV, unit, &status);
    if (debugflag4) {
        USLOSS_Console("diskSeek(): done with waitDevice, status = %d\n", status);
    }
    if (result != 0) {
        USLOSS_Console("diskSeek(): waitDevice returned non-zero value\n");
    }
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
