#pragma once

#include <SFML/Graphics/Color.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class SchematicMap {
public:
    int width = 0;
    int height = 0;
    int length = 0;
    std::unordered_map<int, std::string> palette;
    std::unordered_map<std::string, int> nameToId;
    std::vector<std::vector<int>> topBlocks;
    std::vector<std::vector<int>> topHeights;

    SchematicMap() = default;
    SchematicMap(int w, int h, int l,
        const std::unordered_map<int, std::string>& pal,
        std::vector<std::vector<int>>&& tBlocks,
        std::vector<std::vector<int>>&& tHeights);
};

std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& compressed);
std::pair<int32_t, size_t> readVarint(const std::vector<uint8_t>& data, size_t offset);
std::vector<int32_t> decodeBlockIndices(const std::vector<uint8_t>& rawData, int expectedCount);
sf::Color getBlockColor(int blockId, const std::unordered_map<int, std::string>& palette);
SchematicMap loadSchematic(const std::string& filename);
