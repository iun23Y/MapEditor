#include "WorldMap.h"

#include <algorithm>
#include <fstream>
#include <iostream>

WorldMap::WorldMap(const std::string& filename,
    const std::string& worldDir,
    const BlockPalette& palette)
    : m_palette(palette)
    , m_io(worldDir)
{
    WorldMeta m = m_io.loadMeta();
    hasBounds = m.hasBounds;
    Pos1 = sf::Vector3i(m.x1, m.y1, m.z1);
    Pos2 = sf::Vector3i(m.x2, m.y2, m.z2);

    if (!filename.empty())
        loadFromFile(filename);
}

sf::Vector2f WorldMap::worldToLocal(const sf::Vector2f& p) const {
    return { p.x - (float)Pos1.x, p.y - (float)Pos1.z };
}
sf::Vector2f WorldMap::localToWorld(const sf::Vector2f& p) const {
    return { p.x + (float)Pos1.x, p.y + (float)Pos1.z };
}
sf::Vector2i WorldMap::worldToLocal(const sf::Vector2i& p) const {
    return { p.x - Pos1.x, p.y - Pos1.z };
}
sf::Vector2i WorldMap::localToWorld(const sf::Vector2i& p) const {
    return { p.x + Pos1.x, p.y + Pos1.z };
}

int WorldMap::getRegionCoord(int coordinate) { return floorDiv(coordinate, Chunk::SIZE); }

std::string WorldMap::getRegionName(int x, int y, int z) {
    return std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
}

fs::path WorldMap::getRegionPath(const fs::path& dir, const std::string& name) {
    return dir / (name + ".region");
}

void WorldMap::touchLRU(const ChunkKey& k) const {
    auto it = lruPos.find(k);
    if (it != lruPos.end()) {
        lruList.erase(it->second);
        lruList.push_front(k);
        it->second = lruList.begin();
    }
    else {
        lruList.push_front(k);
        lruPos[k] = lruList.begin();
    }
}

void WorldMap::evictIfNeeded() const {
    while (chunkCache.size() > MAX_CACHED_CHUNKS && !lruList.empty()) {
        const ChunkKey victim = lruList.back();
        lruList.pop_back();
        lruPos.erase(victim);
        auto it = chunkCache.find(victim);
        if (it != chunkCache.end()) {
            if (it->second->dirty)
                m_io.saveChunk(victim.x, victim.y, victim.z, *it->second);
            chunkCache.erase(it);
        }
    }
}

std::shared_ptr<Chunk> WorldMap::getChunk(int cx, int cy, int cz) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const ChunkKey k{ cx, cy, cz };
    auto it = chunkCache.find(k);
    if (it != chunkCache.end()) {
        touchLRU(k);
        return it->second;
    }
    auto chunk = m_io.loadChunk(cx, cy, cz);
    chunkCache[k] = chunk;
    touchLRU(k);
    evictIfNeeded();
    return chunk;
}

void WorldMap::clearRegionCache() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    saveWorldState();
    chunkCache.clear();
    lruList.clear();
    lruPos.clear();
}

void WorldMap::invalidateRegionCache(int regionX, int regionZ) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto it = chunkCache.begin(); it != chunkCache.end();) {
        if (it->first.x * Chunk::SIZE == regionX &&
            it->first.z * Chunk::SIZE == regionZ)
            it = chunkCache.erase(it);
        else
            ++it;
    }
}

void WorldMap::extendBounds(int x, int y, int z) {
    if (!hasBounds) {
        Pos1 = sf::Vector3i(x, y, z);
        Pos2 = sf::Vector3i(x + 1, y + 1, z + 1);
        hasBounds = true;
        return;
    }
    Pos1.x = std::min(Pos1.x, x);
    Pos1.y = std::min(Pos1.y, y);
    Pos1.z = std::min(Pos1.z, z);
    Pos2.x = std::max(Pos2.x, x + 1);
    Pos2.y = std::max(Pos2.y, y + 1);
    Pos2.z = std::max(Pos2.z, z + 1);
}

int WorldMap::getBlock(int x, int y, int z) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const int S = Chunk::SIZE;
    const int cx = floorDiv(x, S), cy = floorDiv(y, S), cz = floorDiv(z, S);
    auto chunk = getChunk(cx, cy, cz);
    const uint16_t id = chunk->get(x - cx * S, y - cy * S, z - cz * S);
    return id == Chunk::AIR ? -1 : (int)id;
}

void WorldMap::setBlock(int x, int y, int z, int blockId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const int S = Chunk::SIZE;
    const int cx = floorDiv(x, S), cy = floorDiv(y, S), cz = floorDiv(z, S);
    auto chunk = getChunk(cx, cy, cz);

    const uint16_t id = (blockId < 0) ? Chunk::AIR : (uint16_t)blockId;
    chunk->set(x - cx * S, y - cy * S, z - cz * S, id);

    if (id != Chunk::AIR) extendBounds(x, y, z);
}

void WorldMap::setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (const auto& [x, y, z, id] : blocks) {
        if (id >= 0) setBlock(x, y, z, id);
        else         removeBlock(x, y, z);
    }
}

void WorldMap::removeBlock(int x, int y, int z) {
    setBlock(x, y, z, -1);
}

bool WorldMap::hasBlock(int x, int y, int z) const {
    return getBlock(x, y, z) >= 0;
}

std::vector<WorldMap::RegionBlock>
WorldMap::getBlocksInArea(int minX, int minY, int minZ,
    int maxX, int maxY, int maxZ) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::vector<RegionBlock> result;
    if (minX > maxX || minY > maxY || minZ > maxZ) return result;

    const int S = Chunk::SIZE;
    const int cx0 = floorDiv(minX, S), cx1 = floorDiv(maxX, S);
    const int cy0 = floorDiv(minY, S), cy1 = floorDiv(maxY, S);
    const int cz0 = floorDiv(minZ, S), cz1 = floorDiv(maxZ, S);

    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cz = cz0; cz <= cz1; ++cz) {
                auto chunk = getChunk(cx, cy, cz);
                if (chunk->isEmpty()) continue;

                const int x0 = std::max(minX, cx * S);
                const int x1 = std::min(maxX, cx * S + S - 1);
                const int y0 = std::max(minY, cy * S);
                const int y1 = std::min(maxY, cy * S + S - 1);
                const int z0 = std::max(minZ, cz * S);
                const int z1 = std::min(maxZ, cz * S + S - 1);

                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x) {
                            const uint16_t id = chunk->get(x - cx * S, y - cy * S, z - cz * S);
                            if (id == Chunk::AIR) continue;
                            result.push_back({ x, y, z, (int)id });
                        }
            }
    return result;
}

std::vector<WorldMap::RegionBlock>
WorldMap::getTopBlocksInArea(int minX, int minZ, int maxX, int maxZ) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!hasBounds || minX > maxX || minZ > maxZ) return {};

    const int S = Chunk::SIZE;
    const int topY = Pos2.y - 1;
    const int botY = Pos1.y;

    const int w = maxX - minX + 1;
    const int d = maxZ - minZ + 1;
    const std::size_t total = static_cast<std::size_t>(w) * d;

    std::vector<int32_t> topId(total, -1);
    std::vector<int16_t> topYArr(total, std::numeric_limits<int16_t>::min());

    const int cyStart = floorDiv(topY, S);
    const int cyEnd = floorDiv(botY, S);
    std::size_t filled = 0;

    for (int cy = cyStart; cy >= cyEnd; --cy) {
        const int cxStart = floorDiv(minX, S), cxEnd = floorDiv(maxX, S);
        const int czStart = floorDiv(minZ, S), czEnd = floorDiv(maxZ, S);

        for (int cx = cxStart; cx <= cxEnd; ++cx) {
            for (int cz = czStart; cz <= czEnd; ++cz) {
                auto chunk = getChunk(cx, cy, cz);
                const int chunkMaxY = chunk->maxY();
                if (chunkMaxY < 0) continue;

                const int x0 = std::max(minX, cx * S);
                const int x1 = std::min(maxX, cx * S + S - 1);
                const int z0 = std::max(minZ, cz * S);
                const int z1 = std::min(maxZ, cz * S + S - 1);
                const int yLocalMax = std::min(chunkMaxY, topY - cy * S);
                const int yLocalMin = std::max(0, botY - cy * S);

                for (int ly = yLocalMax; ly >= yLocalMin; --ly) {
                    const int y = cy * S + ly;
                    for (int z = z0; z <= z1; ++z) {
                        for (int x = x0; x <= x1; ++x) {
                            const std::size_t ti =
                                static_cast<std::size_t>(z - minZ) * w + (x - minX);
                            if (topId[ti] >= 0) continue;

                            const uint16_t id = chunk->get(x - cx * S, ly, z - cz * S);
                            if (id != Chunk::AIR) {
                                topId[ti] = (int32_t)id;
                                topYArr[ti] = (int16_t)y;
                                ++filled;
                            }
                        }
                    }
                }
            }
        }
        if (filled >= total) break;
    }

    std::vector<RegionBlock> result;
    result.reserve(filled);
    for (int z = minZ; z <= maxZ; ++z)
        for (int x = minX; x <= maxX; ++x) {
            const std::size_t ti =
                static_cast<std::size_t>(z - minZ) * w + (x - minX);
            if (topId[ti] < 0) continue;
            result.push_back({ x, topYArr[ti], z, topId[ti] });
        }
    return result;
}

std::vector<WorldMap::RegionBlock>
WorldMap::getRegionBlocks(int regionX, int regionY, int regionZ) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    const int S = Chunk::SIZE;
    auto chunk = getChunk(regionX, regionY, regionZ);
    std::vector<RegionBlock> result;
    if (chunk->isEmpty()) return result;

    for (int ly = 0; ly < S; ++ly)
        for (int lz = 0; lz < S; ++lz)
            for (int lx = 0; lx < S; ++lx) {
                const uint16_t id = chunk->get(lx, ly, lz);
                if (id == Chunk::AIR) continue;
                result.push_back({
                    regionX * S + lx,
                    regionY * S + ly,
                    regionZ * S + lz,
                    (int)id
                    });
            }
    return result;
}

void WorldMap::loadFromFile(const std::string& filename) {
    SchematicData data = m_io.readSchematic(filename, m_palette);
    if (data.width <= 0 || data.height <= 0 || data.length <= 0) return;

    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (!hasBounds) {
        Pos1 = sf::Vector3i(data.offsetX, data.offsetY, data.offsetZ);
        Pos2 = sf::Vector3i(data.offsetX + data.width,
            data.offsetY + data.height,
            data.offsetZ + data.length);
        hasBounds = true;
    }
    else {
        Pos1.x = std::min(Pos1.x, data.offsetX);
        Pos1.y = std::min(Pos1.y, data.offsetY);
        Pos1.z = std::min(Pos1.z, data.offsetZ);
        Pos2.x = std::max(Pos2.x, data.offsetX + data.width);
        Pos2.y = std::max(Pos2.y, data.offsetY + data.height);
        Pos2.z = std::max(Pos2.z, data.offsetZ + data.length);
    }

    for (int y = 0; y < data.height; ++y) {
        for (int z = 0; z < data.length; ++z) {
            for (int x = 0; x < data.width; ++x) {
                const std::size_t i =
                    ((std::size_t)y * data.length + z) * data.width + x;
                const int32_t id = data.blockIds[i];
                if (id <= 0) continue;
                setBlock(data.offsetX + x, data.offsetY + y, data.offsetZ + z, id);
            }
        }
    }

    saveWorldState();
}

void WorldMap::exportToSchematic(const std::string& baseName,
    const std::string& outputDir) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    m_io.writeSchematicCells(*this, m_palette, baseName, outputDir);
}

void WorldMap::saveWorldState() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    WorldMeta m;
    m.hasBounds = hasBounds ? 1 : 0;
    m.x1 = Pos1.x; m.y1 = Pos1.y; m.z1 = Pos1.z;
    m.x2 = Pos2.x; m.y2 = Pos2.y; m.z2 = Pos2.z;
    m_io.saveMeta(m);

    for (auto& [k, c] : chunkCache) {
        if (c->dirty) {
            m_io.saveChunk(k.x, k.y, k.z, *c);
            c->dirty = false;
        }
    }
}