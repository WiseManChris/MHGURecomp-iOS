#pragma once
#include <stdbool.h>

// Automatically detects and extracts any .7z archives in src_dir into target_dir/<name>/.
// Renames extracted archives to .7z.installed so they are not re-extracted every launch.
// Returns the number of archives extracted.
int sz_extract_all_in_dir(const char *src_dir, const char *target_dir);
