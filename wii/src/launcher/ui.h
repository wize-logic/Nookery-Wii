/* The launcher's screens (wii/src/launcher): the look and the pieces they
 * share, over gfx.c and input.c */
#ifndef NK_LAUNCHER_UI_H
#define NK_LAUNCHER_UI_H

#include "gfx.h"
#include "input.h"

#define NK_VERSION "0.1.0"

/* The look: an evening in town */
#define UI_BG_TOP 0x123A2E
#define UI_BG_BOTTOM 0x0B2220
#define UI_PANEL 0x1E4D3E
#define UI_PANEL_LIGHT 0x2C6B55
#define UI_LEAF 0x8FD16A
#define UI_SAND 0xF3DFA2
#define UI_RED 0xE5634B
#define UI_WHITE 0xF6F4EC
#define UI_GREY 0xA8BDB2
#define UI_DARK 0x0A1A16

/* The screen's frame: the title, a heading on the right, the hints below */
void ui_background(const char* heading);
void ui_footer(const char* hints);
/* The Wii Remote's pointer over everything, then the frame */
void ui_present(void);
/* The left edge of the 640-pixel column (the canvas is wider on 16:9) */
int ui_left(void);

/* A list of rows at y = top + i * pitch (rows h high, x from x0 to x1 in the
 * column): the row the pointer is on, or -1 */
int ui_pointed_row(int top, int pitch, int h, int count, int x0, int x1);

/* A menu: draws the items, moves the cursor, returns the index chosen with A
 * (or pointed and A), -1 for B, -2 while nothing was chosen. `cursor` is
 * kept by the caller. `detail` (may be NULL) gives an item's right column. */
typedef const char* (*UiDetailFn)(int item, void* user);
int ui_menu(const char* const* items, int count, int* cursor, int top, UiDetailFn detail,
            void* user, unsigned int pressed);
/* The same with rows `pitch` apart (ui_menu's are 46): for longer lists */
int ui_menu_rows(const char* const* items, int count, int* cursor, int top, int pitch, UiDetailFn detail,
                 void* user, unsigned int pressed);

/* A message until A or B; returns 1 for A */
int ui_message(const char* heading, const char* text, const char* hints);

/* The on-screen keyboard: edits `buf` (`size` bytes with its NUL); returns
 * 1 when confirmed, 0 when cancelled. `secret` shows dots. */
int ui_keyboard(const char* heading, const char* prompt, char* buf, int size, int secret);

/* The screens */
void ui_settings(void);

#endif
