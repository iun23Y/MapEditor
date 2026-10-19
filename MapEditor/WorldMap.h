#pragma once
#include "Chunk.h"
#include "BlockPalette.h"
//#include "WorldMapIO.h"

#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct WorldMeta {
    bool hasBounds = false;
    int x1 = 0, y1 = 0, z1 = 0;
    int x2 = 0, y2 = 0, z2 = 0;
};

class WorldMap {
public:
    static constexpr int REGION_SIZE = 256;
    static constexpr std::size_t MAX_CACHED_CHUNKS = 200;

    struct RegionBlock {
        int32_t x, y, z;
        int32_t blockId;
    };

    struct ChunkKey {
        int x, y, z;
        bool operator==(const ChunkKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct ChunkKeyHash {
        std::size_t operator()(const ChunkKey& k) const noexcept {
            std::size_t s = std::hash<int>{}(k.x);
            s ^= std::hash<int>{}(k.y) + 0x9e3779b9 + (s << 6) + (s >> 2);
            s ^= std::hash<int>{}(k.z) + 0x9e3779b9 + (s << 6) + (s >> 2);
            return s;
        }
    };

    WorldMap(const std::string& filename,
        const std::string& worldDir,
        const BlockPalette& palette);

    // bounds
    sf::Vector3i getPos1() const { return Pos1; }
    sf::Vector3i getPos2() const { return Pos2; }
    bool         getHasBounds() const { return hasBounds; }

    // world <-> local
    sf::Vector2f worldToLocal(const sf::Vector2f& p) const;
    sf::Vector2f localToWorld(const sf::Vector2f& p) const;
    sf::Vector2i worldToLocal(const sf::Vector2i& p) const;
    sf::Vector2i localToWorld(const sf::Vector2i& p) const;

    // access
    int  getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int blockId);
    void setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks);
    void removeBlock(int x, int y, int z);
    bool hasBlock(int x, int y, int z) const;

    std::vector<RegionBlock> getRegionBlocks(int rx, int ry, int rz) const;
    std::vector<RegionBlock> getBlocksInArea(int minX, int minY, int minZ,
        int maxX, int maxY, int maxZ) const;
    std::vector<RegionBlock> getTopBlocksInArea(int minX, int minZ,
        int maxX, int maxZ) const;

    std::shared_ptr<Chunk> getChunk(int cx, int cy, int cz) const;

    // cache
    void invalidateRegionCache(int regionX, int regionZ);
    void clearRegionCache();

    // I/O (тонкие обёртки над WorldMapIO)
    void loadFromFile(const std::string& filename);
    void exportToSchematic(const std::string& baseName,
        const std::string& outputDir) const;
    void saveWorldState() const;

    static int getRegionCoord(int coordinate);
    static std::string getRegionName(int x, int y, int z);
    static fs::path getRegionPath(const fs::path& dir, const std::string& name);

    const BlockPalette& palette() const { return m_palette; }

private:
    mutable std::recursive_mutex mutex;

    const BlockPalette& m_palette;
    sf::Vector3i Pos1{ 0, 0, 0 };
    sf::Vector3i Pos2{ 0, 0, 0 };
    bool hasBounds = false;

    using ChunkPtr = std::shared_ptr<Chunk>;
    mutable std::unordered_map<ChunkKey, ChunkPtr, ChunkKeyHash> chunkCache;
    mutable std::unordered_map<ChunkKey, std::list<ChunkKey>::iterator, ChunkKeyHash> lruPos;
    mutable std::list<ChunkKey> lruList;

    WorldMapIO m_io;

    void extendBounds(int x, int y, int z);
    void touchLRU(const ChunkKey& k) const;
    void evictIfNeeded() const;

    static int floorDiv(int a, int b) {
        int q = a / b, r = a % b;
        return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
    }
};