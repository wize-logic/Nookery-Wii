#include "version.h"

#ifdef ACGC_PORT
char __Creator__[0xC];
#else
char __Creator__[] = {
#include "assets/__Creator__.inc"
};
#endif

#ifdef ACGC_PORT
char __DateTime__[0x12];
#else
char __DateTime__[] = {
#include "assets/__DateTime__.inc"
};
#endif


