#pragma once

#include "Object.h"
#pragma once

#include "Object.h"
#include "ChunkData.h"
#include "ObjectTypes.h"
#include <vector>
#include <unordered_map>
#include <climits>
#include <cstdint>

struct ChunkIndexEntry {
    glm::ivec3 pos;
    uint32_t chunkId;
};

class BigObject : public Object
{
public:
    BigObject() : Object(ObjectType::BigObject) {}

    void setData(ChunkData&& data) { m_data = std::move(data); }
    ChunkData& data() { return m_data; }
    const ChunkData& data() const { return m_data; }

    void setChunkTable(std::vector<ChunkIndexEntry> table) {
        m_chunkTable = std::move(table);
        m_chunkIdByPos.clear();
        m_chunkIdByPos.reserve(m_chunkTable.size());
        uint32_t maxId = 0;
        for (auto& e : m_chunkTable) {
            m_chunkIdByPos[e.pos] = e.chunkId;
            if (e.chunkId > maxId) maxId = e.chunkId;
        }
        m_nextChunkId = maxId + 1;
        m_metaDirty = false;
    }

    const std::vector<ChunkIndexEntry>& getChunkTable() const { return m_chunkTable; }

    uint32_t chunkIdFor(glm::ivec3 pos) const {
        auto it = m_chunkIdByPos.find(pos);
        return it == m_chunkIdByPos.end() ? UINT32_MAX : it->second;
    }

    uint32_t allocateChunkId(glm::ivec3 pos) {
        auto it = m_chunkIdByPos.find(pos);
        if (it != m_chunkIdByPos.end()) return it->second;
        uint32_t id = m_nextChunkId++;
        m_chunkTable.push_back({ pos, id });
        m_chunkIdByPos[pos] = id;
        m_metaDirty = true;
        return id;
    }

    void addLoadedChunk(Chunk&& chunk) {
        glm::ivec3 pos = chunk.getMinPos();
        allocateChunkId(pos);
        m_data.addChunk(std::move(chunk));
    }

    bool isChunkLoaded(glm::ivec3 pos) const {
        return m_data.tryGetChunk(pos) != nullptr;
    }

    Bounds bounds() const override {
        if (m_chunkTable.empty()) {
            if (m_data.empty()) return Bounds{ position, position };
            return Bounds{ position + m_data.minPos(),
                           position + m_data.maxPos() };
        }
        glm::ivec3 mn{ INT_MAX }, mx{ INT_MIN };
        for (auto& e : m_chunkTable) {
            mn = glm::min(mn, e.pos);
            mx = glm::max(mx, e.pos + glm::ivec3(Chunk::SIZE));
        }
        return Bounds{ position + mn, position + mx };
    }

    bool isDirty() const {
        if (m_metaDirty) return true;
        for (const auto& [cc, chunk] : m_data.getChunks())
            if (chunk.isDirty()) return true;
        return false;
    }
    bool isMetaDirty() const { return m_metaDirty; }
    void markMetaDirty() { m_metaDirty = true; }
    void markMetaClean() { m_metaDirty = false; }

private:
    ChunkData m_data;
    std::vector<ChunkIndexEntry> m_chunkTable;
    std::unordered_map<glm::ivec3, uint32_t> m_chunkIdByPos;
    uint32_t m_nextChunkId = 0;
    bool m_metaDirty = false;
};