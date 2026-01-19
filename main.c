/* SPDX-License-Identifier: Zlib
 * Copyright (c) 2026 Tbaggerofsteam, Tbaggeroftheuk
 */

#include <stdio.h>

#define _POSIX_C_SOURCE 200809L
#include <string.h>

#include "tcf.h"

#ifdef _WIN32
  #include <direct.h>
  #include <windows.h>
  #define MKDIR(path) _mkdir(path)
#else
  #include <sys/stat.h>
  #include <dirent.h>
  #define MKDIR(path) mkdir(path, 0755)
#endif

int main(int argc, char *argv[]) {
    printf("Tbag Content File Packer \nVersion: 1.0\n");

    if (argc < 3) {
        printf("For packing: %s pack <input dir> <output file>\n", argv[0]);
        printf("For unpack: %s  unpack <input file> <output dir>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "pack") == 0) {
        ensure_dirs(argv[3]);
        int tcf = tcf_pack(argv[2], argv[3]);

        if (tcf == TCF_ERR_IO) {
            printf("IO error\n");
            return 1;
        }

        if (tcf == TCF_ERR_CRC) {
            printf("The TCF file has a CRC error\n");
            return 1;
        }

        if (tcf == TCF_ERR_MEMORY) {
            printf("A memory error occured!\n");
            return 1;
        }

        if (tcf == TCF_ERR_FORMAT) {
            printf("The TCF file has a format error.\n Is it a TCF file?\n");
            return 1;
        }

        printf("Succesfully packed the TCF!\n");
        
    } else if (strcmp(argv[1], "unpack") == 0) {
        int tcf = tcf_extract(argv[2], argv[3]);

        if (tcf == TCF_ERR_IO) {
            printf("IO error\n");
            return 1;
        }

        if (tcf == TCF_ERR_CRC) {
            printf("The TCF file has a CRC error\n");
            return 1;
        }

        if (tcf == TCF_ERR_MEMORY) {
            printf("A memory error occured!\n");
            return 1;
        }

        if (tcf == TCF_ERR_FORMAT) {
            printf("The TCF file has a format error.\n Is it a TCF file?\n");
            return 1;
        }

    } else if(strcmp(argv[1], "help") == 0) {
        printf("For packing: %s pack <input dir> <output file>\n", argv[1]);
        printf("For unpack: %s  unpack <input file> <output dir>\n", argv[1]);
    }

}