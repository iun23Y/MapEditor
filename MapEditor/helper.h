#pragma once

#define NOMINMAX   // <-- добавьте перед windows.h

#include <string>
#include <utility>
#include <functional>
#include <windows.h>
#include <SFML/System/Vector2.hpp>

static std::string getExeDirectory() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t last = path.find_last_of("\\/");
    if (last != std::string::npos) path = path.substr(0, last + 1);
    return path;
}

struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const {
        return std::hash<int>{}(p.first) ^ (std::hash<int>{}(p.second) << 1);
    }
};

struct Vector2iHash {
    std::size_t operator()(const sf::Vector2i& v) const noexcept {
        std::size_t seed = 0;
        auto hash_combine = [&seed](int value) {
            seed ^= std::hash<int>{}(value)+0x9e3779b9 + (seed << 6) + (seed >> 2);
            };
        hash_combine(v.x);
        hash_combine(v.y);
        return seed;
    }
};

struct Vector2iEqual {
    bool operator()(const sf::Vector2i& lhs, const sf::Vector2i& rhs) const noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
};
