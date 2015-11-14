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
 void diskRequestExec(reqPtr req, int unit);

 void check_kernel_mode(char* name);
 void setUserMode();
/***********************************************/

/************************************************
 * Globals
 ***********************************************/
 int debugflag4 = 0;
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

 int ints[USLOSS_TERM_UNITS];

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
        sprintf(name, "DiskDriver%d", i);

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
        sprintf(name, "TermDriver%d", i);

        // create the terminal driver for this unit
        termpid[i][0] = fork1(name, TermDriver, termbuf, USLOSS_MIN_STACK, 2);
        if (termpid[i][0] < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }

        sprintf(name, "TermReader%d", i);
        // create terminal reader
        termpid[i][1] = fork1(name, TermReader, termbuf, USLOSS_MIN_STACK, 2);
        if (termpid[i][1] < 0) {
            USLOSS_Console("start3(): Can't create term reader %d\n", i);
            USLOSS_Halt(1);
        }

        sprintf(name, "TermWriter%d", i);
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

    // disk
    terminateDisk = 0;
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        semvReal(diskSem[i]);
        zap(diskpid[i]);
        join(stsp);
    }

    terminateTerm = 0;
    // term reader
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(charReceiveBox[i], "c", sizeof(char[MAXLINE]));
        join(stsp);
    }

    // term writers
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxSend(lineWriteBox[i], "c", sizeof(char[MAXLINE]));
        join(stsp);
    }

    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        MboxRelease(charReceiveBox[i]);
        MboxRelease(charSendBox[i]);
        MboxRelease(lineReadBox[i]);
        MboxRelease(lineWriteBox[i]);
        MboxRelease(pidBox[i]);

        long control = 0;
        control = USLOSS_TERM_CTRL_RECV_INT(control);
        int res = USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void *) control);
    }

    // close out the term files
    FILE *file = fopen("term0.in", "a");
    fprintf(file, "last line for termination");
    fflush(file);
    fclose(file);
    file = fopen("term1.in", "a");
    fprintf(file, "last line for termination");
    fflush(file);
    fclose(file);
    file = fopen("term2.in", "a");
    fprintf(file, "last line for termination");
    fflush(file);
    fclose(file);
    file = fopen("term3.in", "a");
    fprintf(file, "last line for termination");
    fflush(file);
    fclose(file);

    // term drivers
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
//        zap(termpid[i][0]);
        join(stsp);
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
        if (debugflag4) {
            USLOSS_Console("diskDriver(%d): inside while loop\n", unit);
        }

        // block for interrupt
        if (debugflag4) {
            USLOSS_Console("diskDriver: blocking on diskSem\n");
        }
        sempReal(diskSem[unit]);
        if (debugflag4) {
            USLOSS_Console("diskDriver: woke up from diskSem\n");
        }

        if (terminateDisk == 0) {
            if (debugflag4)
                USLOSS_Console("disk %d breaking loop\n", unit);
            break;
        }

        // take request from Q
        if (debugflag4) {
            USLOSS_Console("diskDriver: taking request from Q\n");
            if (topQ[unit] == NULL) {
                USLOSS_Console("\ttopQ[%d] is null\n", unit);
            }
        }
        reqPtr req = topQ[unit];
        if (debugflag4) {
            USLOSS_Console("diskDriver: request on track %d reading from sector %d for %d sectors\n", 
                req->track, req->startSector, req->numSectors);
        }

        // remove req from Q
        if (topQ[unit]->nextReq == NULL)
            topQ[unit] = NULL;
        else
            topQ[unit] = topQ[unit]->nextReq;

        if (debugflag4) {
            USLOSS_Console("DiskDriver(): calling diskRequestExec()\n");
        }
        diskRequestExec(req, unit);
        if (debugflag4) {
            USLOSS_Console("diskDriver: returned from diskRequestExec with %s\n", req->buffer);
            USLOSS_Console("diskDriver: semvReal on process %d", req->waitingPID);
        }

        // wake process waiting on the disk action 
        semvReal(ProcTable[req->waitingPID].procDiskSem);
    }

    return 0;
}

void diskRequestExec(reqPtr req, int unit) {
        if (debugflag4) {
            USLOSS_Console("diskRequests: moving disk head to track %d\n", req->track);
        }
        diskSeek(unit, req->track);

        // execute series of actual requests to USLOSSS_DISK_DEV
        if (debugflag4) {
            USLOSS_Console("diskRequests: request for loop begin \n");
        }


        for (int i = 0; i < req->numSectors; i++) {
            USLOSS_DeviceRequest singleRequest;
            singleRequest.opr = req->reqType;
            
            // TODO: change track when i >= 16

            // sector changes depending on i
            singleRequest.reg1 = (void *) req->startSector;

            // address changes depending on which sector we are visiting.
            singleRequest.reg2 = req->buffer;

            // set type
            singleRequest.opr = req->reqType;

            // dev output and waitdev
            if (debugflag4) {
                USLOSS_Console("DiskRequest(): sending devOut the following request\n");
                USLOSS_Console("\treg1 has sector:%d\n", singleRequest.reg1);
                USLOSS_Console("\treg2 has buffer: %s\n", singleRequest.reg2);
                USLOSS_Console("\topr has requesttype\n");
            }
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &singleRequest);
            int status;
            int result = waitDevice(USLOSS_DISK_DEV, unit, &status);
            if (result != 0 && debugflag4) {
                USLOSS_Console("DiskRequests(): waitDevice for singleRequest returned non-zero value\n");
            }

            if (debugflag4) {
                USLOSS_Console("diskDriver(): singleRequest buffer contains: %s\n", req->buffer);
            }
        }
}

static int TermDriver(char *arg) {
    int unit = atoi(arg);
    int status;

    if (debugflag4) {
        USLOSS_Console("process %d: TermDriver%d started\n", getpid(), unit);
    }

    // Let the parent know we are running and enable interrupts.
    semvReal(running);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // wait to run, if something is wrong quit
        int result = waitDevice(USLOSS_TERM_INT, unit, &status);
        if (result != 0) {
            quit(0);
        }

        if (!terminateTerm) {
            break;
        }

        // check for receive character
        int i = USLOSS_TERM_STAT_RECV(status);
        if (i == USLOSS_DEV_BUSY) {
            int ch = USLOSS_TERM_STAT_CHAR(status);

            // send message saying that a char needs to be received
            int r = MboxSend(charReceiveBox[unit], &ch, sizeof(int));

            if (!terminateTerm) {
                break;
            }

            if (debugflag4 && r < 0) {
                USLOSS_Console("TermDriver%d: failed to send for reading\n", unit);
            }
        }
        else if (i == USLOSS_DEV_ERROR) {
           if (debugflag4) {
                USLOSS_Console("TermDriver%d: DEV_ERROR\n", unit);
            }
            USLOSS_Halt(0);
        }

        // check for send character
        int o = USLOSS_TERM_STAT_XMIT(status);
        if (o == USLOSS_DEV_READY) {
            if (debugflag4) {
                USLOSS_Console("TermDriver%d: writing\n", unit);
            }

            // send message saying that a char needs to be sent
            int r = MboxCondSend(charSendBox[unit], &status, sizeof(int));

            if (!terminateTerm) {
                break;
            }

            if (debugflag4 && r < 0) {
                USLOSS_Console("TermDriver%d: failed to send for reading\n", unit);
            }
        }
        else if (o == USLOSS_DEV_ERROR) {
           if (debugflag4) {
                USLOSS_Console("TermDriver%d: DEV_ERROR\n", unit);
            }
            USLOSS_Halt(0);
        }

    }

    return 0;
}

static int TermReader(char *arg) {
    int unit = atoi(arg);
    int pos = 0;  // position in the line to write a character
    char line[MAXLINE + 1];
    int receive; // to hold the receive character
    
    // null out the line
    for (int i = 0; i < MAXLINE + 1; i++) {
        line[i] = '\0';
    }

    // Let the parent know we are running and enable interrupts.
    semvReal(running);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // wait until there is a character to read in
        MboxReceive(charReceiveBox[unit], &receive, sizeof(int));

        if (!terminateTerm) {
            break;
        }

        // place the character in the line and inc pos
        line[pos] = receive;
        ++pos;

        // check to see if its time to send the line
        if (receive == '\n' || pos == MAXLINE) {
            // make the last character null
            line[pos] = '\0';

            if (debugflag4) {
                printf("TermReader%d: sending the line \"%s\" \n", unit, line);
            }

            // send the line to the mailbox for reading lines
            MboxCondSend(lineReadBox[unit], line, sizeof(char[strlen(line)]));

            // clean out line
            for (int i = 0; i < MAXLINE; i++) {
                line[i] = '\0';
            }

            // reset pos back to 0
            pos = 0;
        }
    }
    quit(0);
    return 0;
}

static int TermWriter(char *arg) {
    int unit = atoi(arg);
    int cntr = 0;
    int res;
    char receive[MAXLINE]; // to hold the received line

    // Let the parent know we are running and enable interrupts.
    semvReal(running);

    // Infinite loop until we are zap'd
    while (!isZapped()) {
        // wait until temWriteReal sends a line
        int size = MboxReceive(lineWriteBox[unit], receive, MAXLINE);

        if (!terminateTerm) {
            break;
        }

        // turn on interrupts
        cntr = 0;
        cntr = USLOSS_TERM_CTRL_XMIT_INT(cntr);
        if (ints[unit] == 1) {
            cntr = USLOSS_TERM_CTRL_RECV_INT(cntr);
            ints[unit] = 1;
        }
        res = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) (long) cntr);
        if (res != USLOSS_DEV_OK) {
            USLOSS_Console("process %d: TermWriter%d quit unexpectadly.Res: %d\n",getpid(),unit,res);
            USLOSS_Halt(0);
        }

        // iterate through the line
        for (int i = 0; i < size; i++) {
            // get the character from term driver
            int status;
            MboxReceive(charSendBox[unit], &status, sizeof(int));

            // transmit the character
            int o = USLOSS_TERM_STAT_XMIT(status);
            if (o == USLOSS_DEV_READY) {
                cntr = 0;
                cntr = USLOSS_TERM_CTRL_CHAR(cntr, receive[i]);
                cntr = USLOSS_TERM_CTRL_XMIT_INT(cntr);
                cntr = USLOSS_TERM_CTRL_RECV_INT(cntr);
                cntr = USLOSS_TERM_CTRL_XMIT_CHAR(cntr);

                if (ints[unit] == 1) {
                    cntr = USLOSS_TERM_CTRL_RECV_INT(cntr);
                    ints[unit] = 1;
                }
                res = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) (long) cntr);
            }
        }

        // wait until termWriterReal send its pid
        int pid;
        MboxReceive(pidBox[unit], &pid, sizeof(int));

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

    setUserMode();
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

    if (debugflag4) {
        USLOSS_Console("diskRead(): calling diskReadReal\n");
    }

    // do the read
    long status = diskReadReal(unit, track, first, sectors, buffer);

    if (debugflag4) {
        USLOSS_Console("-------------diskRead(): returning from diskReadReal\n");
    }

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // set the args
    args->arg1 = (void *) status;
    args->arg4 = (void *) 0L;

    if(debugflag4){
        USLOSS_Console("diskRead(): got the string %s\n", buffer);
        USLOSS_Console("\tand is returning %s\n", args->arg1);
    }

    setUserMode();
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

    setUserMode();
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

    setUserMode();
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

    setUserMode();
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
    long status = termWriteReal(unit, size, buffer);

    // check for bad input
    if (status == -1) {
        args->arg4 = (void *) -1L;
        return;
    }

    // set the args
    args->arg2 = (void *) status;
    args->arg4 = (void *) 0L;

    setUserMode();
}

int sleepReal(int seconds) {
    if (debugflag4) {
        USLOSS_Console("process %d: sleepReal\n", getpid());
    }

    check_kernel_mode("sleepReal");

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
    
    check_kernel_mode("termReadReal");

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

    if (debugflag4) {
        USLOSS_Console("diskReadReal(): got the string: %s\n", buffer);
        USLOSS_Console("diskReadReal(): req buffer: %s\n", req.buffer);
    }
    return 0;
}

int diskWriteReal(int unit, int track, int first, int sectors, void *buffer) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskWriteReal\n", getpid());
        USLOSS_Console("\tstring write is %s\n", buffer);
    }

    check_kernel_mode("diskWriteReal");

    // create request struct
    request req;
    req.track = track;
    req.startSector = first;
    req.numSectors = sectors;
    req.waitingPID = getpid();
    req.buffer = buffer;
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
    if (debugflag4) {
        USLOSS_Console("diskReques(%d)\n", unit);
    }

    // insert in topQ
    if (req.track > diskArm[unit]) {
        if (debugflag4) {
            USLOSS_Console("diskRequest: inserting in topQ[%d]\n", unit);
        }

        //if Q is empty
        if (topQ[unit] == NULL) {
            topQ[unit] = &(req);
            if (debugflag4) {
                USLOSS_Console("diskRequest: inserting in empty Q\n");
            }
        }

        // if Q is not empty
        else {
            reqPtr curr = topQ[unit];
            reqPtr prev = NULL;
            while (curr != NULL && curr->track < req.track) {
                prev = curr;
                curr = curr->nextReq;
            }
            if (debugflag4) {
                USLOSS_Console("diskRequest: inserting in non-empty Q\n");
            }
            prev->nextReq = &req;
            req.nextReq = curr;
        }
    }

    else {
        if (debugflag4) {
            USLOSS_Console("diskRequest: inserting in botQ[%d]\n", unit);
        }

        //if Q is empty
        if (bottomQ[unit] == NULL) {
            bottomQ[unit] = &(req);
            if (debugflag4) {
                USLOSS_Console("diskRequest: inserting in empty Q\n");
            }
        }

        // if Q is not empty
        else {
            reqPtr curr = bottomQ[unit];
            reqPtr prev = NULL;
            while (curr != NULL && curr->track < req.track) {
                prev = curr;
                curr = curr->nextReq;
            }
            if (debugflag4) {
                USLOSS_Console("diskRequest: inserting in non-empty Q\n");
            }
            prev->nextReq = &req;
            req.nextReq = curr;
        }
    }

    // wake up disk
    if (debugflag4) {
        USLOSS_Console("diskRequest: waking up disk driver\n");
    }
    semvReal(diskSem[unit]);
}

int diskSizeReal(int unit, int *sector, int *track, int *disk) {
    if (debugflag4) {
        USLOSS_Console("process %d: diskSizeReal\n", getpid());
    }

    check_kernel_mode("diskSizeReal");

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
        USLOSS_Console("process %d: termReadReal for unit: %d\n", getpid(), unit);
    }

    check_kernel_mode("termReadReal");

    // check bad input
    if (unit < 0 || size < 0) {
        return -1;
    }

    char in[MAXLINE];

    // turn on read interrupts
    if (ints[unit] == 0) {
        long control = 0;
        control = USLOSS_TERM_CTRL_RECV_INT(control);
        int res = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *) control);
        ints[unit] = 1;
    }

    // check for a line to read
    int result = MboxReceive(lineReadBox[unit], in, sizeof(char[MAXLINE]));

    memcpy(buffer, in, size);

    int ret = result;
    if (size < result) {
        ret = size;
    }

    return ret;
}

int termWriteReal(int unit, int size, char *text) {
    if (debugflag4) {
        USLOSS_Console("process %d: termWriteReal for unit: %d\n", getpid(), unit);
    }

    check_kernel_mode("termWriteReal");

    // check bad input
    if (unit < 0 || size < 0) {
        return -1;
    }

    // send its pid
    int pid = getpid();
    MboxSend(pidBox[unit], &pid, sizeof(int));

    // send its text
    int result = MboxSend(lineWriteBox[unit], text, size);

    // block until termwriter is done
    sempReal(ProcTable[getpid() % MAXPROC].termSem);
//USLOSS_Console("%d\n", size);
    return size;
}

void check_kernel_mode(char *name) {
    if ((USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0) {
        USLOSS_Console("%s(): Called while in user mode by process %d. Halting...\n",name, getpid());
        USLOSS_Halt(1);
    }
}

void setUserMode() {
    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}
