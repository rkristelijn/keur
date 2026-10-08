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
 * @implements ADR-005
 */
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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

/// Run a program shell-free: argv[0] is the executable (resolved via PATH if it
/// has no separator), the rest are literal arguments passed straight to the
/// child — no /bin/sh, so nothing in `argv` can be interpreted as a shell
/// metacharacter (CWE-78). `env` is a list of KEY=value pairs added to (and
/// overriding) the inherited environment; use it instead of a `VAR=val cmd`
/// shell prefix. Returns the decoded exit code, or -1 if the spawn failed.
/// posix: posix_spawnp   win32: CreateProcessA
int run_argv(const std::vector<std::string>& argv,
             const std::vector<std::pair<std::string, std::string>>& env = {});

/// Run a shell command, capture stdout, return {exit_code, stdout_text}.
/// stderr is redirected to the platform null device.
struct CaptureResult { int exit_code = 0; std::string out; };
CaptureResult capture(const std::string& cmd);

/// Shell-free capture: spawn argv[0] (resolved via PATH when it has no
/// separator) with the given arguments — NO /bin/sh — and capture stdout.
/// Nothing in `argv` can be interpreted as a shell metacharacter (CWE-78), so
/// this is the safe form for commands built from non-literal parts. stderr is
/// discarded. posix: posix_spawnp + pipe   win32: CreateProcess + pipe.
CaptureResult capture_argv(const std::vector<std::string>& argv);

/// Run a command and stream its raw bytes to `sink` (for NUL-delimited output).
/// Returns the decoded exit code. stderr is redirected to the null device.
int capture_bytes(const std::string& cmd, std::string& sink);

/// Shell-free byte capture: like capture_argv but streams raw stdout bytes to
/// `sink` (for NUL-delimited output). Returns the decoded exit code.
int capture_bytes_argv(const std::vector<std::string>& argv, std::string& sink);

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

/// True if stderr is a terminal (not a pipe/file). Used to decide colour the
/// standard way (TTY → colour, pipe → plain), behind the platform abstraction
/// so no OS call leaks into cmd/ (ADR-005). posix: isatty  win32: _isatty.
bool stderr_is_tty();

/// True if a process with the given PID is currently alive.
/// posix: kill(pid, 0) == 0   win32: OpenProcess + GetExitCodeProcess
bool process_alive(long pid);

// ---------------------------------------------------------------------------
// Interactive terminal input (for the `keur manager` tree TUI)
// ---------------------------------------------------------------------------

/// A single logical key press, decoded from raw terminal bytes. Escape
/// sequences (arrows) and control characters are folded into stable values so
/// business code never parses `\033[` or termios/console specifics itself.
enum class Key {
    Up, Down, Left, Right,   ///< arrows (and their hjkl aliases resolve to Char)
    Enter, Space, Tab, BackTab,
    Backspace, Escape,
    Char,                    ///< a printable char; the byte is in KeyEvent::ch
    Unknown,                 ///< anything we don't map
};

/// One decoded key event. `ch` is meaningful only when `key == Key::Char`.
struct KeyEvent {
    Key  key = Key::Unknown;
    char ch  = '\0';
};

/// Decode a byte buffer into one KeyEvent. PURE — no I/O — so the whole
/// escape-sequence mapping is unit-testable without a terminal. `buf` holds the
/// bytes already read for one logical press:
///   - a single byte for printable/control keys (e.g. 'j', ' ', '\r', 0x7f),
///   - the full ESC sequence for arrows (e.g. "\033[A" for Up).
/// A lone ESC (buf == "\033") maps to Key::Escape.
/// @id KEUR-KEYINPUT
[[nodiscard]] KeyEvent decode_key(const std::string& buf);

/// Read one logical key press from stdin, blocking. Reads the leading byte,
/// and when it is ESC reads the rest of any CSI sequence, then hands the bytes
/// to decode_key(). This is the I/O edge; the mapping lives in decode_key().
KeyEvent read_key();

/// RAII guard that puts the terminal into "raw" mode (no line buffering, no
/// echo, no XON/XOFF flow-control so Ctrl-S/Ctrl-Q reach the app) and restores
/// the previous mode on destruction — even on early return or throw. Move-only.
///   posix: tcgetattr/tcsetattr(TCSANOW) with a saved termios
///   win32: GetConsoleMode/SetConsoleMode (disable line input + echo)
/// `active()` is false when there is no tty (e.g. output piped), so callers can
/// fall back to a non-interactive path.
class RawModeGuard {
  public:
    RawModeGuard();
    ~RawModeGuard();
    RawModeGuard(RawModeGuard&& other) noexcept;
    RawModeGuard& operator=(RawModeGuard&& other) noexcept;
    RawModeGuard(const RawModeGuard&)            = delete;
    RawModeGuard& operator=(const RawModeGuard&) = delete;

    [[nodiscard]] bool active() const { return active_; }

  private:
    void restore();
    bool active_ = false;
    // Opaque saved-state storage sized for a termios (posix) / DWORD (win32);
    // the platform .cpp reinterprets it, keeping OS types out of this header.
    // 128 bytes comfortably fits termios on all POSIX variants (72 on macOS,
    // 60 on Linux) and a DWORD on Windows; asserted in the .cpp.
    alignas(std::max_align_t) unsigned char saved_[128] = {};
};

}  // namespace keur::platform
