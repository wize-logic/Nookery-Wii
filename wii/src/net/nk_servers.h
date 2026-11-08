/* The lobby servers (the signaling servers): a list in a text file on the SD
 * card, sd:/apps/nookery/lobby_servers.txt, one address a line ("name" or
 * "203.0.113.7", with ":port" when it isn't 28910; '#' starts a comment),
 * that players edit on a computer or in the launcher (Online > Lobby
 * servers). Nookery's browser lists the lobbies of every server on it, and a
 * hosted town is announced on every one. Without the file, the default list
 * (NK_SERVERS_DEFAULT, as the release's lobby_servers.txt has it). */
#ifndef NK_SERVERS_H
#define NK_SERVERS_H

#define NK_SERVERS_PATH "sd:/apps/nookery/lobby_servers.txt"
#define NK_SERVERS_MAX 8
#define NK_SERVER_NAME_LEN 96

/* The default list, one address a line (wii/release/apps/nookery/lobby_servers.txt
 * has the same): none, so only the local network is searched until a server is added */
#define NK_SERVERS_DEFAULT ""

/* Reads the file (the default list when there is none; the settings' old
 * single lobby_server is kept on it) */
void nk_servers_load(void);
int nk_servers_count(void);
const char* nk_servers_get(int i);
/* Adds an address (trimmed; not twice): 0, or -1 when the list is full or
 * the address empty or already there. Saves the file. */
int nk_servers_add(const char* address);
/* Removes one and saves the file */
void nk_servers_remove(int i);
/* Back to the default list, saved */
void nk_servers_reset(void);
/* The list as nk_lobby_start takes it: the addresses, one a line */
const char* nk_servers_joined(void);

#endif
