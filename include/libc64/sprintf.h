#ifndef SPRINTF_H
#define SPRINTF_H

#ifdef ACGC_PORT
#include <stdio.h>
#else

#ifdef __cplusplus
extern "C" {
#endif

int sprintf(char* dst, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* !ACGC_PORT */

#endif