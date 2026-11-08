/* The SDK's interrupt handlers (GX's PE token, PE finish and CP FIFO
 * handlers) on libogc's dispatcher. The SDK numbers the interrupts its own
 * way; libogc 3.1 (tuxedo) numbers them in the order of its dispatch.
 *
 * This file includes libogc's headers and not the SDK's: the SDK's types are
 * spelled out (an __OSInterrupt is an s16, a handler takes it and an
 * OSContext*). */

#include <stddef.h>
#include <stdint.h>

#include <tuxedo/interrupt.h>
#include <tuxedo/tick.h>

#include "nk_wii.h"

typedef int16_t SdkInterrupt;
typedef void (*SdkInterruptHandler)(SdkInterrupt interrupt, void* context);

#define SDK_INTERRUPT_COUNT 27

/* Index: the SDK's __OS_INTERRUPT_*; value: libogc's KIrqId */
static const uint8_t s_irq_of[SDK_INTERRUPT_COUNT] = {
    IRQ_MEM0,       IRQ_MEM1,      IRQ_MEM2,         IRQ_MEM3,
    IRQ_MEMADDRESS, IRQ_DSP_AI,    IRQ_DSP_ARAM,     IRQ_DSP_DSP,
    IRQ_AI,         IRQ_EXI0_EXI,  IRQ_EXI0_TC,      IRQ_EXI0_EXT,
    IRQ_EXI1_EXI,   IRQ_EXI1_TC,   IRQ_EXI1_EXT,     IRQ_EXI2_EXI,
    IRQ_EXI2_TC,    IRQ_PI_CP,     IRQ_PI_PETOKEN,   IRQ_PI_PEFINISH,
    IRQ_PI_SI,      IRQ_PI_DI,     IRQ_PI_RSW,       IRQ_PI_ERROR,
    IRQ_PI_VI,      IRQ_PI_DEBUG,  IRQ_PI_HSP,
};

static SdkInterruptHandler s_handlers[SDK_INTERRUPT_COUNT];

/* The OSContext the SDK's handlers are given: they only pass it back to
 * OSSetCurrentContext (os.c) */
static uint8_t s_context[0x2C8] __attribute__((aligned(8)));

static void trampoline(KIrqId id, void* user)
{
    (void) id;
    SdkInterrupt interrupt = (SdkInterrupt) (uintptr_t) user;
    SdkInterruptHandler handler = s_handlers[interrupt];
    if (handler != NULL) {
        handler(interrupt, s_context);
    }
}

SdkInterruptHandler __OSSetInterruptHandler(SdkInterrupt interrupt,
                                            SdkInterruptHandler handler)
{
    if (interrupt < 0 || interrupt >= SDK_INTERRUPT_COUNT) {
        return NULL;
    }
    SdkInterruptHandler old = s_handlers[interrupt];
    s_handlers[interrupt] = handler;
    KIrqSet((KIrqId) s_irq_of[interrupt], handler != NULL ? trampoline : NULL,
            (void*) (uintptr_t) interrupt);
    return old;
}

SdkInterruptHandler __OSGetInterruptHandler(SdkInterrupt interrupt)
{
    if (interrupt < 0 || interrupt >= SDK_INTERRUPT_COUNT) {
        return NULL;
    }
    return s_handlers[interrupt];
}

/* An SDK mask (0x80000000 >> its number) as libogc's (PPCBIT(KIrqId)) */
static uint32_t ogc_mask(uint32_t sdk_mask)
{
    uint32_t mask = 0;
    for (int i = 0; i < SDK_INTERRUPT_COUNT; i++) {
        if (sdk_mask & (0x80000000u >> i)) {
            mask |= IRQMASK(s_irq_of[i]);
        }
    }
    return mask;
}

static uint32_t s_masked;

uint32_t __OSMaskInterrupts(uint32_t mask)
{
    uint32_t old = s_masked;
    s_masked |= mask;
    KIrqDisable(ogc_mask(mask));
    return old;
}

uint32_t __OSUnmaskInterrupts(uint32_t mask)
{
    uint32_t old = s_masked;
    s_masked &= ~mask;
    KIrqEnable(ogc_mask(mask));
    return old;
}

/* nk_wii.h's deferred work */
_Static_assert(sizeof(((NkWiiDefer*) 0)->task) >= sizeof(KTickTask),
               "NkWiiDefer holds a KTickTask");

static void defer_run(KTickTask* task)
{
    NkWiiDefer* defer = (NkWiiDefer*) task;
    defer->armed = 0;
    defer->fn(defer->arg);
}

void nk_wii_defer(NkWiiDefer* defer)
{
    PPCIrqState level = PPCIrqLockByMsr();
    if (!defer->armed) {
        defer->armed = 1;
        /* a microsecond on: the decrementer's interrupt, not this call */
        KTickTaskStart((KTickTask*) defer->task, defer_run, PPCUsToTicks(1), 0);
    }
    PPCIrqUnlockByMsr(level);
}
