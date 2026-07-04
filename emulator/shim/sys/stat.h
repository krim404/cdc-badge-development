/**
 * \file sys/stat.h (host shim)
 * \brief Pass-through to the system header; on Windows/MinGW additionally
 *        adapts POSIX mkdir(path, mode) to the single-argument _mkdir the
 *        reused host_api_fs.cpp calls with two arguments.
 */
#pragma once

#include_next <sys/stat.h>

#ifdef _WIN32
#include <direct.h>

static inline int emu_mkdir_compat(const char* path, unsigned mode)
{
    (void)mode; /* permission bits have no direct Windows equivalent */
    return _mkdir(path);
}
#define mkdir emu_mkdir_compat
#endif
