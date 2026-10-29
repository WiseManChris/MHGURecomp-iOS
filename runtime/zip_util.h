#pragma once
#include <stdbool.h>

// Automatically detects and extracts any .zip archives in src_dir into target_dir.
// Renames extracted archives to .zip.installed so they are not re-extracted every launch.
// Returns the number of archives extracted.
int zip_extract_all_in_dir(const char *src_dir, const char *target_dir);
