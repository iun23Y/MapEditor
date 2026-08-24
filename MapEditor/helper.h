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

static std::vector<sf::Vector2i> bresenhamLine(int x0, int z0, int x1, int z1) {
    std::vector<sf::Vector2i> points;
    int dx = std::abs(x1 - x0);
    int dz = std::abs(z1 - z0);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (z0 < z1) ? 1 : -1;
    int err = dx - dz;

    while (true) {
        points.emplace_back(x0, z0);
        if (x0 == x1 && z0 == z1) break;
        int e2 = 2 * err;
        if (e2 > -dz) { err -= dz; x0 += sx; }
        if (e2 < dx) { err += dx; z0 += sz; }
    }
    return points;
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