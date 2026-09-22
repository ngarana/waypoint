// matugen_tokens_test.cpp - Unit tests for the shared token lookup.
// Assert-based, like the other shared suites.
#include "render/MatugenTokens.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>

namespace {

void test_priority_order() {
    const std::map<std::string, std::string> tokens = {{"surface", "#222222"},
                                                       {"background", "#111111"}};
    assert(qypr::pickMatugenToken(tokens, {"background", "surface"}) == "#111111");
    assert(qypr::pickMatugenToken(tokens, {"surface", "background"}) == "#222222");
    std::printf("[PASS] priority order\n");
}

void test_empty_never_wins() {
    // A present-but-blank token must not shadow a later real one.
    const std::map<std::string, std::string> tokens = {{"background", ""}, {"surface", "#222222"}};
    assert(qypr::pickMatugenToken(tokens, {"background", "surface"}) == "#222222");
    assert(qypr::pickMatugenToken(tokens, {"background"}) == "");
    std::printf("[PASS] empty never wins\n");
}

void test_miss_is_empty() {
    const std::map<std::string, std::string> tokens = {{"primary", "#89b4fa"}};
    assert(qypr::pickMatugenToken(tokens, {"background", "surface"}) == "");
    const std::map<std::string, std::string> empty;
    assert(qypr::pickMatugenToken(empty, {"background"}) == "");
    std::printf("[PASS] miss is empty\n");
}

}  // namespace

int main() {
    test_priority_order();
    test_empty_never_wins();
    test_miss_is_empty();
    printf("All MatugenTokens unit tests passed successfully!\n");
    return 0;
}
