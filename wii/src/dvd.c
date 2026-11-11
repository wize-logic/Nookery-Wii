/* The SDK's DVD on the player's own copy of the disc: a GameCube image
 * (.iso/.gcm, or a compact .ciso) on the SD card or a USB drive, opened with
 * wiilib's GameCube disc reader (wl/gcm) and read through its file API
 * (wl/platform).
 *
 * Reads are queued to an IO thread that runs above the game's threads; it is
 * woken, and a read's callback called, from interrupt context
 * (nk_wii_defer), as the SDK's DVD interrupt would. */

#include "nk_wii.h"
#include "ogc_bridge.h"

#include <ctype.h>
#include <dirent.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dolphin/dvd.h>
#include <dolphin/os.h>

#include <wl/base/base.h>
#include <wl/gcm/gcm.h>
#include <wl/platform/platform.h>

/* Where the image is looked for first; then any .iso/.gcm/.ciso in the
 * folders below that is Animal Crossing (GAFE01) */
static const char* const s_image_paths[] = {
    NK_WII_DIR "/game.iso", NK_WII_DIR "/game.gcm", NK_WII_DIR "/game.ciso",
    "sd:/games/GAFE01.iso", "sd:/games/GAFE01.gcm", "sd:/games/GAFE01.ciso",
    NK_WII_USB_DIR "/game.iso", "usb:/games/GAFE01.iso", "usb:/games/GAFE01.ciso",
};
static const char* const s_scan_dirs[] = {
    NK_WII_DIR, "sd:/games", "sd:/", NK_WII_USB_DIR, "usb:/games", "usb:/",
};

/* The image file, and for a CISO its block map */
typedef struct ImageSource {
    WlPlatformFile* file;
    u32 ciso_block; /* 0: a plain image */
    u32 ciso_blocks;
    u32* ciso_index; /* per block: its data's index + 1, 0 when absent */
} ImageSource;

static ImageSource s_src;
static WlGcm s_gcm;
static DVDDiskID s_disk_id ATTRIBUTE_ALIGN(32);
static char s_image_path[160];

/* The SDK's DiskID at 0x80000000 on a GameCube: a variable here (dvd.h,
 * ACGC_PORT) the port fills from the image */
DVDDiskID DiskID ATTRIBUTE_ALIGN(32);

/* The FST, as the SDK keeps it: 12-byte entries, then the names */
static const u8* s_fst;
static u32 s_entries;
static const char* s_names;

/* The queue of reads (linked by their command blocks) and the thread */
static DVDCommandBlock* s_head;
static DVDCommandBlock* s_tail;
static DVDCommandBlock* volatile s_current;
static volatile BOOL s_stopping; /* leaving: no more reads (nk_wii_dvd_stop) */
static DVDCommandBlock* s_done_head;
static DVDCommandBlock* s_done_tail;
static ogc_KThrQueue s_wait;
static NkWiiDefer s_kick;
static NkWiiDefer s_complete;
static ogc_lwp_t s_thread;
static u8 s_stack[32 * 1024] ATTRIBUTE_ALIGN(32);

#define CISO_HEADER 0x8000u

/* How much the game has read from the image (the walk test logs it) */
unsigned nk_wii_dvd_reads, nk_wii_dvd_kb, nk_wii_dvd_ms;

static void* io_thread(void* arg);
static void kick(void* arg);
static void complete(void* arg);

static u32 le32(const u8* p)
{
    return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24);
}

static u32 be32(const u8* p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

static WlResult read_image(void* ctx, uint64_t off, void* buf, size_t n)
{
    ImageSource* src = ctx;
    if (src->ciso_block == 0) {
        return wl_platform_fs_read_at(src->file, off, buf, (uint32_t) n);
    }
    u8* dst = buf;
    while (n > 0) {
        u32 block = (u32) (off / src->ciso_block);
        u32 within = (u32) (off % src->ciso_block);
        u32 chunk = src->ciso_block - within;
        if (chunk > n) {
            chunk = (u32) n;
        }
        if (block >= src->ciso_blocks || src->ciso_index[block] == 0) {
            memset(dst, 0, chunk);
        } else {
            u64 at = CISO_HEADER + (u64) (src->ciso_index[block] - 1) * src->ciso_block + within;
            WlResult r = wl_platform_fs_read_at(src->file, at, dst, chunk);
            if (r != WL_OK) {
                return r;
            }
        }
        dst += chunk;
        off += chunk;
        n -= chunk;
    }
    return WL_OK;
}

static void source_close(ImageSource* src)
{
    if (src->file != NULL) {
        wl_platform_fs_close(src->file);
    }
    free(src->ciso_index);
    memset(src, 0, sizeof(*src));
}

/* A .ciso: "CISO", the block size, then a byte per block (1: stored) */
static BOOL source_open(ImageSource* src, const char* path, u64* size)
{
    WlPlatformFile* file;
    u8 head[8];
    memset(src, 0, sizeof(*src));
    if (!wl_platform_fs_exists(path) || wl_platform_fs_open(path, WL_FILE_READ, &file) != WL_OK) {
        return FALSE;
    }
    src->file = file;
    *size = wl_platform_fs_size(file);
    if (*size > CISO_HEADER && wl_platform_fs_read_at(file, 0, head, sizeof(head)) == WL_OK &&
        memcmp(head, "CISO", 4) == 0)
    {
        u32 block = le32(head + 4);
        u32 blocks = CISO_HEADER - 8;
        u8* map = malloc(blocks);
        if (block == 0 || (block & 0x7FF) != 0 || map == NULL ||
            wl_platform_fs_read_at(file, 8, map, blocks) != WL_OK)
        {
            free(map);
            source_close(src);
            return FALSE;
        }
        src->ciso_index = malloc(blocks * sizeof(u32));
        if (src->ciso_index == NULL) {
            free(map);
            source_close(src);
            return FALSE;
        }
        u32 present = 0, last = 0;
        for (u32 i = 0; i < blocks; i++) {
            src->ciso_index[i] = map[i] ? ++present : 0;
            if (map[i]) {
                last = i + 1;
            }
        }
        free(map);
        src->ciso_block = block;
        src->ciso_blocks = blocks;
        /* a GameCube disc's size, the blocks past the last one zeros */
        *size = (u64) last * block;
        if (*size < 0x57058000ull) {
            *size = 0x57058000ull;
        }
    }
    return TRUE;
}

static BOOL open_path(const char* path, BOOL quiet)
{
    ImageSource src;
    u64 size;
    if (!source_open(&src, path, &size)) {
        return FALSE;
    }
    s_src = src;
    WlResult r = wl_gcm_open_src(&s_gcm, read_image, NULL, &s_src, size);
    if (r != WL_OK) {
        if (!quiet) {
            OSReport("DVD: %s is not a GameCube image (%s)\n", path, wl_result_name(r));
        }
        source_close(&s_src);
        return FALSE;
    }
    if (memcmp(s_gcm.game_code, "GAFE", 4) != 0) {
        if (!quiet) {
            OSReport("DVD: %s is %.4s, not Animal Crossing (GAFE01)\n", path, s_gcm.game_code);
        }
        wl_gcm_close(&s_gcm);
        source_close(&s_src);
        return FALSE;
    }
    snprintf(s_image_path, sizeof(s_image_path), "%s", path);
    return TRUE;
}

static BOOL has_image_suffix(const char* name)
{
    size_t n = strlen(name);
    static const char* const suffixes[] = {".iso", ".gcm", ".ciso"};
    for (size_t i = 0; i < (sizeof(suffixes) / sizeof(suffixes[0])); i++) {
        size_t k = strlen(suffixes[i]);
        if (n > k && strcasecmp(name + n - k, suffixes[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL scan_dir(const char* dir)
{
    DIR* d = opendir(dir);
    if (d == NULL) {
        return FALSE;
    }
    struct dirent* e;
    BOOL found = FALSE;
    while (!found && (e = readdir(d)) != NULL) {
        char path[160];
        if (e->d_name[0] == '.' || !has_image_suffix(e->d_name)) {
            continue;
        }
        size_t len = strlen(dir);
        snprintf(path, sizeof(path), "%s%s%s", dir, (len > 0 && dir[len - 1] == '/') ? "" : "/",
                 e->d_name);
        found = open_path(path, TRUE);
    }
    closedir(d);
    return found;
}

static BOOL open_image(void)
{
    if (s_src.file != NULL) {
        return TRUE;
    }
    if (!wl_platform_fs_mounted(WL_DEVICE_USB)) {
        wl_platform_fs_mount(WL_DEVICE_USB);
    }
    for (size_t i = 0; i < (sizeof(s_image_paths) / sizeof(s_image_paths[0])); i++) {
        if (open_path(s_image_paths[i], FALSE)) {
            return TRUE;
        }
    }
    for (size_t i = 0; i < (sizeof(s_scan_dirs) / sizeof(s_scan_dirs[0])); i++) {
        if (scan_dir(s_scan_dirs[i])) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The start (main.c): the image, before anything reads it. 0, or -1 */
int nk_wii_disc_open(void)
{
    if (s_fst != NULL) {
        return 0;
    }
    if (!open_image()) {
        return -1;
    }
    if (wl_gcm_read(&s_gcm, 0, &s_disk_id, sizeof(s_disk_id)) != WL_OK) {
        return -1;
    }
    DiskID = s_disk_id;
    /* the loader's ID in low memory, as a GameCube's boot leaves it */
    memcpy((void*) 0x80000000, &s_disk_id, sizeof(s_disk_id));
    DCFlushRange((void*) 0x80000000, sizeof(s_disk_id));
    s_fst = s_gcm.fst;
    s_entries = s_gcm.entry_count;
    s_names = (const char*) s_fst + s_entries * 12;
    OSReport("DVD: %s (%.6s rev %d), %u FST entries%s\n", s_image_path, s_disk_id.gameName,
             s_disk_id.gameVersion, (unsigned) s_entries, s_src.ciso_block ? ", CISO" : "");
    if (s_disk_id.gameVersion != 0) {
        OSReport("DVD: revision %d; Nookery is built for revision 0\n", s_disk_id.gameVersion);
    }
    return 0;
}

const char* nk_wii_disc_path(void)
{
    return s_image_path;
}

void DVDInit(void)
{
    static BOOL started;
    if (started) {
        return;
    }
    started = TRUE;
    if (nk_wii_disc_open() != 0) {
        nk_wii_fatal("No copy of Animal Crossing was found.\n\n"
                     "Put your own dump of the game (NTSC-U, GAFE01) on the SD\n"
                     "card or a USB drive as\n"
                     "  " NK_WII_DIR "/game.iso  or  /games/GAFE01.iso\n"
                     "(.iso, .gcm and .ciso images work)");
    }
    s_kick.fn = kick;
    s_complete.fn = complete;
    LWP_CreateThread(&s_thread, io_thread, NULL, s_stack, sizeof(s_stack), NK_WII_PRIO_IO);
}

/* pc/src/pc_assets.c (Aurora's call on PC): the disc's main.dol, whole */
static u8* s_dol;
static u32 s_dol_size;

const u8* DVDGetDOLLocation(s32* size)
{
    if (s_dol == NULL) {
        uint8_t* data;
        size_t len;
        if (nk_wii_disc_open() != 0 || wl_gcm_read_dol(&s_gcm, &data, &len) != WL_OK) {
            *size = 0;
            return NULL;
        }
        s_dol = data;
        s_dol_size = (u32) len;
    }
    *size = (s32) s_dol_size;
    return s_dol;
}

/* main.c: once the game's tables are copied out of it */
void nk_wii_disc_release_dol(void)
{
    free(s_dol);
    s_dol = NULL;
    s_dol_size = 0;
}

/* dvdfs.c: the names compare without case, '/' separates, "." and ".." */
static s32 s_current_dir;

static BOOL is_dir(u32 entry)
{
    return s_fst[entry * 12] != 0;
}

static const char* name_of(u32 entry)
{
    return s_names + (be32(&s_fst[entry * 12]) & 0xFFFFFF);
}

static u32 parent_of(u32 entry)
{
    return be32(&s_fst[entry * 12 + 4]);
}

static u32 next_of(u32 entry)
{
    return be32(&s_fst[entry * 12 + 8]);
}

static BOOL same_name(const char* path, size_t len, const char* name)
{
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '\0' || tolower((unsigned char) path[i]) != tolower((unsigned char) name[i])) {
            return FALSE;
        }
    }
    return name[len] == '\0';
}

static s32 convert_path(const char* path)
{
    u32 dir = s_current_dir;
    if (s_fst == NULL || path == NULL) {
        return -1;
    }
    while (*path != '\0') {
        if (*path == '/') {
            dir = 0;
            path++;
            continue;
        }
        size_t len = 0;
        while (path[len] != '\0' && path[len] != '/') {
            len++;
        }
        if (len == 1 && path[0] == '.') {
            /* stays */
        } else if (len == 2 && path[0] == '.' && path[1] == '.') {
            dir = parent_of(dir);
        } else {
            u32 found = 0;
            u32 end = next_of(dir);
            for (u32 e = dir + 1; e < end && e < s_entries; e = is_dir(e) ? next_of(e) : e + 1) {
                if (same_name(path, len, name_of(e))) {
                    found = e;
                    break;
                }
            }
            if (found == 0) {
                return -1;
            }
            if (path[len] == '\0') {
                return found;
            }
            if (!is_dir(found)) {
                return -1;
            }
            dir = found;
        }
        path += len;
        if (*path == '/') {
            path++;
        }
    }
    return dir;
}

s32 DVDConvertPathToEntrynum(const char* path)
{
    s32 entry = convert_path(path);
    if (entry < 0) {
        OSReport("DVD: no file \"%s\" on the disc\n", path != NULL ? path : "(null)");
    }
    return entry;
}

BOOL DVDChangeDir(const char* dirName)
{
    s32 entry = convert_path(dirName);
    if (entry < 0 || !is_dir(entry)) {
        return FALSE;
    }
    s_current_dir = entry;
    return TRUE;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo)
{
    if (entrynum <= 0 || (u32) entrynum >= s_entries || is_dir(entrynum)) {
        return FALSE;
    }
    fileInfo->startAddr = be32(&s_fst[entrynum * 12 + 4]);
    fileInfo->length = be32(&s_fst[entrynum * 12 + 8]);
    fileInfo->callback = NULL;
    fileInfo->cb.state = DVD_STATE_END;
    return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo)
{
    s32 entry = DVDConvertPathToEntrynum(fileName);
    return entry >= 0 && DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo)
{
    (void) fileInfo;
    return TRUE;
}

int DVDOpenDir(const char* dirName, DVDDir* dir)
{
    s32 entry = convert_path(dirName);
    if (entry < 0 || !is_dir(entry)) {
        return FALSE;
    }
    dir->entryNum = entry;
    dir->location = entry + 1;
    dir->next = next_of(entry);
    return TRUE;
}

BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirent)
{
    u32 loc = dir->location;
    if (loc <= dir->entryNum || loc >= dir->next || loc >= s_entries) {
        return FALSE;
    }
    dirent->entryNum = loc;
    dirent->isDir = is_dir(loc);
    dirent->name = (char*) name_of(loc);
    dir->location = is_dir(loc) ? next_of(loc) : loc + 1;
    return TRUE;
}

BOOL DVDCloseDir(DVDDir* dir)
{
    (void) dir;
    return TRUE;
}

static void dvd_callback(s32 result, DVDCommandBlock* block)
{
    DVDFileInfo* fileInfo = (DVDFileInfo*) block;
    if (fileInfo->callback != NULL) {
        fileInfo->callback(result, fileInfo);
    }
}

static void queue_read(DVDCommandBlock* block, void* addr, s32 length, u32 disc_offset,
                       DVDCBCallback callback)
{
    block->next = NULL;
    block->prev = NULL;
    block->command = 1; /* read */
    block->state = DVD_STATE_WAITING;
    block->offset = disc_offset;
    block->length = length;
    block->addr = addr;
    block->currTransferSize = 0;
    block->transferredSize = 0;
    block->callback = callback;

    BOOL enabled = OSDisableInterrupts();
    if (s_tail != NULL) {
        s_tail->next = block;
        block->prev = s_tail;
    } else {
        s_head = block;
    }
    s_tail = block;
    nk_wii_defer(&s_kick);
    OSRestoreInterrupts(enabled);
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                      DVDCallback callback, s32 prio)
{
    (void) prio;
    fileInfo->callback = callback;
    queue_read(&fileInfo->cb, addr, length, fileInfo->startAddr + offset, dvd_callback);
    return TRUE;
}

BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset,
                         DVDCBCallback callback, s32 prio)
{
    (void) prio;
    queue_read(block, addr, length, offset, callback);
    return TRUE;
}

/* The synchronous read: queued as the others, the caller sleeping until it
 * is done (on the game's thread, never in interrupt context) */
static OSThreadQueue s_sync_queue;

static void sync_callback(s32 result, DVDCommandBlock* block)
{
    (void) result;
    (void) block;
    OSWakeupThread(&s_sync_queue);
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio)
{
    (void) prio;
    DVDCommandBlock* block = &fileInfo->cb;
    fileInfo->callback = NULL;
    BOOL enabled = OSDisableInterrupts();
    queue_read(block, addr, length, fileInfo->startAddr + offset, sync_callback);
    while (block->state == DVD_STATE_WAITING || block->state == DVD_STATE_BUSY) {
        OSSleepThread(&s_sync_queue);
    }
    s32 state = block->state;
    OSRestoreInterrupts(enabled);
    return state == DVD_STATE_END ? (s32) block->transferredSize : DVD_RESULT_FATAL_ERROR;
}

/* Leaving (ogc/main.c): the read in progress finished, no other started, so that the card is unmounted under no
 * read */
void nk_wii_dvd_stop(void)
{
    s_stopping = TRUE;
    for (int i = 0; i < 100 && s_current != NULL; i++) {
        usleep(10000);
    }
}

/* Interrupt context: the IO thread, woken here, runs once it returns */
static void kick(void* arg)
{
    (void) arg;
    KThrQueueUnblockAllByValue(&s_wait, (uintptr_t) &s_head);
}

/* Interrupt context: the finished reads' callbacks */
static void complete(void* arg)
{
    (void) arg;
    while (s_done_head != NULL) {
        DVDCommandBlock* block = s_done_head;
        s_done_head = block->next;
        if (s_done_head == NULL) {
            s_done_tail = NULL;
        }
        block->next = NULL;
        s32 result = block->state == DVD_STATE_END ? (s32) block->length : DVD_RESULT_FATAL_ERROR;
        if (block->callback != NULL) {
            block->callback(result, block);
        }
    }
}

static void* io_thread(void* arg)
{
    (void) arg;
    for (;;) {
        BOOL enabled = OSDisableInterrupts();
        while (s_head == NULL || s_stopping) {
            KThrQueueBlock(&s_wait, (uintptr_t) &s_head);
        }
        DVDCommandBlock* block = s_head;
        s_head = block->next;
        if (s_head == NULL) {
            s_tail = NULL;
        } else {
            s_head->prev = NULL;
        }
        block->next = NULL;
        block->state = DVD_STATE_BUSY;
        s_current = block;
        OSRestoreInterrupts(enabled);

        u32 length = block->length;
        if ((u64) block->offset + length > s_gcm.size) {
            length = block->offset < s_gcm.size ? (u32) (s_gcm.size - block->offset) : 0;
        }
        OSTime read_start = OSGetTime();
        WlResult r = length == 0 ? WL_OK : read_image(&s_src, block->offset, block->addr, length);
        static u32 read_us;
        read_us += (u32) OSTicksToMicroseconds(OSGetTime() - read_start);
        nk_wii_dvd_ms += read_us / 1000;
        read_us %= 1000;
        nk_wii_dvd_reads++;
        nk_wii_dvd_kb += (length + 1023) / 1024;
        /* The SDK's DMA writes memory under the cache: the game may
         * invalidate the lines over the buffer after the read */
        DCFlushRange(block->addr, block->length);

        if (r != WL_OK) {
            OSReport("DVD: read of 0x%x bytes at 0x%x failed (%s)\n", (unsigned) block->length,
                     (unsigned) block->offset, wl_result_name(r));
        }
        enabled = OSDisableInterrupts();
        s_current = NULL;
        if (r == WL_OK) {
            block->transferredSize = block->length;
            block->state = DVD_STATE_END;
        } else {
            block->state = DVD_STATE_FATAL_ERROR;
        }
        if (s_done_tail != NULL) {
            s_done_tail->next = block;
        } else {
            s_done_head = block;
        }
        s_done_tail = block;
        nk_wii_defer(&s_complete);
        OSRestoreInterrupts(enabled);
    }
    return NULL;
}

s32 DVDGetDriveStatus(void)
{
    BOOL enabled = OSDisableInterrupts();
    s32 status = (s_head != NULL || s_current != NULL || s_done_head != NULL) ? DVD_STATE_BUSY
                                                                             : DVD_STATE_END;
    OSRestoreInterrupts(enabled);
    return status;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block)
{
    return block->state;
}

s32 DVDCancel(volatile DVDCommandBlock* block_v)
{
    DVDCommandBlock* block = (DVDCommandBlock*) block_v;
    BOOL enabled = OSDisableInterrupts();
    if (block->state == DVD_STATE_WAITING) {
        if (block->prev != NULL) {
            block->prev->next = block->next;
        } else if (s_head == block) {
            s_head = block->next;
        }
        if (block->next != NULL) {
            block->next->prev = block->prev;
        } else if (s_tail == block) {
            s_tail = block->prev;
        }
        block->next = block->prev = NULL;
        block->state = DVD_STATE_CANCELED;
    }
    OSRestoreInterrupts(enabled);
    return 0;
}

BOOL DVDCheckDisk(void)
{
    return TRUE;
}

DVDDiskID* DVDGetCurrentDiskID(void)
{
    return &s_disk_id;
}

/* Streaming audio from the disc (the drive's own decoder) is a GameCube
 * drive's: there is none in Wii mode */
BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, DVDCallback callback)
{
    (void) fileInfo;
    (void) length;
    (void) offset;
    (void) callback;
    return FALSE;
}

BOOL DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback callback)
{
    (void) block;
    if (callback != NULL) {
        callback(0, block);
    }
    return TRUE;
}

s32 DVDCancelStream(DVDCommandBlock* block)
{
    (void) block;
    return 0;
}

/* Bytes of the disc image (net code: the lobby's file checks) */
int nk_wii_disc_read(unsigned int offset, void* buf, unsigned int len)
{
    if (s_src.file == NULL || (u64) offset + len > s_gcm.size) {
        return -1;
    }
    return read_image(&s_src, offset, buf, len) == WL_OK ? 0 : -1;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo)
{
    return fileInfo->cb.state;
}
