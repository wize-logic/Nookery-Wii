/* The launcher's look and its shared pieces: the frame, menus, messages and
 * the on-screen keyboard (for lobby names, passwords and codes) */

#include "ui.h"

#include <gccore.h>
#include <stdio.h>
#include <string.h>

int ui_left(void)
{
    return (gfx_width() - 640) / 2;
}

void ui_background(const char* heading)
{
    int x = ui_left();
    gfx_gradient_v(0, 0, gfx_width(), gfx_height(), UI_BG_TOP, UI_BG_BOTTOM);
    /* a few leaves, for the town */
    for (int i = 0; i < 6; i++) {
        int lx = x + 470 + (i * 37) % 150, ly = 22 + (i * 23) % 50;
        gfx_round_rect(lx, ly, 14, 9, 4, UI_LEAF, 60 + i * 12);
    }
    gfx_text(&font_title, x + 40, 28, UI_WHITE, "Nookery");
    gfx_text(&font_small, x + 42, 70, UI_GREY,
             "Animal Crossing for the Wii  \xC2\xB7  online towns  \xC2\xB7  " NK_VERSION);
    if (heading != NULL) {
        int w = gfx_text_width(&font_body_bold, heading);
        gfx_text(&font_body_bold, x + 600 - w, 40, UI_SAND, heading);
    }
    gfx_rect(x + 40, 96, 560, 2, UI_LEAF, 200);
}

void ui_footer(const char* hints)
{
    int x = ui_left();
    gfx_rect(0, 444, gfx_width(), 36, UI_DARK, 180);
    gfx_text(&font_small, x + 40, 452, UI_GREY, hints);
}

void ui_present(void)
{
    int px, py;
    if (input_pointer(&px, &py)) {
        int x = px * gfx_width() / 640, y = py;
        gfx_round_rect(x - 9, y - 9, 18, 18, 9, UI_DARK, 200);
        gfx_round_rect(x - 7, y - 7, 14, 14, 7, UI_WHITE, 255);
        gfx_round_rect(x - 4, y - 4, 8, 8, 4, UI_LEAF, 255);
    }
    gfx_present();
}

int ui_pointed_row(int top, int pitch, int h, int count, int x0, int x1)
{
    int px, py;
    if (!input_pointer(&px, &py)) {
        return -1;
    }
    px = px * gfx_width() / 640;
    if (px < ui_left() + x0 || px >= ui_left() + x1 || py < top) {
        return -1;
    }
    if ((py - top) % pitch >= h || (py - top) / pitch >= count) {
        return -1;
    }
    return (py - top) / pitch;
}

int ui_menu(const char* const* items, int count, int* cursor, int top, UiDetailFn detail,
            void* user, unsigned int pressed)
{
    return ui_menu_rows(items, count, cursor, top, 46, detail, user, pressed);
}

int ui_menu_rows(const char* const* items, int count, int* cursor, int top, int pitch, UiDetailFn detail,
                 void* user, unsigned int pressed)
{
    const int h = pitch - 8, text_y = (h - 22) / 2;
    int x = ui_left();
    int pointed = ui_pointed_row(top, pitch, h, count, 40, 600);
    if (pointed >= 0) {
        *cursor = pointed;
    }
    if (pressed & IN_UP) {
        *cursor = (*cursor + count - 1) % count;
    }
    if (pressed & IN_DOWN) {
        *cursor = (*cursor + 1) % count;
    }
    for (int i = 0; i < count; i++) {
        int y = top + i * pitch;
        int on = i == *cursor;
        gfx_round_rect(x + 40, y, 560, h, 10, on ? UI_PANEL_LIGHT : UI_PANEL, on ? 255 : 200);
        if (on) {
            gfx_round_rect(x + 40, y, 6, h, 3, UI_LEAF, 255);
        }
        gfx_text(&font_body_bold, x + 62, y + text_y, on ? UI_WHITE : UI_GREY, items[i]);
        if (detail != NULL) {
            const char* d = detail(i, user);
            if (d != NULL) {
                int w = gfx_text_width(&font_body, d);
                gfx_text(&font_body, x + 584 - w, y + text_y, on ? UI_SAND : UI_GREY, d);
            }
        }
    }
    if (pressed & IN_A) {
        return *cursor;
    }
    if (pressed & IN_B) {
        return -1;
    }
    return -2;
}

/* Text wrapped to the column, from y; returns the y after it */
static int wrapped(const Font* f, int x, int y, int width, unsigned int rgb, const char* text)
{
    char line[160];
    const char* p = text;
    while (*p != '\0') {
        int n = 0, last_space = -1;
        while (p[n] != '\0' && p[n] != '\n' && n < (int) sizeof(line) - 1) {
            memcpy(line, p, n + 1);
            line[n + 1] = '\0';
            if (gfx_text_width(f, line) > width) {
                break;
            }
            if (p[n] == ' ') {
                last_space = n;
            }
            n++;
        }
        if (p[n] != '\0' && p[n] != '\n' && last_space > 0) {
            n = last_space;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        gfx_text(f, x, y, rgb, line);
        y += gfx_line_height(f);
        p += n;
        while (*p == ' ') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return y;
}

int ui_message(const char* heading, const char* text, const char* hints)
{
    for (;;) {
        unsigned int pressed = input_poll();
        ui_background(heading);
        gfx_round_rect(ui_left() + 40, 120, 560, 300, 12, UI_PANEL, 220);
        wrapped(&font_body, ui_left() + 62, 140, 516, UI_WHITE, text);
        ui_footer(hints != NULL ? hints : "A: OK    B: Back");
        ui_present();
        if (pressed & IN_A) {
            return 1;
        }
        if (pressed & (IN_B | IN_HOME)) {
            return 0;
        }
    }
}

/* ---- the keyboard ---------------------------------------------------------- */

#define KB_COLS 10
#define KB_ROWS 4
static const char* const s_layouts[2][KB_ROWS] = {
    {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.:"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.?"},
};
/* the bottom row's keys */
enum { KEY_SHIFT, KEY_SPACE, KEY_DELETE, KEY_DONE, KEY_SPECIALS };

int ui_keyboard(const char* heading, const char* prompt, char* buf, int size, int secret)
{
    int row = 1, col = 0, shift = 0;
    char work[128];
    snprintf(work, sizeof(work), "%s", buf);
    if (size > (int) sizeof(work)) {
        size = sizeof(work);
    }
    for (;;) {
        unsigned int pressed = input_poll();
        int x = ui_left();
        const int key_w = 50, key_h = 40, kx = x + 70, ky = 200;
        size_t len = strlen(work);

        /* the pointer */
        int px, py;
        if (input_pointer(&px, &py)) {
            px = px * gfx_width() / 640;
            if (py >= ky && py < ky + (KB_ROWS + 1) * (key_h + 6) && px >= kx && px < kx + KB_COLS * key_w) {
                int r = (py - ky) / (key_h + 6);
                int c = (px - kx) / key_w;
                if (r < KB_ROWS) {
                    row = r;
                    col = c;
                } else {
                    row = KB_ROWS;
                    col = (px - kx) / (KB_COLS * key_w / KEY_SPECIALS);
                }
            }
        }
        if (pressed & IN_UP) {
            row = (row + KB_ROWS) % (KB_ROWS + 1);
        }
        if (pressed & IN_DOWN) {
            row = (row + 1) % (KB_ROWS + 1);
        }
        int cols = row == KB_ROWS ? KEY_SPECIALS : KB_COLS;
        if (col >= cols) {
            col = cols - 1;
        }
        if (pressed & IN_LEFT) {
            col = (col + cols - 1) % cols;
        }
        if (pressed & IN_RIGHT) {
            col = (col + 1) % cols;
        }
        if (pressed & IN_A) {
            if (row < KB_ROWS) {
                if ((int) len < size - 1) {
                    work[len] = s_layouts[shift][row][col];
                    work[len + 1] = '\0';
                }
            } else if (col == KEY_SHIFT) {
                shift ^= 1;
            } else if (col == KEY_SPACE) {
                if ((int) len < size - 1 && len > 0) {
                    work[len] = ' ';
                    work[len + 1] = '\0';
                }
            } else if (col == KEY_DELETE) {
                if (len > 0) {
                    work[len - 1] = '\0';
                }
            } else {
                snprintf(buf, size, "%s", work);
                return 1;
            }
        }
        if (pressed & IN_B) {
            if (len > 0) {
                work[len - 1] = '\0';
            } else {
                return 0;
            }
        }
        if (pressed & IN_X) {
            shift ^= 1;
        }
        if (pressed & IN_START) {
            snprintf(buf, size, "%s", work);
            return 1;
        }
        if (pressed & IN_HOME) {
            return 0;
        }

        ui_background(heading);
        gfx_text(&font_body, x + 40, 112, UI_GREY, prompt);
        gfx_round_rect(x + 40, 144, 560, 40, 8, UI_DARK, 220);
        {
            char shown[130];
            if (secret) {
                size_t n = strlen(work);
                memset(shown, '*', n);
                shown[n] = '\0';
            } else {
                snprintf(shown, sizeof(shown), "%s", work);
            }
            int w = gfx_text(&font_body_bold, x + 54, 152, UI_WHITE, shown);
            gfx_rect(x + 56 + w, 152, 2, 24, UI_LEAF, 255);
        }
        for (int r = 0; r < KB_ROWS; r++) {
            for (int c = 0; c < KB_COLS; c++) {
                int on = r == row && c == col;
                int bx = kx + c * key_w, by = ky + r * (key_h + 6);
                char k[2] = {s_layouts[shift][r][c], '\0'};
                gfx_round_rect(bx, by, key_w - 6, key_h, 8, on ? UI_PANEL_LIGHT : UI_PANEL, 255);
                gfx_text_centered(&font_body_bold, bx + (key_w - 6) / 2, by + 8, on ? UI_SAND : UI_WHITE, k);
            }
        }
        static const char* const specials[KEY_SPECIALS] = {"Shift", "Space", "Delete", "Done"};
        int sw = KB_COLS * key_w / KEY_SPECIALS;
        for (int c = 0; c < KEY_SPECIALS; c++) {
            int on = row == KB_ROWS && c == col;
            int bx = kx + c * sw, by = ky + KB_ROWS * (key_h + 6);
            gfx_round_rect(bx, by, sw - 6, key_h, 8, on ? UI_PANEL_LIGHT : (c == KEY_DONE ? 0x3E7A3A : UI_PANEL),
                           255);
            gfx_text_centered(&font_body_bold, bx + (sw - 6) / 2, by + 8, on ? UI_SAND : UI_WHITE,
                              (c == KEY_SHIFT && shift) ? "SHIFT" : specials[c]);
        }
        ui_footer("A: Type    B: Delete    +/X: Shift    START: Done    HOME: Cancel");
        ui_present();
    }
}
