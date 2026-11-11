/* The SDK's threads on libogc's. An OSThread is the SDK's structure, kept
 * for the game and GX to hold; the thread under it is an LWP. Thread queues
 * are tuxedo's: one queue, each sleeper waiting on its OSThreadQueue's
 * address (libogc's LWP_ThreadSleep does the same with its queue ids). */

#include "nk_wii.h"
#include "ogc_bridge.h"

#include <malloc.h>
#include <reent.h>
#include <string.h>

#include <dolphin/os.h>
#include <dolphin/os/OSThread.h>

#define MAX_THREADS 16

typedef struct ThreadSlot {
    OSThread* thread;
    ogc_lwp_t lwp;
    void* (*func)(void*);
    void* param;
} ThreadSlot;

static ThreadSlot s_slots[MAX_THREADS];
static OSThread s_main_thread;
static ogc_KThrQueue s_sleepers;

void nk_wii_thread_init(void)
{
    memset(&s_main_thread, 0, sizeof(s_main_thread));
    s_main_thread.state = OS_THREAD_STATE_RUNNING;
    s_main_thread.priority = s_main_thread.base = 16;
    s_slots[0].thread = &s_main_thread;
    s_slots[0].lwp = LWP_GetSelf();
}

static ThreadSlot* slot_of_lwp(ogc_lwp_t lwp)
{
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_slots[i].thread != NULL && s_slots[i].lwp == lwp) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static ThreadSlot* slot_of(OSThread* thread)
{
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_slots[i].thread == thread) {
            return &s_slots[i];
        }
    }
    return NULL;
}

/* A thread the SDK did not create (a shim's worker) is still given one */
static OSThread s_foreign_thread;

OSThread* OSGetCurrentThread(void)
{
    ThreadSlot* slot = slot_of_lwp(LWP_GetSelf());
    return slot != NULL ? slot->thread : &s_foreign_thread;
}

/* The SDK's 0 (highest) to 31 (lowest) in libogc's 127 (highest) to 0; the
 * SDK's main thread (16) at libogc's (64) */
static u8 ogc_priority(OSPriority priority)
{
    int p = 80 - priority;
    if (p < 1) {
        p = 1;
    } else if (p > 126) {
        p = 126;
    }
    return (u8) p;
}

/* libogc starts its threads running; the SDK's wait for OSResumeThread */
static void* thread_entry(void* arg)
{
    ThreadSlot* slot = arg;
    BOOL enabled = OSDisableInterrupts();
    while (slot->thread->suspend > 0) {
        KThrQueueBlock(&s_sleepers, (uintptr_t) slot->thread);
    }
    OSRestoreInterrupts(enabled);
    nk_wii_fpscr();
    return slot->func(slot->param);
}

int OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
                   void* stack, unsigned long stackSize, long priority,
                   unsigned short attr)
{
    BOOL enabled = OSDisableInterrupts();
    ThreadSlot* slot = NULL;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_slots[i].thread == NULL) {
            slot = &s_slots[i];
            break;
        }
    }
    if (slot == NULL) {
        OSRestoreInterrupts(enabled);
        OSReport("OSCreateThread: no free slot\n");
        return FALSE;
    }
    memset(thread, 0, sizeof(*thread));
    thread->state = OS_THREAD_STATE_READY;
    thread->attr = attr;
    thread->priority = thread->base = priority;
    thread->suspend = 1;
    thread->stackBase = stack;
    thread->stackEnd = (u32*) ((u8*) stack - stackSize);
    slot->thread = thread;
    slot->func = func;
    slot->param = param;
    OSRestoreInterrupts(enabled);

    /* The SDK's stack is given by its top; libogc's by its base */
    void* base = (u8*) stack - stackSize;
    if (LWP_CreateThread(&slot->lwp, thread_entry, slot, base, stackSize,
                         ogc_priority(priority)) < 0)
    {
        slot->thread = NULL;
        OSReport("OSCreateThread: LWP_CreateThread failed\n");
        return FALSE;
    }
    return TRUE;
}

s32 OSResumeThread(OSThread* thread)
{
    BOOL enabled = OSDisableInterrupts();
    ThreadSlot* slot = slot_of(thread);
    s32 old = thread->suspend;
    if (thread->suspend > 0) {
        thread->suspend--;
    }
    if (slot != NULL && thread->suspend == 0) {
        thread->state = OS_THREAD_STATE_RUNNING;
        if (thread->attr & 0x8000) {
            /* suspended by OSSuspendThread */
            thread->attr &= ~0x8000;
            nk_wii_wait_suspend((unsigned int) slot->lwp, 0, NULL);
            LWP_ResumeThread(slot->lwp);
        } else {
            /* waiting in thread_entry */
            KThrQueueUnblockAllByValue(&s_sleepers, (uintptr_t) thread);
        }
    }
    OSRestoreInterrupts(enabled);
    return old;
}

s32 OSSuspendThread(OSThread* thread)
{
    BOOL enabled = OSDisableInterrupts();
    ThreadSlot* slot = slot_of(thread);
    s32 old = thread->suspend++;
    if (slot != NULL && old == 0) {
        thread->state = OS_THREAD_STATE_READY;
        thread->attr |= 0x8000;
        OSRestoreInterrupts(enabled);
        nk_wii_wait_suspend((unsigned int) slot->lwp, 1, __builtin_return_address(0));
        LWP_SuspendThread(slot->lwp);
        return old;
    }
    OSRestoreInterrupts(enabled);
    return old;
}

void OSYieldThread(void)
{
    LWP_YieldThread();
}

long OSCheckActiveThreads(void)
{
    long n = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_slots[i].thread != NULL) {
            n++;
        }
    }
    return n;
}

void OSInitThreadQueue(OSThreadQueue* queue)
{
    queue->head = queue->tail = NULL;
}

/* Called with interrupts disabled, as the SDK's is: the caller checked its
 * condition and is not to miss the wakeup */
void OSSleepThread(OSThreadQueue* queue)
{
    nk_wii_wait_begin(NK_WII_WAIT_SLEEP, queue, __builtin_frame_address(0));
    KThrQueueBlock(&s_sleepers, (uintptr_t) queue);
    nk_wii_wait_end();
}

void OSWakeupThread(OSThreadQueue* queue)
{
    KThrQueueUnblockAllByValue(&s_sleepers, (uintptr_t) queue);
}

/* The rest of OSThread.c. A thread's LWP ends when its function returns
 * (thread_entry): OSExitThread marks the thread done and sleeps for good, the
 * thread's slot stays (the game creates its threads once). */
static ogc_KThrQueue s_joiners;

BOOL OSIsThreadTerminated(OSThread* thread)
{
    return thread->state == OS_THREAD_STATE_MORIBUND || thread->state == 0;
}

void OSExitThread(void* val)
{
    OSThread* self = OSGetCurrentThread();
    BOOL enabled = OSDisableInterrupts();
    self->val = val;
    self->state = OS_THREAD_STATE_MORIBUND;
    KThrQueueUnblockAllByValue(&s_joiners, (uintptr_t) self);
    for (;;) {
        KThrQueueBlock(&s_sleepers, (uintptr_t) &s_joiners);
    }
    OSRestoreInterrupts(enabled);
}

void OSCancelThread(OSThread* thread)
{
    if (thread == OSGetCurrentThread()) {
        OSExitThread(NULL);
    }
    BOOL enabled = OSDisableInterrupts();
    ThreadSlot* slot = slot_of(thread);
    thread->state = OS_THREAD_STATE_MORIBUND;
    if (slot != NULL) {
        LWP_SuspendThread(slot->lwp);
    }
    KThrQueueUnblockAllByValue(&s_joiners, (uintptr_t) thread);
    OSRestoreInterrupts(enabled);
}

BOOL OSJoinThread(OSThread* thread, void** val)
{
    BOOL enabled = OSDisableInterrupts();
    while (thread->state != OS_THREAD_STATE_MORIBUND) {
        KThrQueueBlock(&s_joiners, (uintptr_t) thread);
    }
    if (val != NULL) {
        *val = thread->val;
    }
    OSRestoreInterrupts(enabled);
    return TRUE;
}

void OSDetachThread(OSThread* thread)
{
    thread->attr |= OS_THREAD_ATTR_DETACH;
}

BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority)
{
    BOOL enabled = OSDisableInterrupts();
    ThreadSlot* slot = slot_of(thread);
    thread->priority = thread->base = priority;
    if (slot != NULL) {
        LWP_SetThreadPriority(slot->lwp, ogc_priority(priority));
    }
    OSRestoreInterrupts(enabled);
    return TRUE;
}

s32 OSGetThreadPriority(OSThread* thread)
{
    return thread->base;
}

/* Leaving (ogc/main.c): the game's threads stopped where they are, for good, all but the caller's: nothing of the
 * game runs while the console shuts down (a GameCube's OSResetSystem stops them the same way). None is stopped
 * inside malloc, whose lock the shutdown takes too: it is held meanwhile. */
void nk_wii_threads_stop(void)
{
    ogc_lwp_t self = LWP_GetSelf();
    __malloc_lock(_REENT);
    for (int i = 0; i < MAX_THREADS; i++) {
        if (s_slots[i].thread != NULL && s_slots[i].lwp != 0 && s_slots[i].lwp != self) {
            LWP_SuspendThread(s_slots[i].lwp);
        }
    }
    __malloc_unlock(_REENT);
}

/* The SDK's scheduler switch: libogc preempts by priority itself */
static s32 s_scheduler_off;

s32 OSDisableScheduler(void)
{
    return s_scheduler_off++;
}

s32 OSEnableScheduler(void)
{
    return s_scheduler_off > 0 ? s_scheduler_off-- : 0;
}
