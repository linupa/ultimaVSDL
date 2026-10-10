/* files.h - finding the original game's data files. */
#ifndef U5_FILES_H
#define U5_FILES_H

#include <stdio.h>

/* Directory holding the game files (ULTIMA.EXE, DATA.OVL, IBM.CH, ...). Default ".". */
void files_set_game_dir(const char *dir);

/* Open a game file by its DOS name. The game uses lower-case names ("ibm.ch") and the files are
 * upper case on disk, so on a case-sensitive file system the name is tried as given, then upper
 * case, then lower case. Returns NULL if none exists. */
FILE *game_fopen(const char *name, const char *mode);

/* Directory for files the game writes (SAVED.GAM, SAVED.OOL, BRIT.OOL, UNDER.OOL). If set, writes go
 * there and reads look there first, so the original game directory is never modified. If not set,
 * writes go to the game directory, as the original does. */
void files_set_save_dir(const char *dir);

/* Whole file into a new malloc'd buffer. Files named in the game's picture table (ds:25EA) are
 * LZW-compressed and are decompressed, the rest are read as they are (load_file_or_die, 125d). */
unsigned char *game_load_file(const char *name, size_t *size);

#endif
