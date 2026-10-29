#include "sz_util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define Z7_EXTRACT_ONLY
#define Z7_PPMD_SUPPORT

#include "../third_party/lzma/Precomp.h"
#include "../third_party/lzma/7zAlloc.c"
#include "../third_party/lzma/7zArcIn.c"
#include "../third_party/lzma/7zBuf.c"
#include "../third_party/lzma/7zBuf2.c"
#include "../third_party/lzma/7zCrc.c"
#include "../third_party/lzma/7zCrcOpt.c"
#include "../third_party/lzma/7zDec.c"
#include "../third_party/lzma/7zFile.c"
#include "../third_party/lzma/7zStream.c"
#include "../third_party/lzma/Bcj2.c"
#include "../third_party/lzma/Bra.c"
#include "../third_party/lzma/Bra86.c"
#include "../third_party/lzma/CpuArch.c"
#include "../third_party/lzma/Delta.c"
#include "../third_party/lzma/Lzma2Dec.c"
#include "../third_party/lzma/LzmaDec.c"
#include "../third_party/lzma/Ppmd7.c"
#include "../third_party/lzma/Ppmd7Dec.c"

#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define kInputBufSize ((size_t)1 << 18)

static const ISzAlloc g_Alloc = { SzAlloc, SzFree };

static void make_dirs_for_path(const char *path) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char orig = *p;
            *p = '\0';
            mkdir(tmp, 0755);
            *p = orig;
        }
    }
    mkdir(tmp, 0755);
}

static void make_parent_dirs(const char *file_path) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s", file_path);
    char *slash = strrchr(tmp, '/');
    char *bslash = strrchr(tmp, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) {
        *slash = '\0';
        make_dirs_for_path(tmp);
    }
}

static bool ends_with_ci(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t ls = strlen(str), lsf = strlen(suffix);
    if (ls < lsf) return false;
    return !strcasecmp(str + ls - lsf, suffix);
}

static int sz_extract_single_archive(const char *archive_path, const char *dest_dir) {
    CFileInStream archiveStream;
    CLookToRead2 lookStream;
    CSzArEx db;
    ISzAlloc allocImp = g_Alloc;
    ISzAlloc allocTempImp = g_Alloc;

    FileInStream_CreateVTable(&archiveStream);
    archiveStream.wres = 0;
    LookToRead2_CreateVTable(&lookStream, False);
    lookStream.buf = NULL;

    WRes wres = InFile_Open(&archiveStream.file, archive_path);
    if (wres != 0) {
        LOG("[7z] Cannot open 7z input file: %s (error %d)\n", archive_path, wres);
        return -1;
    }

    lookStream.buf = (Byte *)ISzAlloc_Alloc(&allocImp, kInputBufSize);
    if (!lookStream.buf) {
        File_Close(&archiveStream.file);
        return -1;
    }
    lookStream.bufSize = kInputBufSize;
    lookStream.realStream = &archiveStream.vt;
    LookToRead2_INIT(&lookStream);

    CrcGenerateTable();
    SzArEx_Init(&db);

    SRes res = SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp);
    if (res != SZ_OK) {
        LOG("[7z] SzArEx_Open error for %s: %d\n", archive_path, res);
        ISzAlloc_Free(&allocImp, lookStream.buf);
        File_Close(&archiveStream.file);
        return -1;
    }

    UInt32 blockIndex = 0xFFFFFFFF;
    Byte *outBuffer = 0;
    size_t outBufferSize = 0;
    UInt16 *temp = NULL;
    size_t tempSize = 0;
    int extracted = 0;

    make_dirs_for_path(dest_dir);

    for (UInt32 i = 0; i < db.NumFiles; i++) {
        size_t offset = 0;
        size_t outSizeProcessed = 0;
        const BoolInt isDir = SzArEx_IsDir(&db, i);
        size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        if (len > tempSize) {
            SzFree(NULL, temp);
            tempSize = len;
            temp = (UInt16 *)SzAlloc(NULL, tempSize * sizeof(temp[0]));
            if (!temp) break;
        }
        SzArEx_GetFileNameUtf16(&db, i, temp);

        // Convert UTF-16 filename to UTF-8
        char utf8_name[1024];
        size_t ui = 0;
        for (size_t k = 0; temp[k] != 0 && ui + 4 < sizeof(utf8_name); k++) {
            UInt32 c = temp[k];
            if (c < 0x80) utf8_name[ui++] = (char)c;
            else if (c < 0x800) {
                utf8_name[ui++] = (char)(0xC0 | (c >> 6));
                utf8_name[ui++] = (char)(0x80 | (c & 0x3F));
            } else {
                utf8_name[ui++] = (char)(0xE0 | (c >> 12));
                utf8_name[ui++] = (char)(0x80 | ((c >> 6) & 0x3F));
                utf8_name[ui++] = (char)(0x80 | (c & 0x3F));
            }
        }
        utf8_name[ui] = '\0';

        // Skip directory traversal attempts
        if (strstr(utf8_name, "../") || strstr(utf8_name, "..\\")) continue;
        // Skip macOS metadata
        if (!strncmp(utf8_name, "__MACOSX", 8) || strstr(utf8_name, "/__MACOSX") ||
            ends_with_ci(utf8_name, ".DS_Store")) continue;

        char out_path[1400];
        snprintf(out_path, sizeof out_path, "%s/%s", dest_dir, utf8_name);

        if (isDir) {
            make_dirs_for_path(out_path);
            continue;
        }

        res = SzArEx_Extract(&db, &lookStream.vt, i,
                             &blockIndex, &outBuffer, &outBufferSize,
                             &offset, &outSizeProcessed,
                             &allocImp, &allocTempImp);
        if (res != SZ_OK) {
            LOG("[7z] Error extracting file %s from %s (code %d)\n", utf8_name, archive_path, res);
            break;
        }

        make_parent_dirs(out_path);
        CSzFile outFile;
        wres = OutFile_Open(&outFile, out_path);
        if (wres != 0) {
            LOG("[7z] Cannot open output file %s: %d\n", out_path, wres);
            continue;
        }
        size_t processedSize = outSizeProcessed;
        File_Write(&outFile, outBuffer + offset, &processedSize);
        File_Close(&outFile);
        extracted++;
    }

    SzFree(NULL, temp);
    SzArEx_Free(&db, &allocImp);
    ISzAlloc_Free(&allocImp, outBuffer);
    ISzAlloc_Free(&allocImp, lookStream.buf);
    File_Close(&archiveStream.file);
    return extracted;
}

int sz_extract_all_in_dir(const char *src_dir, const char *target_dir) {
    DIR *d = opendir(src_dir);
    if (!d) return 0;
    struct dirent *e;
    int count = 0;

    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (!ends_with_ci(e->d_name, ".7z")) continue;
        if (ends_with_ci(e->d_name, ".installed") || ends_with_ci(e->d_name, ".extracted")) continue;

        char archive_path[1200];
        snprintf(archive_path, sizeof archive_path, "%s/%s", src_dir, e->d_name);

        struct stat st;
        if (stat(archive_path, &st) != 0 || S_ISDIR(st.st_mode)) continue;

        char mod_name[512];
        snprintf(mod_name, sizeof mod_name, "%s", e->d_name);
        char *dot = strrchr(mod_name, '.');
        if (dot) *dot = '\0';

        char dest_dir[1200];
        snprintf(dest_dir, sizeof dest_dir, "%s/%s", target_dir, mod_name);

        LOG("[7z] Found 7z archive '%s' (%lld MB) -> extracting to '%s'...\n",
            archive_path, (long long)(st.st_size / (1024 * 1024)), dest_dir);

        int files = sz_extract_single_archive(archive_path, dest_dir);
        if (files >= 0) {
            char installed_path[1250];
            snprintf(installed_path, sizeof installed_path, "%s.installed", archive_path);
            rename(archive_path, installed_path);
            LOG("[7z] Successfully extracted %d files from '%s' into '%s'\n", files, archive_path, dest_dir);
            count++;
        }
    }
    closedir(d);
    return count;
}
