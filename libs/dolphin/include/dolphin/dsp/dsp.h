#ifndef DSP_DSP_H
#define DSP_DSP_H

#include "types.h"

#ifdef __cplusplus
extern "C"{
#endif

#ifdef __MWERKS__
extern volatile u16 __DSPRegs[] AT_ADDRESS(0xCC005000);
#endif

#ifdef __cplusplus
}
#endif

#endif
