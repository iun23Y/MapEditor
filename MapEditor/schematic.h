#pragma once
#include "Chunk.h"
#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

class BlockPalette {
public:
    std::unordered_map<int, std::string> nameById;
    std::unordered_map<std::string, int> idByName;

    void addBlock(int id, const std::string& name);
    bool hasBlock(int id) const;
    bool hasBlock(const std::string& name) const;
    int  getId(const std::string& name) const;
    std::string getName(int id) const;
};

class SchematicMap {
public:
    static constexpr int REGION_SIZE = 256;

    static constexpr std::size_t MAX_CACHED_CHUNKS = 200;

    struct RegionRecord {
        int32_t x, z, id;
    };

    struct RegionKey {
        int x, y, z;
        bool operator==(const RegionKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct RegionKeyHash {
        std::size_t operator()(const RegionKey& k) const noexcept {
            std::size_t s = std::hash<int>{}(k.x);
            s ^= std::hash<int>{}(k.y) + 0x9e3779b9 + (s << 6) + (s >> 2);
            s ^= std::hash<int>{}(k.z) + 0x9e3779b9 + (s << 6) + (s >> 2);
            return s;
        }
    };

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

    SchematicMap(const std::string& filename, const std::string& worldDir);

    sf::Vector2f worldToLocal(const sf::Vector2f& worldPos) const;
    sf::Vector2f localToWorld(const sf::Vector2f& localPos) const;
    sf::Vector2i worldToLocal(const sf::Vector2i& worldPos) const;
    sf::Vector2i localToWorld(const sf::Vector2i& localPos) const;

    sf::Vector3i getPos1() const { return Pos1; }
    sf::Vector3i getPos2() const { return Pos2; }
    bool         getHasBounds() const { return hasBounds; }

    BlockPalette& getPalette() { return palette; }
    const BlockPalette& getPalette() const { return palette; }

    std::vector<RegionBlock> getRegionBlocks(int regionX, int regionY, int regionZ) const;
    std::vector<RegionBlock> getBlocksInArea(int minX, int minY, int minZ,
        int maxX, int maxY, int maxZ) const;
    std::vector<RegionBlock> getTopBlocksInArea(int minX, int minZ,
        int maxX, int maxZ) const;

    int  getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int blockId);
    void setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks);
    void removeBlock(int x, int y, int z);
    bool hasBlock(int x, int y, int z) const;

    void loadFromFile(const std::string& filename);
    void exportToSchematic(const std::string& baseName, const std::string& outputDir) const;
    void saveWorldState() const;

    void invalidateRegionCache(int regionX, int regionZ);
    void clearRegionCache();

    static int getRegionCoord(int coordinate);
    static std::string getRegionName(int regionX, int regionY, int regionZ);
    static fs::path getRegionPath(const fs::path& regionsDir, const std::string& regionName);

    std::shared_ptr<Chunk> getChunk(int cx, int cy, int cz) const;
    void saveDirtyChunks() const;
    void saveAllChunks() const;

private:
    fs::path worldPath;
    fs::path chunksPath;

    mutable std::recursive_mutex mutex;

    BlockPalette palette;
    sf::Vector3i Pos1{ 0, 0, 0 };
    sf::Vector3i Pos2{ 0, 0, 0 };
    bool         hasBounds = false;

    using ChunkPtr = std::shared_ptr<Chunk>;
    mutable std::unordered_map<ChunkKey, ChunkPtr, ChunkKeyHash> chunkCache;
    mutable std::unordered_map<ChunkKey, std::list<ChunkKey>::iterator, ChunkKeyHash> lruPos;
    mutable std::list<ChunkKey> lruList;

    void loadPalette();
    void savePalette() const;
    void loadMeta();
    void saveMeta() const;
    void extendBounds(int x, int y, int z);

    static int floorDiv(int a, int b) {
        int q = a / b, r = a % b;
        return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
    }

    fs::path chunkPath(int cx, int cy, int cz) const;
    ChunkPtr loadChunkFromDisk(int cx, int cy, int cz) const;
    void saveChunkToDisk(const ChunkKey& k, const Chunk& c) const;
    void touchLRU(const ChunkKey& k) const;
    void evictIfNeeded() const;
};