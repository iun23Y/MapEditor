#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------// Block Palette
struct BlockPalette {
    std::unordered_map<int, std::string> nameById;
    std::unordered_map<std::string, int> idByName;

    void addBlock(int id, const std::string& name);
    bool hasBlock(int id) const;
    bool hasBlock(const std::string& name) const;
    int getId(const std::string& name) const;
    std::string getName(int id) const;
};

// -----------------------------------------------------------------------------// Hash for sf::Vector3i (SFML 3: sf::Vector3<int>)
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

// -----------------------------------------------------------------------------// Schematic Map - works with files on disk, does not keep everything in RAM
class SchematicMap {
private:
	bool hasBounds = false;
    sf::Vector3i Pos1;
    sf::Vector3i Pos2;
    std::filesystem::path worldPath;
    BlockPalette palette;

    // Internal helper: region file name
    static std::string getRegionName(int regionX, int regionY, int regionZ);
    static std::filesystem::path getRegionPath(const std::filesystem::path& regionsDir, const std::string& regionName);

public:
    explicit SchematicMap(const std::string& filename, const std::string& worldDir = "world");

    // Load schematic — immediately writes to disk, does not keep in memory
    void loadFromFile(const std::string& filename);

    // Get a block by absolute coordinates (reads from disk)
    int getBlock(int x, int y, int z) const;

    // Set or remove a block by absolute coordinates.
    // blockId < 0 means air / remove.
    void setBlock(int x, int y, int z, int blockId);
    void removeBlock(int x, int y, int z);

    // Get block color
    sf::Color getBlockColor(int blockId) const;
    sf::Color getBlockColor(int x, int y, int z) const;

    // Check whether a block exists (not air)
    bool hasBlock(int x, int y, int z) const;

    // Get all blocks from a region (for rendering)
    struct RegionBlock {
        int32_t x, y, z;
        int32_t blockId;
    };
    std::vector<RegionBlock> getRegionBlocks(int regionX, int regionY, int regionZ) const;

    // Get blocks for the visible area (for rendering)
    std::vector<RegionBlock> getBlocksInArea(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) const;

    const BlockPalette& getPalette() const { return palette; }
    sf::Vector3i getPos1() const { return Pos1; }
    sf::Vector3i getPos2() const { return Pos2; }
    std::filesystem::path getWorldPath() const { return worldPath; }
};

// -----------------------------------------------------------------------------// Free functions
// -----------------------------------------------------------------------------std::vector<std::uint8_t> gzipDecompress(const std::vector<std::uint8_t>& compressed);
std::pair<std::int32_t, std::size_t> readVarint(const std::vector<std::uint8_t>& data, std::size_t offset);
std::vector<std::int32_t> decodeBlockIndices(const std::vector<std::uint8_t>& rawData, int expectedCount);
