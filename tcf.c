/* SPDX-License-Identifier: Zlib
 * Copyright (c) 2026 Tbaggerofsteam, Tbaggeroftheuk
 */

#include "tcf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define TCF_MAGIC      "TCF"
#define VERSION        1
#define ENDIANNESS     0
#define SHIFT_BITS     2
#define BUFFER_SIZE    8192

#ifdef _WIN32
  #include <direct.h>
  #include <windows.h>
  #define MKDIR(path) _mkdir(path)
  #define PATH_SEP '\\'
#else
  #include <sys/stat.h>
  #include <dirent.h>
  #define MKDIR(path) mkdir(path, 0755)
  #define PATH_SEP '/'
#endif


static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}


static uint8_t shift_table[256];
static uint8_t unshift_table[256];
static int tables_initialized = 0;

static void init_tables(void)
{
    if (tables_initialized)
        return;

    for (int i = 0; i < 256; i++) {
        shift_table[i] = (uint8_t)(((i << SHIFT_BITS) & 0xFF) |
                                   (i >> (8 - SHIFT_BITS)));
        unshift_table[i] = (uint8_t)((i >> SHIFT_BITS) | 
                                     (i << (8 - SHIFT_BITS)));
    }

    tables_initialized = 1;
}


typedef struct {
    char    *path;
    uint32_t offset;
    uint32_t size;
} tcf_entry;

static tcf_entry *entries = NULL;
static size_t entry_count = 0;
static size_t entry_cap   = 0;

static void add_entry(const char *path, uint32_t size)
{
    if (entry_count == entry_cap) {
        entry_cap = entry_cap ? entry_cap * 2 : 64;
        entries = realloc(entries, entry_cap * sizeof(tcf_entry));
        if (!entries) {
            fprintf(stderr, "Out of memory\n");
            exit(EXIT_FAILURE);
        }
    }

    // Allocate memory for the path
    size_t len = strlen(path) + 1;
    entries[entry_count].path = (char *)malloc(len);
    if (!entries[entry_count].path) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    strcpy(entries[entry_count].path, path);  // copy string
    entries[entry_count].size   = size;
    entries[entry_count].offset = 0;
    entry_count++;
}


int ensure_dirs(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = 0;
            MKDIR(tmp);
            *p = saved;
        }
    }
    return 0;
}

static uint16_t read_u16(FILE *f)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2)
        return 0;
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t read_u32(FILE *f)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
        return 0;
    return (uint32_t)(
        b[0] |
        (b[1] << 8) |
        (b[2] << 16) |
        (b[3] << 24)
    );
}

#ifdef _WIN32

static void walk_dir(const char *base, const char *rel)
{
    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\%s*", base, rel);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;

        char new_rel[MAX_PATH];
        snprintf(new_rel, sizeof(new_rel), "%s%s%s",
                 rel, rel[0] ? "\\" : "", fd.cFileName);

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", base, new_rel);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            walk_dir(base, new_rel);
        } else {
            FILE *f = fopen(full, "rb");
            fseek(f, 0, SEEK_END);
            uint32_t size = (uint32_t)ftell(f);
            fclose(f);

            for (char *p = new_rel; *p; p++)
                if (*p == '\\') *p = '/';

            add_entry(new_rel, size);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

#else 

static void walk_dir(const char *base, const char *rel)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", base, rel);

    DIR *d = opendir(path[0] ? path : base);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char new_rel[1024];
        snprintf(new_rel, sizeof(new_rel), "%s%s%s",
                 rel, rel[0] ? "/" : "", e->d_name);

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", base, new_rel);

        struct stat st;
        if (stat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            walk_dir(base, new_rel);
        } else if (S_ISREG(st.st_mode)) {
            add_entry(new_rel, (uint32_t)st.st_size);
        }
    }
    closedir(d);
}

#endif


int tcf_pack(const char *input_dir, const char *out_path)
{
    init_tables();
    walk_dir(input_dir, "");

    FILE *out = fopen(out_path, "wb");
    if (!out) return TCF_ERR_IO;

    uint8_t header[18] = {0};
    fwrite(header, 1, sizeof(header), out);

    uint8_t buffer[BUFFER_SIZE];
    uint32_t offset = 0;

    for (size_t i = 0; i < entry_count; i++) {
        entries[i].offset = offset;

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", input_dir, entries[i].path);

        FILE *in = fopen(full, "rb");
        if (!in) return TCF_ERR_IO;

        size_t r;
        while ((r = fread(buffer, 1, BUFFER_SIZE, in)) > 0) {
            for (size_t j = 0; j < r; j++)
                buffer[j] = shift_table[buffer[j]];

            fwrite(buffer, 1, r, out);
        }

        fclose(in);
        offset += entries[i].size;
    }

    uint32_t index_offset = (uint32_t)ftell(out);

    for (size_t i = 0; i < entry_count; i++) {
        uint16_t len = (uint16_t)strlen(entries[i].path);
        fwrite(&len, 2, 1, out);
        fwrite(entries[i].path, 1, len, out);
        fwrite(&entries[i].offset, 4, 1, out);
        fwrite(&entries[i].size,   4, 1, out);
    }

    fwrite("EOF", 1, 3, out);

    memcpy(header + 0, TCF_MAGIC, 3);
    header[3] = VERSION;
    header[4] = 0;
    header[5] = ENDIANNESS;

    memcpy(header + 6,  &index_offset, 4);
    memcpy(header + 10, &entry_count,  4);

    uint32_t crc = crc32(header, 14);
    memcpy(header + 14, &crc, 4);

    fseek(out, 0, SEEK_SET);
    fwrite(header, 1, sizeof(header), out);

    fclose(out);
    return TCF_OK;
}


int tcf_extract(const char *tcf_path, const char *output_dir)
{
    init_tables();

    FILE *f = fopen(tcf_path, "rb");
    if (!f)
        return TCF_ERR_IO;

    uint8_t header[18];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return TCF_ERR_FORMAT;
    }

    if (memcmp(header, TCF_MAGIC, 3) != 0) {
        fclose(f);
        return TCF_ERR_FORMAT;
    }

    uint32_t index_offset =
        header[6]  |
        header[7]  << 8 |
        header[8]  << 16 |
        header[9]  << 24;

    uint32_t file_count =
        header[10] |
        header[11] << 8 |
        header[12] << 16 |
        header[13] << 24;

    uint32_t expected_crc =
        header[14] |
        header[15] << 8 |
        header[16] << 16 |
        header[17] << 24;

    if (crc32(header, 14) != expected_crc) {
        fclose(f);
        return TCF_ERR_CRC;
    }

    /* Read payload */
    size_t payload_size = index_offset - sizeof(header);
    uint8_t *payload = (uint8_t *)malloc(payload_size);
    if (!payload) {
        fclose(f);
        return TCF_ERR_MEMORY;
    }

    if (fread(payload, 1, payload_size, f) != payload_size) {
        free(payload);
        fclose(f);
        return TCF_ERR_IO;
    }

    uint8_t buffer[BUFFER_SIZE];

    for (uint32_t i = 0; i < file_count; i++) {
        uint16_t path_len = read_u16(f);

        char *path = (char *)malloc(path_len + 1);
        if (!path) {
            free(payload);
            fclose(f);
            return TCF_ERR_MEMORY;
        }

        fread(path, 1, path_len, f);
        path[path_len] = 0;

        if (strstr(path, "..")) {
            free(path);
            continue;
        }

        uint32_t offset = read_u32(f);
        uint32_t size   = read_u32(f);

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", output_dir, path);
        ensure_dirs(full_path);

        FILE *out = fopen(full_path, "wb");
        if (!out) {
            free(path);
            free(payload);
            fclose(f);
            return TCF_ERR_IO;
        }

        uint32_t remaining = size;
        uint32_t pos = offset;

        while (remaining > 0) {
            uint32_t chunk =
                remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;

            for (uint32_t j = 0; j < chunk; j++) {
                buffer[j] = unshift_table[payload[pos + j]];
            }

            fwrite(buffer, 1, chunk, out);
            pos += chunk;
            remaining -= chunk;
        }

        fclose(out);
        free(path);
    }

    free(payload);
    fclose(f);
    return TCF_OK;
}