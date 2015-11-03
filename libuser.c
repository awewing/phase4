#include <phase1.h>
#include <phase2.h>
#include <libuser.h>
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
    sysArg.arg1 = (void *) seconds;
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
    *status = (int) sysArg.arg1;
    return (int) sysArg.arg4;
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
    *status = (int) sysArg.arg1;
    return (int) sysArg.arg4;
}
