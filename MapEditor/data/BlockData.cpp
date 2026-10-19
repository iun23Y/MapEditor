#include "BlockData.h"

#include <limits>

void BlockData::setData(const std::vector<int>& data, int width, int height, int length) {
    blockData.clear();
    const std::size_t W = static_cast<std::size_t>(width);
    const std::size_t H = static_cast<std::size_t>(height);
    const std::size_t L = static_cast<std::size_t>(length);

    for (std::size_t y = 0; y < H; ++y)
        for (std::size_t z = 0; z < L; ++z)
            for (std::size_t x = 0; x < W; ++x) {
                const std::size_t index = x + z * W + y * W * L;
                if (index >= data.size()) continue;
                const int id = data[index];
                if (id != 0)
                    blockData[glm::ivec3(x, y, z)] = id;
            }
}
std::unordered_map<glm::ivec3, int> BlockData::getBlocks(glm::ivec3 pos1, glm::ivec3 pos2) const {
    std::unordered_map<glm::ivec3, int> blocks;
    blocks.reserve(blockData.size());

    glm::ivec3 a = glm::min(pos1, pos2);
    glm::ivec3 b = glm::max(pos1, pos2);

    for (const auto& [pos, id] : blockData) {
        if (pos.x < a.x || pos.x > b.x ||
            pos.y < a.y || pos.y > b.y ||
            pos.z < a.z || pos.z > b.z) continue;
        blocks[pos] = id;
    }
    return blocks;
}
std::unordered_map<glm::ivec3, int> BlockData::getBlocks(const std::vector<glm::ivec3>& positions) const {
    std::unordered_map<glm::ivec3, int> result;
    result.reserve(positions.size());
    for (const auto& pos : positions) {
        auto it = blockData.find(pos);
        if (it != blockData.end()) result[pos] = it->second;
    }
    return result;
}

glm::ivec3 BlockData::minCorner() const {
    if (blockData.empty()) return glm::ivec3(0);
    glm::ivec3 m = glm::ivec3(std::numeric_limits<int>::max());
    for (const auto& [pos, _] : blockData) m = glm::min(m, pos);
    return m;
}
glm::ivec3 BlockData::maxCorner() const {
    if (blockData.empty()) return glm::ivec3(0);
    glm::ivec3 m = glm::ivec3(std::numeric_limits<int>::min());
    for (const auto& [pos, _] : blockData) m = glm::max(m, pos);
    return m;
}