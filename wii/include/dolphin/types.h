/* Aurora's SDK types (extern/aurora/include/dolphin/types.h), with what its
 * console branch leaves to the game's own headers: BOOL, TRUE and FALSE (the
 * game's include/types.h defines them the same way), bool for GXAurora.h,
 * and the hosted C library's NULL and size_t */
#ifndef NK_WII_DOLPHIN_TYPES_H
#define NK_WII_DOLPHIN_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include_next <dolphin/types.h>
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif
