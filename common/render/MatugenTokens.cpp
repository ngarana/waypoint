// MatugenTokens.cpp - Shared Material You token lookup.
#include "render/MatugenTokens.hpp"

namespace qypr {

std::string pickMatugenToken(const std::map<std::string, std::string>& tokens,
                             const std::vector<const char*>& candidates) {
    for (const char* name : candidates) {
        if (const auto it = tokens.find(name); it != tokens.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return "";
}

std::string pickMatugenToken(const std::map<std::string, std::string>& tokens,
                             std::initializer_list<const char*> candidates) {
    return pickMatugenToken(tokens, std::vector<const char*>(candidates));
}

}  // namespace qypr
