/**
 * @file common/compat.h
 * @brief Cross-platform compatibility shims (name-level) for POSIX/Windows.
 *
 * These are *name* shims (popen -> _popen, a PATH_SEP constant, etc.) used
 * inside the platform_*.cpp files. Business logic should prefer the richer
 * keur::platform:: API in platform.h; compat.h exists so the platform
 * implementations can share one spelling of the primitives.
 *
 * @see docs/adr/ADR-005-platform-abstraction.md
 */
#pragma once

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define popen _popen
#define pclose _pclose
static constexpr char KEUR_PATH_SEP = '\\';
static constexpr const char* KEUR_NULL_DEVICE = "nul";
#else
#include <unistd.h>
static constexpr char KEUR_PATH_SEP = '/';
static constexpr const char* KEUR_NULL_DEVICE = "/dev/null";
#endif
