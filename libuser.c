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
