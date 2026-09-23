/**
 * @file common/keyinput.cpp
 * @brief Pure terminal key decoding — the platform-independent half of the
 *        input layer. Turns a raw byte buffer (a single byte, or a full ANSI
 *        escape sequence) into one stable KeyEvent.
 *
 * This is intentionally NOT in platform_posix/win32.cpp: the byte→Key mapping
 * is identical on macOS, Linux and Windows once the terminal speaks ANSI/VT
 * (POSIX termios and Win10+ ENABLE_VIRTUAL_TERMINAL_INPUT both deliver the same
 * "\033[A"-style sequences). Only entering raw mode and reading bytes are
 * OS-divergent — those stay behind the platform wall. Keeping the decoder here,
 * pure and shared, means one unit-tested mapping and no #ifdef in the logic.
 * @see docs/adr/ADR-005-platform-abstraction.md
 * @implements ADR-005
 */
#include "common/platform.h"

namespace keur::platform {

KeyEvent decode_key(const std::string& buf) {
    if (buf.empty()) return {Key::Unknown, '\0'};

    const unsigned char c0 = static_cast<unsigned char>(buf[0]);

    // CSI escape sequences: ESC '[' <final>. Arrows are the ones we care about;
    // this is the same on POSIX and on a VT-enabled Windows console.
    if (c0 == 0x1b) {  // ESC
        if (buf.size() >= 3 && buf[1] == '[') {
            switch (buf[2]) {
                case 'A': return {Key::Up, '\0'};
                case 'B': return {Key::Down, '\0'};
                case 'C': return {Key::Right, '\0'};
                case 'D': return {Key::Left, '\0'};
                default:  return {Key::Unknown, '\0'};
            }
        }
        // A lone ESC (or an unrecognised/partial sequence) is Escape.
        return {Key::Escape, '\0'};
    }

    // Control characters we map by name; everything else printable is a Char.
    switch (c0) {
        case '\r':   // CR — Enter in raw mode
        case '\n':   // LF — Enter in line mode / piped input
            return {Key::Enter, '\0'};
        case '\t':
            return {Key::Tab, '\0'};
        case 0x7f:   // DEL
        case 0x08:   // BS
            return {Key::Backspace, '\0'};
        case ' ':
            return {Key::Space, ' '};
        default:
            break;
    }

    // Printable ASCII (incl. hjkl, s, q, ?, g, G) → Char carrying the byte.
    if (c0 >= 0x20 && c0 < 0x7f)
        return {Key::Char, static_cast<char>(c0)};

    return {Key::Unknown, '\0'};
}

}  // namespace keur::platform
