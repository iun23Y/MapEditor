#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

class Chunk {
public:
    static constexpr int SIZE = 100;
    static constexpr size_t VOLUME = static_cast<size_t>(SIZE) * SIZE * SIZE;
    static constexpr size_t LAYER = static_cast<size_t>(SIZE) * SIZE;
    static constexpr uint16_t AIR = 0;

    Chunk(glm::ivec3 minPos, glm::ivec3 maxPos)
        : Chunk(std::vector<uint16_t>(VOLUME, AIR), minPos, maxPos) {
    }
    Chunk(std::span<const uint16_t, VOLUME> blocks, glm::ivec3 minPos, glm::ivec3 maxPos)
        : m_data(blocks.begin(), blocks.end()), m_minPos(minPos), m_maxPos(maxPos)
    {
        if (!verifyPos()) throw std::invalid_argument("Chunk: invalid position");
        maxYTight_ = false;
    }
    Chunk(const std::vector<uint16_t>& blocks, glm::ivec3 minPos, glm::ivec3 maxPos)
        : m_data(blocks), m_minPos(minPos), m_maxPos(maxPos)
    {
        if (m_data.size() != VOLUME)
            throw std::invalid_argument("Chunk: wrong data size");
        if (!verifyPos())
            throw std::invalid_argument("Chunk: invalid position");
        maxYTight_ = false;
    }

    inline bool contains(glm::ivec3 worldPos) const noexcept {
        return worldPos.x >= m_minPos.x && worldPos.x < m_maxPos.x &&
            worldPos.y >= m_minPos.y && worldPos.y < m_maxPos.y &&
            worldPos.z >= m_minPos.z && worldPos.z < m_maxPos.z;
    }

    inline std::vector<glm::ivec3> get(uint16_t id) const {
        std::vector<glm::ivec3> result;
        if (id == AIR) return result;

        const int ymax = maxY();
        if (ymax < 0) return result;

        for (int y = 0; y <= ymax; ++y)
            for (int z = 0; z < SIZE; ++z)
                for (int x = 0; x < SIZE; ++x)
                    if (m_data[index(x, y, z)] == id)
                        result.push_back(m_minPos + glm::ivec3(x, y, z));

        return result;
    }
    inline uint16_t get(glm::ivec3 worldPos) const {
        if (!contains(worldPos)) {
            assert(false && "Chunk::get: worldPos outside chunk bounds");
            return AIR;
        }
        const glm::ivec3 l = worldPos - m_minPos;
        return m_data[index(l.x, l.y, l.z)];
    }

    inline void set(glm::ivec3 worldPos, uint16_t id) {
        if (!contains(worldPos)) {
            assert(false && "Chunk::set: worldPos outside chunk bounds");
            return;
        }
        const glm::ivec3 l = worldPos - m_minPos;
        setLocal(l.x, l.y, l.z, id);
    }
    inline void set(const std::unordered_map<glm::ivec3, int>& worldBlocks) {
        bool anyDirty = false;
        int  newMaxY = m_maxY;
        bool removedTop = false;

        for (const auto& [wpos, rawId] : worldBlocks) {
            if (!contains(wpos)) continue;

            const uint16_t id = (rawId < 0 || rawId > 65535)
                ? AIR
                : static_cast<uint16_t>(rawId);

            const glm::ivec3 l = wpos - m_minPos;
            auto& block = m_data[index(l.x, l.y, l.z)];
            if (block == id) continue;

            if (block == AIR && id != AIR) {
                if (l.y > newMaxY) newMaxY = l.y;
            }
            else if (block != AIR && id == AIR) {
                if (l.y == m_maxY) removedTop = true;
            }
            block = id;
            anyDirty = true;
        }

        if (newMaxY > m_maxY) {
            m_maxY = newMaxY;
        }
        else if (removedTop) {
            maxYTight_ = false;
        }
        if (anyDirty) m_dirty = true;
    }
    inline void set(const std::vector<glm::ivec3>& worldPositions, uint16_t id) {
        bool anyDirty = false;
        int  newMaxY = m_maxY;
        bool removedTop = false;

        for (const auto& wpos : worldPositions) {
            if (!contains(wpos)) continue;

            const glm::ivec3 l = wpos - m_minPos;
            auto& block = m_data[index(l.x, l.y, l.z)];
            if (block == id) continue;

            if (block == AIR && id != AIR) {
                if (l.y > newMaxY) newMaxY = l.y;
            }
            else if (block != AIR && id == AIR) {
                if (l.y == m_maxY) removedTop = true;
            }
            block = id;
            anyDirty = true;
        }

        if (newMaxY > m_maxY) {
            m_maxY = newMaxY;
        }
        else if (removedTop) {
            maxYTight_ = false;
        }
        if (anyDirty) m_dirty = true;
    }

    inline std::span<const uint16_t, VOLUME> data() const noexcept {
        return std::span<const uint16_t, VOLUME>(m_data.data(), VOLUME);
    }

    inline std::span<const uint16_t> getLayer(int worldY) const {
        if (worldY < m_minPos.y || worldY >= m_maxPos.y) {
            assert(false && "Chunk::getLayer: worldY outside chunk bounds");
            return {};
        }
        const int localY = worldY - m_minPos.y;
        return std::span<const uint16_t>(
            m_data.data() + static_cast<size_t>(localY) * LAYER, LAYER);
    }

    int maxY() const {
        if (!maxYTight_) recomputeMaxY();
        return m_maxY;
    }
    int getMaxWorldY() const {
        const int m = maxY();
        return m < 0 ? INT_MIN : m + m_minPos.y;
    }

    bool isEmpty() const { return maxY() < 0; }

    void clear() {
        std::fill(m_data.begin(), m_data.end(), AIR);
        m_maxY = INT_MIN;
        maxYTight_ = true;
        m_dirty = true;
    }

    std::vector<uint8_t> encodeRLE() const;
    bool decodeRLE(const std::vector<uint8_t>& data);

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void markClean() { m_dirty = false; }

    glm::ivec3 getMinPos() const { return m_minPos; }
    glm::ivec3 getMaxPos() const { return m_maxPos; }

private:
    inline uint16_t getLocal(int x, int y, int z) const {
        assert(x >= 0 && x < SIZE &&
            y >= 0 && y < SIZE &&
            z >= 0 && z < SIZE);
        return m_data[index(x, y, z)];
    }
    inline void setLocal(int x, int y, int z, uint16_t id) {
        assert(x >= 0 && x < SIZE &&
            y >= 0 && y < SIZE &&
            z >= 0 && z < SIZE);

        auto& block = m_data[index(x, y, z)];
        if (block == id) return;

        if (block == AIR && id != AIR) {
            if (y > m_maxY) m_maxY = y;
        }
        else if (block != AIR && id == AIR) {
            if (y == m_maxY) maxYTight_ = false;
        }
        block = id;
        m_dirty = true;
    }

    bool verifyPos() const {
        return isAligned(m_minPos)
            && isAligned(m_maxPos)
            && m_maxPos == m_minPos + glm::ivec3(SIZE);
    }
    static bool isAligned(glm::ivec3 p) {
        return p.x % SIZE == 0 && p.y % SIZE == 0 && p.z % SIZE == 0;
    }

    inline static size_t index(int x, int y, int z) {
        return (static_cast<size_t>(y) * SIZE + z) * SIZE + x;
    }

    void recomputeMaxY() const {
        const int start = (m_maxY >= 0) ? m_maxY : SIZE - 1;
        for (int y = start; y >= 0; --y) {
            for (int z = 0; z < SIZE; ++z) {
                for (int x = 0; x < SIZE; ++x) {
                    if (m_data[index(x, y, z)] != AIR) {
                        m_maxY = y;
                        maxYTight_ = true;
                        return;
                    }
                }
            }
        }
        m_maxY = INT_MIN;
        maxYTight_ = true;
    }

    std::vector<uint16_t> m_data;
    glm::ivec3 m_minPos{ 0 };
    glm::ivec3 m_maxPos{ 0 };
    mutable int m_maxY = INT_MIN;
    mutable bool maxYTight_ = true;
    bool m_dirty = false;
};