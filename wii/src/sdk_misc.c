/* The rest of the SDK the game calls, where the Wii has nothing to do: the
 * DSP (the port runs no DSP microcode: jaudio mixes on the CPU, as on PC),
 * the GBA link cable, the debugger's EXI channel, the mutexes over the
 * port's thread queues. */

#include "nk_wii.h"

#include <string.h>

#include <dolphin/dsp.h>
#include <dolphin/os.h>

/* dsp.c */
void DSPInit(void) {}
BOOL DSPCheckInit(void) { return TRUE; }
u32 DSPCheckMailToDSP(void) { return 0; }
u32 DSPCheckMailFromDSP(void) { return 0; }
u32 DSPReadMailFromDSP(void) { return 0; }
u32 DSPReadCPUToDSPMbox(void) { return 0; }
void DSPSendMailToDSP(u32 mail) { (void) mail; }
void DSPAssertInt(void) {}
void DSPReset(void) {}
void DSPHalt(void) {}
void DSPUnhalt(void) {}
u32 DSPGetDMAStatus(void) { return 0; }
DSPTaskInfo* DSPAddTask(DSPTaskInfo* task) { return task; }

/* OSMutex.c: a mutex over the port's thread queues (no priority inheritance:
 * libogc's scheduler preempts by priority anyway) */
void OSInitMutex(OSMutex* mutex)
{
    OSInitThreadQueue(&mutex->queue);
    mutex->thread = NULL;
    mutex->count = 0;
}

void OSLockMutex(OSMutex* mutex)
{
    BOOL enabled = OSDisableInterrupts();
    OSThread* self = OSGetCurrentThread();
    for (;;) {
        if (mutex->thread == NULL) {
            mutex->thread = self;
            mutex->count = 1;
            break;
        }
        if (mutex->thread == self) {
            mutex->count++;
            break;
        }
        OSSleepThread(&mutex->queue);
    }
    OSRestoreInterrupts(enabled);
}

void OSUnlockMutex(OSMutex* mutex)
{
    BOOL enabled = OSDisableInterrupts();
    if (mutex->thread == OSGetCurrentThread() && --mutex->count == 0) {
        mutex->thread = NULL;
        OSWakeupThread(&mutex->queue);
    }
    OSRestoreInterrupts(enabled);
}

BOOL OSTryLockMutex(OSMutex* mutex)
{
    BOOL enabled = OSDisableInterrupts();
    OSThread* self = OSGetCurrentThread();
    BOOL locked = FALSE;
    if (mutex->thread == NULL) {
        mutex->thread = self;
        mutex->count = 1;
        locked = TRUE;
    } else if (mutex->thread == self) {
        mutex->count++;
        locked = TRUE;
    }
    OSRestoreInterrupts(enabled);
    return locked;
}

void OSInitCond(OSCond* cond)
{
    OSInitThreadQueue(&cond->queue);
}

void OSWaitCond(OSCond* cond, OSMutex* mutex)
{
    BOOL enabled = OSDisableInterrupts();
    OSThread* self = OSGetCurrentThread();
    if (mutex->thread == self) {
        s32 count = mutex->count;
        mutex->thread = NULL;
        mutex->count = 0;
        OSWakeupThread(&mutex->queue);
        OSSleepThread(&cond->queue);
        while (mutex->thread != NULL) {
            OSSleepThread(&mutex->queue);
        }
        mutex->thread = self;
        mutex->count = count;
    }
    OSRestoreInterrupts(enabled);
}

void OSSignalCond(OSCond* cond)
{
    OSWakeupThread(&cond->queue);
}
