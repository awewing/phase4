#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <libuser4.h>
#include <usyscall.h>
#include <usloss.h>

#define CHECKMODE {                                             \
        if (USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) {                                \
            USLOSS_Console("Trying to invoke syscall from kernel\n");   \
            USLOSS_Halt(1);                                             \
        }                                                       \
}

int Sleep(int seconds) {
    systemArgs sysArg;

    CHECKMODE;
    sysArg.number = SYS_SLEEP;
    sysArg.arg1 = (void *) ( (long) seconds);
    USLOSS_Syscall(&sysArg);

    return (long) sysArg.arg4;
}

int DiskRead(void *address, int sectors, int startTrack, int startSector, int units, int *status) {
    systemArgs sysArg;

    CHECKMODE;
    sysArg.number = SYS_DISKREAD;
    sysArg.arg1 = address;
    sysArg.arg2 = (void *) ( (long) sectors);
    sysArg.arg3 = (void *) ( (long) startTrack);
    sysArg.arg4 = (void *) ( (long) startSector);
    sysArg.arg5 = (void *) ( (long) units);

    USLOSS_Syscall(&sysArg);
    *status = (long) sysArg.arg1;
    return (long) sysArg.arg4;
}

int DiskWrite(void *address, int sectors, int startTrack, int startSector, int units, int*status) {
    systemArgs sysArg;

    CHECKMODE;
    sysArg.number = SYS_DISKWRITE;
    sysArg.arg1 = address;
    sysArg.arg2 = (void *) ( (long) sectors);
    sysArg.arg3 = (void *) ( (long) startTrack);
    sysArg.arg4 = (void *) ( (long) startSector);
    sysArg.arg5 = (void *) ( (long) units);

    USLOSS_Syscall(&sysArg);
    *status = (long) sysArg.arg1;
    return (long) sysArg.arg4;
}

int DiskSize(int unit, int *sector, int *track, int *disk) {
    systemArgs sysArg;

    CHECKMODE;
    sysArg.number = SYS_DISKSIZE;
    sysArg.arg1 = (void *) ( (long) unit);

    USLOSS_Syscall(&sysArg);
    *sectors = (long) sysArg.arg1;
    *track = (long) sysArg.arg2;
    *disk = (long) sysArg.arg3;
    return (long) sysArg.arg4;
}


int TermRead(int unit, int size, char *buffer) {
    systemArgs sysArg;

    CHECKMODE;
    sysArg.number = SYS_TERMREAD;
    sysArg.arg1 = (void *) buffer;
    sysArg.arg2 = (void *) ( (long) size);
    sysArg.arg3 = (void *) ( (long) unit);

    USLOSS_Syscall(&sysArg);

    long success = (long) sysArg.arg4;
    long size = (long) sysArg.arg2;

    if (success < 0) {
        return success;
    }

    return size;
}


int TermWrite(int unit, int size, char *text) {
    systemArgs sysArg;

    CHECKMODE;
    sysArg.number = SYS_TERMWRITE;
    sysArg.arg1 = (void *) text;
    sysArg.arg2 = (void *) ( (long) size);
    sysArg.arg3 = (void *) ( (long) unit);

    USLOSS_Syscall(&sysArg);

    long success = (long) sysArg.arg4;
    long size = (long) sysArg.arg2;

    if (success < 0) {
        return success;
    }

    return size;
}
