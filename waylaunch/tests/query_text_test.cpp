// Query-text sanitation helpers (src/util/query_text.cpp). These guard the
// launcher search box: xkb folds editing keys onto control codepoints and
// pasted clipboard text can carry newlines/other controls, neither of which
// may enter the single-line query. Pure, no Wayland/xkb needed.
#include "waylaunch/query_text.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace waylaunch;

namespace {

void control_classification() {
    assert(is_control_cp(0x00));     // NUL / no-symbol keys
    assert(is_control_cp(0x08));     // BS
    assert(is_control_cp(0x09));     // TAB
    assert(is_control_cp(0x0A));     // LF
    assert(is_control_cp(0x0D));     // CR
    assert(is_control_cp(0x1F));     // last C0
    assert(is_control_cp(0x7F));     // DEL  <- the Delete-key bug
    assert(is_control_cp(0x80));     // first C1
    assert(is_control_cp(0x9F));     // last C1
    assert(!is_control_cp(0x20));    // space is text
    assert(!is_control_cp('a'));     // ASCII letter
    assert(!is_control_cp(0x7E));    // '~'
    assert(!is_control_cp(0xA0));    // NBSP: not a control
    assert(!is_control_cp(0xE9));    // 'é'
    assert(!is_control_cp(0x1F600)); // emoji
}

void passthrough_keeps_plain_text() {
    assert(strip_control_chars("hello world") == "hello world");
    assert(strip_control_chars("").empty());
}

void strips_c0_controls() {
    assert(strip_control_chars("a\nb\tc") == "abc");               // LF + TAB
    assert(strip_control_chars("line1\r\nline2") == "line1line2"); // CRLF
    assert(strip_control_chars("a\x7f"
                               "b") == "ab"); // DEL byte
}

void strips_c1_without_shearing_multibyte() {
    // U+0085 (NEL) is a C1 control encoded as 0xC2 0x85 — must be removed whole.
    assert(strip_control_chars("x\xC2\x85y") == "xy");
    // Adjacent real multibyte characters survive intact.
    assert(strip_control_chars("caf\xC3\xA9") == "caf\xC3\xA9");               // café
    assert(strip_control_chars("\xF0\x9F\x98\x80ok") == "\xF0\x9F\x98\x80ok"); // 😀ok
    // A C1 wedged between two multibyte chars leaves both untouched.
    assert(strip_control_chars("\xC3\xA9\xC2\x85\xC3\xA9") == "\xC3\xA9\xC3\xA9");
}

void malformed_bytes_are_dropped_not_corrupting() {
    assert(strip_control_chars("a\xFF"
                               "b") == "ab");    // invalid lead byte
    assert(strip_control_chars("a\xC2") == "a"); // truncated 2-byte lead at end
    assert(strip_control_chars("a\xC2z"
                               "b") == "azb");       // bad continuation: drop lead, keep 'z'
    assert(strip_control_chars("\x80\x80").empty()); // stray continuation bytes
}

} // namespace

int main() {
    control_classification();
    passthrough_keeps_plain_text();
    strips_c0_controls();
    strips_c1_without_shearing_multibyte();
    malformed_bytes_are_dropped_not_corrupting();
    std::puts("query_text_test: all passed");
    return 0;
}
