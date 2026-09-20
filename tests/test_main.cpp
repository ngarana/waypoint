// test_main.cpp - Counters + runner for the modular unit tests.
#include "test_framework.hpp"

int g_tests_run = 0;
int g_tests_failed = 0;

int main() {
    std::cout << "========================================" << '\n';
    std::cout << " Running unit tests" << '\n';
    std::cout << "========================================" << '\n';

    // Unit tests are registered during static initialization.
    // They run automatically.

    std::cout << "========================================" << '\n';
    std::cout << " Results: Passed: " << (g_tests_run - g_tests_failed) << " / " << g_tests_run
              << "   Failed: " << g_tests_failed << '\n';
    std::cout << "========================================" << '\n';

    return g_tests_failed == 0 ? 0 : 1;
}
