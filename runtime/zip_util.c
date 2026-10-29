#include "zip_util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "miniz.h"

#define LOG(...) fprintf(stderr, __VA_ARGS__)

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

static bool extract_single_zip(const char *zip_path, const char *dest_dir) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
        LOG("[zip] Failed to open zip archive '%s'\n", zip_path);
        return false;
    }
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    int extracted_files = 0;
    make_dirs_for_path(dest_dir);

    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (!st.m_filename[0]) continue;
        // Skip directory traversal attempts
        if (strstr(st.m_filename, "../") || strstr(st.m_filename, "..\\")) continue;
        // Skip macOS metadata and hidden system files
        if (!strncmp(st.m_filename, "__MACOSX", 8) || strstr(st.m_filename, "/__MACOSX") ||
            ends_with_ci(st.m_filename, ".DS_Store")) continue;

        char out_path[1400];
        snprintf(out_path, sizeof out_path, "%s/%s", dest_dir, st.m_filename);

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            make_dirs_for_path(out_path);
        } else {
            make_parent_dirs(out_path);
            if (mz_zip_reader_extract_to_file(&zip, i, out_path, 0)) {
                extracted_files++;
            } else {
                LOG("[zip] Warning: could not extract '%s' from '%s'\n", st.m_filename, zip_path);
            }
        }
    }
    mz_zip_reader_end(&zip);
    LOG("[zip] Extracted %d files from '%s' into '%s'\n", extracted_files, zip_path, dest_dir);
    return true;
}

int zip_extract_all_in_dir(const char *src_dir, const char *target_dir) {
    DIR *d = opendir(src_dir);
    if (!d) return 0;
    struct dirent *e;
    int count = 0;

    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (!ends_with_ci(e->d_name, ".zip")) continue;
        if (ends_with_ci(e->d_name, ".installed") || ends_with_ci(e->d_name, ".extracted")) continue;

        char zip_path[1200];
        snprintf(zip_path, sizeof zip_path, "%s/%s", src_dir, e->d_name);

        struct stat st;
        if (stat(zip_path, &st) != 0 || S_ISDIR(st.st_mode)) continue;

        // Base name without .zip
        char mod_name[512];
        snprintf(mod_name, sizeof mod_name, "%s", e->d_name);
        char *dot = strrchr(mod_name, '.');
        if (dot) *dot = '\0';

        char dest_dir[1200];
        snprintf(dest_dir, sizeof dest_dir, "%s/%s", target_dir, mod_name);

        LOG("[zip] Found zip archive '%s' (%lld bytes) -> extracting to '%s'...\n",
            zip_path, (long long)st.st_size, dest_dir);

        if (extract_single_zip(zip_path, dest_dir)) {
            char installed_path[1250];
            snprintf(installed_path, sizeof installed_path, "%s.installed", zip_path);
            rename(zip_path, installed_path);
            count++;
        }
    }
    closedir(d);
    return count;
}
