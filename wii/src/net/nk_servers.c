/* The lobby servers' list: see nk_servers.h */

#include "nk_servers.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <wl/base/base.h>

/* settings.c: the single lobby server of older settings.ini files */
extern char g_nk_lobby_server[96];

static char s_servers[NK_SERVERS_MAX][NK_SERVER_NAME_LEN];
static int s_count;
static int s_loaded;
static char s_joined[NK_SERVERS_MAX * NK_SERVER_NAME_LEN];

static const char s_header[] =
    "# Nookery's lobby servers (signaling servers): one address a line, a name\n"
    "# or an IP address, with :port when it isn't 28910. Nookery's lobby browser\n"
    "# lists the lobbies of every server here, and a town you host is announced\n"
    "# on every one. Lines starting with # are ignored. Edit this file on a\n"
    "# computer, or in the launcher: Online > Lobby servers (which can also put\n"
    "# the default list back).\n";

/* The address in `line`, trimmed (no spaces, no comment): its length */
static int clean(const char* line, char* out, int size)
{
    int n = 0;
    while (*line != '\0' && isspace((unsigned char) *line)) {
        line++;
    }
    if (*line == '#') {
        line = "";
    }
    while (*line != '\0' && *line != '#' && !isspace((unsigned char) *line) && n < size - 1) {
        out[n++] = *line++;
    }
    out[n] = '\0';
    return n;
}

static int find(const char* address)
{
    for (int i = 0; i < s_count; i++) {
        if (strcasecmp(s_servers[i], address) == 0) {
            return i;
        }
    }
    return -1;
}

static void add_quiet(const char* line)
{
    char address[NK_SERVER_NAME_LEN];
    if (s_count < NK_SERVERS_MAX && clean(line, address, sizeof(address)) > 0 && find(address) < 0) {
        snprintf(s_servers[s_count++], NK_SERVER_NAME_LEN, "%s", address);
    }
}

static void add_lines(const char* text)
{
    char line[NK_SERVER_NAME_LEN];
    while (*text != '\0') {
        int n = 0;
        while (*text != '\0' && *text != '\n' && n < (int) sizeof(line) - 1) {
            line[n++] = *text++;
        }
        line[n] = '\0';
        while (*text != '\0' && *text != '\n') {
            text++;
        }
        if (*text == '\n') {
            text++;
        }
        add_quiet(line);
    }
}

static void save(void)
{
    FILE* f = fopen(NK_SERVERS_PATH, "w");
    if (f == NULL) {
        WL_LOG_W("lobby", "could not write %s", NK_SERVERS_PATH);
        return;
    }
    fputs(s_header, f);
    for (int i = 0; i < s_count; i++) {
        fprintf(f, "%s\n", s_servers[i]);
    }
    fclose(f);
}

void nk_servers_load(void)
{
    s_count = 0;
    s_loaded = 1;
    FILE* f = fopen(NK_SERVERS_PATH, "r");
    if (f != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), f) != NULL) {
            add_quiet(line);
        }
        fclose(f);
        WL_LOG_I("lobby", "%d lobby server(s) in %s", s_count, NK_SERVERS_PATH);
        return;
    }
    /* No file yet: the default list, and the server an older settings.ini
     * named */
    add_lines(NK_SERVERS_DEFAULT);
    add_quiet(g_nk_lobby_server);
    save();
}

int nk_servers_count(void)
{
    if (!s_loaded) {
        nk_servers_load();
    }
    return s_count;
}

const char* nk_servers_get(int i)
{
    return i >= 0 && i < nk_servers_count() ? s_servers[i] : "";
}

int nk_servers_add(const char* address)
{
    char clean_address[NK_SERVER_NAME_LEN];
    nk_servers_count();
    if (s_count >= NK_SERVERS_MAX || clean(address, clean_address, sizeof(clean_address)) == 0 ||
        find(clean_address) >= 0)
    {
        return -1;
    }
    snprintf(s_servers[s_count++], NK_SERVER_NAME_LEN, "%s", clean_address);
    save();
    return 0;
}

void nk_servers_remove(int i)
{
    if (i < 0 || i >= nk_servers_count()) {
        return;
    }
    memmove(s_servers[i], s_servers[i + 1], (size_t) (s_count - i - 1) * NK_SERVER_NAME_LEN);
    s_count--;
    save();
}

void nk_servers_reset(void)
{
    s_count = 0;
    s_loaded = 1;
    add_lines(NK_SERVERS_DEFAULT);
    save();
}

const char* nk_servers_joined(void)
{
    s_joined[0] = '\0';
    for (int i = 0; i < nk_servers_count(); i++) {
        strcat(s_joined, s_servers[i]);
        strcat(s_joined, "\n");
    }
    return s_joined;
}
