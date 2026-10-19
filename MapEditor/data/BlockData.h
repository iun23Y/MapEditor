#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <unordered_map>
#include <vector>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

class BlockData {
public:
    BlockData() = default;

    explicit BlockData(const std::unordered_map<glm::ivec3, int>& data) : blockData(data) {}
    explicit BlockData(std::unordered_map<glm::ivec3, int>&& data) : blockData(std::move(data)) {}
    BlockData(const std::vector<int>& data, int width, int height, int depth) { setData(data, width, height, depth); }

    void setData(const std::vector<int>& data, int width, int height, int depth);
    std::unordered_map<glm::ivec3, int> getData() { return blockData; }

    int getBlock(glm::ivec3 pos) const {
        auto it = blockData.find(pos);
        return it != blockData.end() ? it->second : 0;
    }
    std::unordered_map<glm::ivec3, int> getBlocks(const std::vector<glm::ivec3>& positions) const;
    std::unordered_map<glm::ivec3, int> getBlocks(glm::ivec3 pos1, glm::ivec3 pos2) const;

    bool hasBlock(glm::ivec3 pos) const {
        return blockData.find(pos) != blockData.end();
    }
    void setBlocks(const std::unordered_map<glm::ivec3, int>& data) {
        for (const auto& [pos, id] : data) {
            if (id == 0) blockData.erase(pos);
            else blockData[pos] = id;
        }
    }
    void setBlock(glm::ivec3 pos, int blockId) {
        if (blockId == 0) blockData.erase(pos);
        else blockData[pos] = blockId;
    }

    void removeBlock(glm::ivec3 pos) { blockData.erase(pos); }
    void clear() { blockData.clear(); }

    bool empty() const { return blockData.empty(); }
    std::size_t size() const { return blockData.size(); }

    glm::ivec3 minCorner() const;
    glm::ivec3 maxCorner() const;

    const std::unordered_map<glm::ivec3, int>& raw() const { return blockData; }
    std::unordered_map<glm::ivec3, int>& raw() { return blockData; }

private:
    std::unordered_map<glm::ivec3, int> blockData;
};