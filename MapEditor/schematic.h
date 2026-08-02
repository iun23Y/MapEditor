#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/System.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace std {
    template<>
    struct hash<sf::Vector3i> {
        std::size_t operator()(const sf::Vector3i& v) const noexcept {
            std::size_t seed = 0;
            auto hash_combine = [&seed](int value) {
                seed ^= std::hash<int>{}(value)+0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
            hash_combine(v.x);
            hash_combine(v.y);
            hash_combine(v.z);
            return seed;
        }
    };
};

class SchematicMap {
public:
    int width = 0;
    int height = 0;
    int length = 0;
    int offsetX = 0;
    int offsetY = 0;
    int offsetZ = 0;
    std::unordered_map<int, std::string> palette;
    std::unordered_map<std::string, int> nameToId;

    std::unordered_map<sf::Vector3i, int> blocks;

    SchematicMap(const std::string& filename);
};

std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& compressed);
std::pair<int32_t, size_t> readVarint(const std::vector<uint8_t>& data, size_t offset);
std::vector<int32_t> decodeBlockIndices(const std::vector<uint8_t>& rawData, int expectedCount);
sf::Color getBlockColor(int blockId, const std::unordered_map<int, std::string>& palette);