#pragma once

#define GLM_ENABLE_EXPERIMENTA

#include "Chunk.h"
#include "BlockData.h"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

class ChunkData {
public:
    ChunkData() = default;

    void createChunk(glm::ivec3 pos) {
        const glm::ivec3 cc = toChunkCoord(pos);
        if (m_chunks.find(cc) != m_chunks.end())
            throw std::invalid_argument("ChunkData: chunk already exists");
        const glm::ivec3 minPos = cc * Chunk::SIZE;
        const glm::ivec3 maxPos = minPos + glm::ivec3(Chunk::SIZE);
        m_chunks.emplace(cc, Chunk(minPos, maxPos));
        updateBoundsCached(minPos, maxPos);
    }
    Chunk& addChunk(Chunk&& chunk) {
        const glm::ivec3 cc = toChunkCoord(chunk.getMinPos());
        auto [it, inserted] = m_chunks.insert_or_assign(cc, std::move(chunk));
        updateBoundsCached(it->second.getMinPos(), it->second.getMaxPos());
        return it->second;
    }

    void setBlocks(const BlockData& data) {
        std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, int>> byChunk;
        for (const auto& [pos, id] : data.raw()) {
            if (id < 0) continue;
            const glm::ivec3 cc = toChunkCoord(pos);
            const glm::ivec3 chunkMin = cc * Chunk::SIZE;
            byChunk[cc][pos - chunkMin] = id;
        }

        for (auto& [cc, blocks] : byChunk) {
            auto it = m_chunks.find(cc);
            if (it == m_chunks.end()) {
                const glm::ivec3 minPos = cc * Chunk::SIZE;
                const glm::ivec3 maxPos = minPos + glm::ivec3(Chunk::SIZE);
                it = m_chunks.emplace(cc, Chunk(minPos, maxPos)).first;
                updateBoundsCached(minPos, maxPos);
            }

            std::unordered_map<glm::ivec3, int> worldBlocks;
            worldBlocks.reserve(blocks.size());
            const glm::ivec3 chunkMin = it->second.getMinPos();
            for (const auto& [localPos, id] : blocks)
                worldBlocks.emplace(chunkMin + localPos, id);
            it->second.set(worldBlocks);
        }
    }
    void setBlock(glm::ivec3 worldPos, uint16_t id) {
        Chunk& c = getOrCreateChunk(worldPos);
        c.set(worldPos, id);
    }

    BlockData getRegion(glm::ivec3 from, glm::ivec3 to) const {
        BlockData out;
        if (from.x >= to.x || from.y >= to.y || from.z >= to.z)
            return out;

        auto emitChunk = [&](const Chunk& chunk) {
            const int localMaxY = chunk.maxY();
            if (localMaxY < 0) return;

            const glm::ivec3 chunkMin = chunk.getMinPos();
            const glm::ivec3 chunkMax = chunk.getMaxPos();

            const glm::ivec3 a = glm::max(from, chunkMin);
            const glm::ivec3 b = glm::min(to, chunkMax);
            if (a.x >= b.x || a.y >= b.y || a.z >= b.z) return;

            const int worldMaxY = chunkMin.y + localMaxY;
            const int y1 = std::min(b.y, worldMaxY + 1);

            const auto raw = chunk.data();
            const int lx0 = a.x - chunkMin.x;
            const int lx1 = b.x - chunkMin.x;
            const int lz0 = a.z - chunkMin.z;
            const int lz1 = b.z - chunkMin.z;

            for (int wy = a.y; wy < y1; ++wy) {
                const int ly = wy - chunkMin.y;
                const size_t layerBase = static_cast<size_t>(ly) * Chunk::LAYER;
                for (int lz = lz0; lz < lz1; ++lz) {
                    const size_t rowBase = layerBase + static_cast<size_t>(lz) * Chunk::SIZE;
                    for (int lx = lx0; lx < lx1; ++lx) {
                        const uint16_t id = raw[rowBase + lx];
                        if (id == Chunk::AIR) continue;
                        out.raw().emplace(chunkMin + glm::ivec3{ lx, ly, lz } - from, id);
                    }
                }
            }
            };

        const glm::ivec3 ccMin = toChunkCoord(from);
        const glm::ivec3 ccMax = toChunkCoord(to - glm::ivec3(1));
        const int64_t dx = int64_t(ccMax.x) - ccMin.x + 1;
        const int64_t dy = int64_t(ccMax.y) - ccMin.y + 1;
        const int64_t dz = int64_t(ccMax.z) - ccMin.z + 1;
        const int64_t scanSize = dx * dy * dz;

        if (scanSize <= int64_t(m_chunks.size())) {
            for (int cy = ccMin.y; cy <= ccMax.y; ++cy)
                for (int cz = ccMin.z; cz <= ccMax.z; ++cz)
                    for (int cx = ccMin.x; cx <= ccMax.x; ++cx) {
                        auto it = m_chunks.find(glm::ivec3{ cx, cy, cz });
                        if (it != m_chunks.end()) emitChunk(it->second);
                    }
        }
        else {
            for (const auto& [cc, chunk] : m_chunks) emitChunk(chunk);
        }
        return out;
    }
    std::optional<uint16_t> getBlock(glm::ivec3 worldPos) const {
        const Chunk* c = tryGetChunk(worldPos);
        if (!c) return std::nullopt;
        return c->get(worldPos);
    }
    const std::unordered_map<glm::ivec3, Chunk>& getChunks() const noexcept {
        return m_chunks;
    }

    std::vector<Chunk*> getChunksInRegion(glm::ivec3 from, glm::ivec3 to) {
        std::vector<Chunk*> result;
        if (from.x >= to.x || from.y >= to.y || from.z >= to.z)
            return result;
        const glm::ivec3 ccMin = toChunkCoord(from);
        const glm::ivec3 ccMax = toChunkCoord(to - glm::ivec3(1));
        for (int cy = ccMin.y; cy <= ccMax.y; ++cy)
            for (int cz = ccMin.z; cz <= ccMax.z; ++cz)
                for (int cx = ccMin.x; cx <= ccMax.x; ++cx) {
                    auto it = m_chunks.find(glm::ivec3{ cx, cy, cz });
                    if (it != m_chunks.end())
                        result.push_back(&it->second);
                }
        return result;
    }
    std::vector<const Chunk*> getChunksInRegion(glm::ivec3 from, glm::ivec3 to) const {
        std::vector<const Chunk*> result;
        if (from.x >= to.x || from.y >= to.y || from.z >= to.z)
            return result;
        const glm::ivec3 ccMin = toChunkCoord(from);
        const glm::ivec3 ccMax = toChunkCoord(to - glm::ivec3(1));
        for (int cy = ccMin.y; cy <= ccMax.y; ++cy)
            for (int cz = ccMin.z; cz <= ccMax.z; ++cz)
                for (int cx = ccMin.x; cx <= ccMax.x; ++cx) {
                    auto it = m_chunks.find(glm::ivec3{ cx, cy, cz });
                    if (it != m_chunks.end())
                        result.push_back(&it->second);
                }
        return result;
    }

    Chunk& getChunk(glm::ivec3 pos) {
        return const_cast<Chunk&>(std::as_const(*this).getChunk(pos));
    }
    const Chunk& getChunk(glm::ivec3 pos) const {
        const glm::ivec3 cc = toChunkCoord(pos);
        auto it = m_chunks.find(cc);
        if (it == m_chunks.end())
            throw std::out_of_range("ChunkData: no chunk contains the position");
        return it->second;
    }

    Chunk* tryGetChunk(glm::ivec3 pos) noexcept {
        auto it = m_chunks.find(toChunkCoord(pos));
        return it == m_chunks.end() ? nullptr : &it->second;
    }
    const Chunk* tryGetChunk(glm::ivec3 pos) const noexcept {
        auto it = m_chunks.find(toChunkCoord(pos));
        return it == m_chunks.end() ? nullptr : &it->second;
    }

    bool hasChunk(glm::ivec3 pos) const noexcept {
        return m_chunks.find(toChunkCoord(pos)) != m_chunks.end();
	}

    size_t size() const noexcept { return m_chunks.size(); }
    bool empty() const noexcept { return m_chunks.empty(); }

    void clear() {
        m_chunks.clear();
        m_minPos = glm::ivec3(0);
        m_maxPos = glm::ivec3(0);
        m_hasBounds = false;
    }

    glm::ivec3 minPos() const noexcept { return m_minPos; }
    glm::ivec3 maxPos() const noexcept { return m_maxPos; }
    bool hasBounds() const noexcept { return m_hasBounds; }
private:
    Chunk& getOrCreateChunk(glm::ivec3 pos) {
        const glm::ivec3 cc = toChunkCoord(pos);
        auto it = m_chunks.find(cc);
        if (it == m_chunks.end()) {
            const glm::ivec3 minPos = cc * Chunk::SIZE;
            const glm::ivec3 maxPos = minPos + glm::ivec3(Chunk::SIZE);
            it = m_chunks.emplace(cc, Chunk(minPos, maxPos)).first;
            updateBoundsCached(minPos, maxPos);
        }
        return it->second;
    }

    void updateBoundsCached(glm::ivec3 chunkMin, glm::ivec3 chunkMax) noexcept {
        if (!m_hasBounds) {
            m_minPos = chunkMin;
            m_maxPos = chunkMax;
            m_hasBounds = true;
        }
        else {
            m_minPos = glm::min(m_minPos, chunkMin);
            m_maxPos = glm::max(m_maxPos, chunkMax);
        }
    }

    static glm::ivec3 toChunkCoord(glm::ivec3 pos) noexcept {
        const int S = Chunk::SIZE;
        return { floorDiv(pos.x, S), floorDiv(pos.y, S), floorDiv(pos.z, S) };
    }
    static int floorDiv(int a, int b) noexcept {
        const int q = a / b;
        const int r = a % b;
        return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
    }

    std::unordered_map<glm::ivec3, Chunk> m_chunks;
    glm::ivec3 m_minPos{ 0 };
    glm::ivec3 m_maxPos{ 0 };
    bool m_hasBounds = false;
};