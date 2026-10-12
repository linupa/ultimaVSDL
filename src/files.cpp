/* files.cpp - finding the original game's data files. */
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "files.h"
#include "lzw.h"
#include "pictures.h"
#include "strings.h"

static char game_dir[512] = ".";

void files_set_game_dir(const char *dir)
{
    strncpy(game_dir, dir, sizeof game_dir - 1);
    game_dir[sizeof game_dir - 1] = '\0';
}

static char save_dir[512];

void files_set_save_dir(const char *dir)
{
    strncpy(save_dir, dir, sizeof save_dir - 1);
    save_dir[sizeof save_dir - 1] = '\0';
}

static FILE *open_in(const char *dir, const char *name, const char *mode);

FILE *game_fopen(const char *name, const char *mode)
{
    if (save_dir[0]) {
        if (strchr(mode, 'w') || strchr(mode, '+')) {   /* writes: upper-case name in the save dir */
            char path[640];
            snprintf(path, sizeof path, "%s/%s", save_dir, name);
            for (char *p = path + strlen(save_dir) + 1; *p; p++) *p = (char)toupper((unsigned char)*p);
            FILE *f = fopen(path, mode);
            if (!f && strchr(mode, '+')) f = fopen(path, "w+b");
            return f;
        }
        FILE *f = open_in(save_dir, name, mode);
        if (f) return f;
    }
    return open_in(game_dir, name, mode);
}

static FILE *open_in(const char *dir, const char *name, const char *mode)
{
    char path[640], *base;
    for (int pass = 0; pass < 3; pass++) {
        snprintf(path, sizeof path, "%s/%s", dir, name);
        base = path + strlen(dir) + 1;
        for (char *p = base; *p; p++)
            if (pass == 1) *p = (char)toupper((unsigned char)*p);
            else if (pass == 2) *p = (char)tolower((unsigned char)*p);
        FILE *f = fopen(path, mode);
        if (f) return f;
    }
    return NULL;
}

unsigned char *game_load_file(const char *name, size_t *size)
{
    if (pictures.has(name)) return lzw_load_file(name, size);
    FILE *f = game_fopen(name, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = len > 0 ? (unsigned char *)malloc((size_t)len) : NULL;
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) *size = (size_t)len;
    return buf;
}
