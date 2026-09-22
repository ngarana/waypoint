// toplevel_state_test.cpp - Unit tests for shared toplevel/ToplevelStates.
// Assert-based, like the other shared suites.
#include "toplevel/ToplevelStates.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {

void test_empty() {
    auto none = qypr::decodeToplevelStates(nullptr, 0);
    assert(!none.active && !none.minimized && !none.maximized && !none.fullscreen);
    const uint32_t empty[] = {};
    auto alsoNone = qypr::decodeToplevelStates(empty, 0);
    assert(alsoNone == none);
    std::printf("[PASS] empty\n");
}

void test_each_flag() {
    // Protocol values: 0=maximized, 1=minimized, 2=activated, 3=fullscreen.
    const uint32_t active[] = {2};
    assert(qypr::decodeToplevelStates(active, 1).active);
    const uint32_t minimized[] = {1};
    assert(qypr::decodeToplevelStates(minimized, 1).minimized);
    const uint32_t maximized[] = {0};
    assert(qypr::decodeToplevelStates(maximized, 1).maximized);
    const uint32_t fullscreen[] = {3};
    assert(qypr::decodeToplevelStates(fullscreen, 1).fullscreen);
    std::printf("[PASS] each flag\n");
}

void test_combined_and_unknown() {
    // Full set at once, plus an unknown future flag that must be ignored.
    const uint32_t all[] = {0, 1, 2, 3, 42};
    const auto states = qypr::decodeToplevelStates(all, 5);
    assert(states.active && states.minimized && states.maximized && states.fullscreen);
    std::printf("[PASS] combined and unknown\n");
}

}  // namespace

int main() {
    test_empty();
    test_each_flag();
    test_combined_and_unknown();
    printf("All ToplevelStates unit tests passed successfully!\n");
    return 0;
}
