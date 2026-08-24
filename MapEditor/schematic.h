#pragma once

#include "helper.h"

#include <unordered_set>
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector3.hpp>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

struct BlockPalette {
    std::unordered_map<int, std::string> nameById;
    std::unordered_map<std::string, int> idByName;

    void addBlock(int id, const std::string& name);
    bool hasBlock(int id) const;
    bool hasBlock(const std::string& name) const;
    int getId(const std::string& name) const;
    std::string getName(int id) const;
};

namespace std {
    template<>
    struct hash<sf::Vector3i> {
        std::size_t operator()(const sf::Vector3i& v) const noexcept {
            std::size_t h1 = std::hash<int>{}(v.x);
            std::size_t h2 = std::hash<int>{}(v.y);
            std::size_t h3 = std::hash<int>{}(v.z);
            std::size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}

class SchematicMap {
public:
    static constexpr int REGION_SIZE = 1000;

    static constexpr std::size_t MAX_CACHED_REGIONS = 64;

    struct RegionBlock {
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t blockId;
    };

    struct RegionRecord {
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t id;
    };

    struct RegionKey {
        int32_t x;
        int32_t y;
        int32_t z;
        bool operator==(const RegionKey& other) const noexcept {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct RegionKeyHash {
        std::size_t operator()(const RegionKey& key) const noexcept {
            std::size_t seed = std::hash<int32_t>{}(key.x);
            seed ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<int32_t>{}(key.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

private:
    struct RegionData {
        std::vector<RegionRecord> blocks;
        std::vector<int32_t> index;
        bool indexed = false;
        std::uint64_t lastUsed = 0;
        void ensureIndex();
    };
    mutable std::uint64_t cacheClock = 0;
    bool hasBounds = false;
    sf::Vector3i Pos1{};
    sf::Vector3i Pos2{};
    std::filesystem::path worldPath;
    BlockPalette palette;

    mutable std::unordered_map<RegionKey, std::shared_ptr<RegionData>, RegionKeyHash> regionCache;
    mutable std::recursive_mutex mutex;

    mutable bool dirIndexValid = false;
    mutable std::unordered_set<RegionKey, RegionKeyHash> knownRegionFiles;
    void rebuildRegionDirIndex() const;

    static int getRegionCoord(int coordinate);
    static std::string getRegionName(int regionX, int regionY, int regionZ);
    static std::filesystem::path getRegionPath(const std::filesystem::path& regionsDir, const std::string& regionName);

    static std::unordered_map<sf::Vector3i, int32_t> loadRegionBlocks(const std::filesystem::path& filePath);
    static void writeRegionBlocks(const std::filesystem::path& filePath, const std::unordered_map<sf::Vector3i, int32_t>& blocks);

    std::shared_ptr<RegionData> getRegionData(int regionX, int regionY, int regionZ) const;
    void extendBounds(int x, int y, int z);

    void loadPalette();
    void savePalette() const;
    void loadMeta();
    void saveMeta() const;

    void invalidateRegionCache(int regionX, int regionZ);
    void clearRegionCache();

public:
    explicit SchematicMap(const std::string& filename, const std::string& worldDir = "world");

    void loadFromFile(const std::string& filename);
    void exportToSchematic(const std::string& baseName, const std::string& outputDir) const;
    void saveWorldState() const;

    int getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int blockId);
    void setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks);
    void removeBlock(int x, int y, int z);
    bool hasBlock(int x, int y, int z) const;

    std::vector<RegionBlock> getRegionBlocks(int regionX, int regionY, int regionZ) const;
    std::vector<RegionBlock> getBlocksInArea(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) const;
    std::vector<RegionBlock> getTopBlocksInArea(int minX, int minZ, int maxX, int maxZ) const;

    BlockPalette& getPalette() { return palette; }
    const BlockPalette& getPalette() const { return palette; }

    sf::Vector3i getPos1() const { return Pos1; }
    sf::Vector3i getPos2() const { return Pos2; }
    bool hasWorldBounds() const { return hasBounds; }

    std::filesystem::path getWorldPath() const { return worldPath; }
};

std::pair<std::int32_t, std::size_t> readVarint(const std::vector<std::uint8_t>& data, std::size_t offset);
std::vector<std::int32_t> decodeBlockIndices(const std::vector<std::uint8_t>& rawData, int expectedCount);