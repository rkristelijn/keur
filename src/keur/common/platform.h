/**
 * @file common/platform.h
 * @brief Platform abstraction layer — compile-time selection via translation units.
 *
 * Declare every platform-divergent function here. Implementations live in:
 *   platform_posix.cpp  — macOS + Linux
 *   platform_win32.cpp  — Windows
 *
 * The Makefile selects the correct .cpp via PLATFORM_SRC (ADR-005 / cpm ADR-170).
 * Business logic (cmd/, adapters/, learn/, report/) must NEVER include
 * <windows.h>, <unistd.h>, mach headers, hardcode a path separator, or call
 * popen/WEXITSTATUS/`command -v` directly. Route everything through platform::
 * (and the shims in compat.h). The keur-internal portability rules enforce this.
 *
 * @see docs/adr/ADR-005-platform-abstraction.md
 */
#pragma once
#include <string>

namespace keur::platform {

/// Coarse OS family, for logic that must branch on the platform without
/// scattering #ifdefs through business code.
enum class OsKind { MacOS, Linux, Windows, Unknown };

/// The OS this binary was built for.
OsKind os_kind();

/// Path separator for this platform ('/' on POSIX, '\\' on Windows).
char path_sep();

/// Join two path segments with the platform separator (no duplicate sep).
std::string path_join(const std::string& a, const std::string& b);

/// Decode a raw system()/pclose() status into a plain exit code.
/// posix: WIFEXITED / WEXITSTATUS   win32: the value is already the exit code.
int wait_exit(int raw_status);

/// Run a shell command, discard output, return the decoded exit code.
int run(const std::string& cmd);

/// Run a shell command, capture stdout, return {exit_code, stdout_text}.
/// stderr is redirected to the platform null device.
struct CaptureResult { int exit_code = 0; std::string out; };
CaptureResult capture(const std::string& cmd);

/// Run a command and stream its raw bytes to `sink` (for NUL-delimited output).
/// Returns the decoded exit code. stderr is redirected to the null device.
int capture_bytes(const std::string& cmd, std::string& sink);

/// True if a tool is resolvable on PATH (exit 0 = found).
/// posix: command -v <tool>   win32: where <tool>
bool tool_on_path(const std::string& tool);

/// Create `path` and any missing parents (mkdir -p semantics). Idempotent.
bool make_dir(const std::string& path);

/// True if `path` exists (file or directory).
bool path_exists(const std::string& path);

/// Write `content` to `path`; on POSIX also mark it executable (0755).
/// On Windows the executable bit is a no-op (extension-based). Returns success.
bool write_executable(const std::string& path, const std::string& content);

/// Remove a file (no error if absent).
void remove_file(const std::string& path);

/// Host resource snapshot for `keur learn`. Any field the platform cannot
/// supply is left at 0 (e.g. load average on Windows).
struct Resources {
    double load1 = 0.0;       // 1-min load average
    int    ncpu = 0;          // logical CPUs
    double ram_total_gb = 0;
    double ram_free_gb = 0;   // available RAM
};
Resources read_resources();

/// Sleep for `ms` milliseconds.
void sleep_ms(int ms);

/// Terminal size (columns, rows) of the controlling terminal. Falls back to
/// {cols, rows} = {0, 0} when there is no tty or the query is unsupported.
struct TermSize { int cols = 0; int rows = 0; };
TermSize terminal_size();

/// True if a process with the given PID is currently alive.
/// posix: kill(pid, 0) == 0   win32: OpenProcess + GetExitCodeProcess
bool process_alive(long pid);

}  // namespace keur::platform
