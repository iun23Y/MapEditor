#include "schematic.h"
#include "helper.h"
#include "NBTManager.h"
#include "BlockPalette.h"

#include <SFML/Graphics/Color.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <zlib.h>

using namespace NBT;

namespace {
    std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& compressed) {
        z_stream strm{};
        strm.avail_in = static_cast<uInt>(compressed.size());
        strm.next_in = const_cast<Bytef*>(compressed.data());
        if (inflateInit2(&strm, 31) != Z_OK)
            throw std::runtime_error("inflateInit2 failed");

        std::vector<uint8_t> output;
        uint8_t buffer[8192];
        int ret;
        do {
            strm.avail_out = sizeof(buffer);
            strm.next_out = buffer;
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                inflateEnd(&strm);
                throw std::runtime_error("gzip decompression failed");
            }
            output.insert(output.end(), buffer, buffer + (sizeof(buffer) - strm.avail_out));
        } while (ret != Z_STREAM_END);
        inflateEnd(&strm);
        return output;
    }

    std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& data) {
        z_stream strm{};
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8,
            Z_DEFAULT_STRATEGY) != Z_OK)
            throw std::runtime_error("deflateInit2 failed");

        strm.avail_in = static_cast<uInt>(data.size());
        strm.next_in = const_cast<Bytef*>(data.data());

        std::vector<uint8_t> output;
        uint8_t buffer[8192];
        int ret;
        do {
            strm.avail_out = sizeof(buffer);
            strm.next_out = buffer;
            ret = deflate(&strm, Z_FINISH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                deflateEnd(&strm);
                throw std::runtime_error("gzip compression failed");
            }
            output.insert(output.end(), buffer, buffer + (sizeof(buffer) - strm.avail_out));
        } while (ret != Z_STREAM_END);
        deflateEnd(&strm);
        return output;
    }
}

SchematicMap::SchematicMap(const std::string& filename, const std::string& worldDir) {
    worldPath = fs::path(worldDir);
    chunksPath = worldPath / "chunks";
    fs::create_directories(chunksPath);

    loadPalette();
    loadMeta();

    if (!filename.empty())
        loadFromFile(filename);
}

void SchematicMap::loadPalette() {
    std::ifstream in(worldPath / "palette.dat");
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        try {
            const int id = std::stoi(line.substr(0, space));
            const std::string name = line.substr(space + 1);
            if (!name.empty()) palette.addBlock(id, name);
        }
        catch (...) {}
    }
}
void SchematicMap::savePalette() const {
    std::vector<std::pair<int, std::string>> entries(
        palette.nameById.begin(), palette.nameById.end());
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::ofstream out(worldPath / "palette.dat", std::ios::trunc);
    if (!out) return;
    for (const auto& [id, name] : entries)
        out << id << ' ' << name << '\n';
}
void SchematicMap::loadMeta() {
    struct MetaData { int32_t hasBounds; int32_t x1, y1, z1; int32_t x2, y2, z2; };
    std::ifstream in(worldPath / "meta.dat", std::ios::binary);
    if (!in) return;
    MetaData m{};
    in.read(reinterpret_cast<char*>(&m), sizeof(m));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(m))) return;
    hasBounds = m.hasBounds != 0;
    Pos1 = sf::Vector3i(m.x1, m.y1, m.z1);
    Pos2 = sf::Vector3i(m.x2, m.y2, m.z2);
}
void SchematicMap::saveMeta() const {
    struct MetaData { int32_t hasBounds; int32_t x1, y1, z1; int32_t x2, y2, z2; };
    MetaData m{};
    m.hasBounds = hasBounds ? 1 : 0;
    m.x1 = Pos1.x; m.y1 = Pos1.y; m.z1 = Pos1.z;
    m.x2 = Pos2.x; m.y2 = Pos2.y; m.z2 = Pos2.z;
    std::ofstream out(worldPath / "meta.dat", std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(reinterpret_cast<const char*>(&m), sizeof(m));
}
void SchematicMap::saveWorldState() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    savePalette();
    saveMeta();
    const_cast<SchematicMap*>(this)->saveDirtyChunks();
}

sf::Vector2f SchematicMap::worldToLocal(const sf::Vector2f& p) const {
    return { p.x - (float)Pos1.x, p.y - (float)Pos1.z };
}
sf::Vector2f SchematicMap::localToWorld(const sf::Vector2f& p) const {
    return { p.x + (float)Pos1.x, p.y + (float)Pos1.z };
}
sf::Vector2i SchematicMap::worldToLocal(const sf::Vector2i& p) const {
    return { p.x - Pos1.x, p.y - Pos1.z };
}
sf::Vector2i SchematicMap::localToWorld(const sf::Vector2i& p) const {
    return { p.x + Pos1.x, p.y + Pos1.z };
}

int SchematicMap::getRegionCoord(int coordinate) { return floorDiv(coordinate, Chunk::SIZE); }
std::string SchematicMap::getRegionName(int x, int y, int z) {
    return std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
}
fs::path SchematicMap::getRegionPath(const fs::path& dir, const std::string& name) {
    return dir / (name + ".region");
}

fs::path SchematicMap::chunkPath(int cx, int cy, int cz) const {
    return chunksPath / (std::to_string(cx) + "_" +
        std::to_string(cy) + "_" +
        std::to_string(cz) + ".vchunk");
}

void SchematicMap::touchLRU(const ChunkKey& k) const {
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

void SchematicMap::evictIfNeeded() const {
    while (chunkCache.size() > MAX_CACHED_CHUNKS && !lruList.empty()) {
        const ChunkKey victim = lruList.back();
        lruList.pop_back();
        lruPos.erase(victim);
        auto it = chunkCache.find(victim);
        if (it != chunkCache.end()) {
            if (it->second->dirty) saveChunkToDisk(victim, *it->second);
            chunkCache.erase(it);
        }
    }
}

std::shared_ptr<Chunk> SchematicMap::getChunk(int cx, int cy, int cz) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const ChunkKey k{ cx, cy, cz };
    auto it = chunkCache.find(k);
    if (it != chunkCache.end()) {
        touchLRU(k);
        return it->second;
    }
    auto chunk = loadChunkFromDisk(cx, cy, cz);
    chunkCache[k] = chunk;
    touchLRU(k);
    evictIfNeeded();
    return chunk;
}

void SchematicMap::saveChunkToDisk(const ChunkKey& k, const Chunk& c) const {
    const fs::path p = chunkPath(k.x, k.y, k.z);
    if (c.isEmpty()) { std::error_code ec; fs::remove(p, ec); return; }

    auto rle = c.encodeRLE();   // без gzip!
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { std::cerr << "cannot write " << p << "\n"; return; }
    f.write(reinterpret_cast<const char*>(rle.data()),
        static_cast<std::streamsize>(rle.size()));
}

std::shared_ptr<Chunk> SchematicMap::loadChunkFromDisk(int cx, int cy, int cz) const {
    auto chunk = std::make_shared<Chunk>();
    const fs::path p = chunkPath(cx, cy, cz);
    std::ifstream f(p, std::ios::binary);
    if (!f) return chunk;

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    if (raw.empty()) return chunk;
    chunk->decodeRLE(raw);   // без gzip!
    return chunk;
}

void SchematicMap::saveDirtyChunks() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto& [k, c] : chunkCache) {
        if (c->dirty) {
            saveChunkToDisk(k, *c);
            c->dirty = false;
        }
    }
}
void SchematicMap::saveAllChunks() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto& [k, c] : chunkCache) {
        saveChunkToDisk(k, *c);
        c->dirty = false;
    }
}
void SchematicMap::clearRegionCache() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    saveDirtyChunks();
    chunkCache.clear();
    lruList.clear();
    lruPos.clear();
}
void SchematicMap::invalidateRegionCache(int regionX, int regionZ) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto it = chunkCache.begin(); it != chunkCache.end();) {
        if (it->first.x * Chunk::SIZE == regionX &&
            it->first.z * Chunk::SIZE == regionZ)
            it = chunkCache.erase(it);
        else
            ++it;
    }
}

void SchematicMap::extendBounds(int x, int y, int z) {
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

int SchematicMap::getBlock(int x, int y, int z) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const int S = Chunk::SIZE;
    const int cx = floorDiv(x, S), cy = floorDiv(y, S), cz = floorDiv(z, S);
    auto chunk = getChunk(cx, cy, cz);
    const uint16_t id = chunk->get(x - cx * S, y - cy * S, z - cz * S);
    return id == Chunk::AIR ? -1 : (int)id;
}

void SchematicMap::setBlock(int x, int y, int z, int blockId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const int S = Chunk::SIZE;
    const int cx = floorDiv(x, S), cy = floorDiv(y, S), cz = floorDiv(z, S);
    auto chunk = getChunk(cx, cy, cz);

    const uint16_t id = (blockId < 0) ? Chunk::AIR
        : (uint16_t)blockId;
    chunk->set(x - cx * S, y - cy * S, z - cz * S, id);

    if (id != Chunk::AIR) extendBounds(x, y, z);
}

void SchematicMap::setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (const auto& [x, y, z, id] : blocks) {
        if (id >= 0) setBlock(x, y, z, id);
        else         removeBlock(x, y, z);
    }
}

void SchematicMap::removeBlock(int x, int y, int z) {
    setBlock(x, y, z, -1);
}

bool SchematicMap::hasBlock(int x, int y, int z) const {
    return getBlock(x, y, z) >= 0;
}

std::vector<SchematicMap::RegionBlock> SchematicMap::getBlocksInArea(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) const {
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

std::vector<SchematicMap::RegionBlock> SchematicMap::getTopBlocksInArea(int minX, int minZ, int maxX, int maxZ) const {
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
                            const std::size_t ti = static_cast<std::size_t>(z - minZ) * w + (x - minX);
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
            const std::size_t ti = static_cast<std::size_t>(z - minZ) * w + (x - minX);
            if (topId[ti] < 0) continue;
            result.push_back({ x, topYArr[ti], z, topId[ti] });
        }
    return result;
}

std::vector<SchematicMap::RegionBlock> SchematicMap::getRegionBlocks(int regionX, int regionY, int regionZ) const {
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

void SchematicMap::loadFromFile(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open schematic: " + filename);

    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    std::vector<uint8_t> nbtData;
    if (fileData.size() >= 2 && fileData[0] == 0x1F && fileData[1] == 0x8B)
        nbtData = gzipDecompress(fileData);
    else
        nbtData = std::move(fileData);

    NBTReader reader(nbtData);
    auto rootTag = reader.readTag();
    if (!rootTag || rootTag->type != TagType::TAG_COMPOUND)
        throw std::runtime_error("Root NBT tag is not compound");

    auto root = std::dynamic_pointer_cast<TagCompound>(rootTag);
    std::shared_ptr<TagCompound> schem = root;

    auto schemIt = root->children.find("Schematic");
    if (schemIt != root->children.end() && schemIt->second->type == TagType::TAG_COMPOUND)
        schem = std::dynamic_pointer_cast<TagCompound>(schemIt->second);

    auto getNumber = [&](const std::string& name) -> int {
        auto it = schem->children.find(name);
        if (it == schem->children.end()) return 0;
        if (it->second->type == TagType::TAG_SHORT)
            return std::dynamic_pointer_cast<TagShort>(it->second)->value;
        if (it->second->type == TagType::TAG_INT)
            return std::dynamic_pointer_cast<TagInt>(it->second)->value;
        return 0;
        };

    const int width = getNumber("Width");
    const int height = getNumber("Height");
    const int length = getNumber("Length");
    if (width <= 0 || height <= 0 || length <= 0)
        throw std::runtime_error("Invalid schematic dimensions");

    const std::int64_t volume64 = (std::int64_t)width * height * length;
    if (volume64 > std::numeric_limits<int>::max())
        throw std::runtime_error("Schematic is too large");

    auto blocksIt = schem->children.find("Blocks");
    if (blocksIt == schem->children.end() || blocksIt->second->type != TagType::TAG_COMPOUND)
        throw std::runtime_error("Blocks compound not found");

    auto blocks = std::dynamic_pointer_cast<TagCompound>(blocksIt->second);
    std::unordered_map<int, int> idRemap;

    auto paletteIt = blocks->children.find("Palette");
    if (paletteIt != blocks->children.end() && paletteIt->second->type == TagType::TAG_COMPOUND) {
        auto pal = std::dynamic_pointer_cast<TagCompound>(paletteIt->second);
        for (const auto& [name, tag] : pal->children) {
            if (tag->type != TagType::TAG_INT) continue;
            if (name == "minecraft:__reserved__") continue;

            const int schematicId = std::dynamic_pointer_cast<TagInt>(tag)->value;
            int worldId;
            if (palette.hasBlock(name)) {
                worldId = palette.getId(name);
            }
            else {
                worldId = (int)palette.nameById.size();
                while (palette.hasBlock(worldId)) ++worldId;
                palette.addBlock(worldId, name);
            }
            idRemap[schematicId] = worldId;
        }
    }

    std::vector<uint8_t> rawData;
    auto dataIt = blocks->children.find("Data");
    if (dataIt != blocks->children.end() && dataIt->second->type == TagType::TAG_BYTE_ARRAY) {
        rawData = std::dynamic_pointer_cast<TagByteArray>(dataIt->second)->value;
    }
    else {
        auto blockDataIt = blocks->children.find("BlockData");
        if (blockDataIt != blocks->children.end() && blockDataIt->second->type == TagType::TAG_BYTE_ARRAY)
            rawData = std::dynamic_pointer_cast<TagByteArray>(blockDataIt->second)->value;
        else
            throw std::runtime_error("Block data not found");
    }

    int offsetX = 0, offsetY = 0, offsetZ = 0;
    auto offsetIt = schem->children.find("Offset");
    if (offsetIt != schem->children.end() && offsetIt->second->type == TagType::TAG_INT_ARRAY) {
        auto offset = std::dynamic_pointer_cast<TagIntArray>(offsetIt->second)->value;
        if (offset.size() >= 3) {
            offsetX = offset[0];
            offsetY = offset[1];
            offsetZ = offset[2];
        }
    }

    if (!hasBounds) {
        Pos1 = sf::Vector3i(offsetX, offsetY, offsetZ);
        Pos2 = sf::Vector3i(offsetX + width, offsetY + height, offsetZ + length);
        hasBounds = true;
    }
    else {
        Pos1.x = std::min(Pos1.x, offsetX);
        Pos1.y = std::min(Pos1.y, offsetY);
        Pos1.z = std::min(Pos1.z, offsetZ);
        Pos2.x = std::max(Pos2.x, offsetX + width);
        Pos2.y = std::max(Pos2.y, offsetY + height);
        Pos2.z = std::max(Pos2.z, offsetZ + length);
    }

    const int volume = static_cast<int>(volume64);
    auto indices = decodeBlockIndices(rawData, volume);

    std::unordered_set<int> ignored;
    for (const auto& [id, name] : palette.nameById)
        if (name == "minecraft:air" || name == "air" ||
            name == "minecraft:__reserved__" || name == "__reserved__")
            ignored.insert(id);

    for (int y = 0; y < height; ++y) {
        for (int z = 0; z < length; ++z) {
            for (int x = 0; x < width; ++x) {
                const std::int64_t index = ((std::int64_t)y * length + z) * width + x;
                if (index >= (std::int64_t)indices.size()) continue;

                const int schematicId = indices[(std::size_t)index];
                auto remap = idRemap.find(schematicId);
                if (remap == idRemap.end()) continue;
                if (ignored.count(remap->second)) continue;

                setBlock(offsetX + x, offsetY + y, offsetZ + z, remap->second);
            }
        }
    }

    saveWorldState();
}

void SchematicMap::exportToSchematic(const std::string& baseName, const std::string& outputDir) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    fs::path outDir = fs::path(outputDir);
    fs::create_directories(outDir);

    if (!hasBounds) {
        std::cerr << "World has no blocks to export.\n";
        return;
    }

    constexpr int CELL_SIZE = 2000;

    const int worldMinX = Pos1.x, worldMaxX = Pos2.x - 1;
    const int worldMinY = Pos1.y, worldMaxY = Pos2.y - 1;
    const int worldMinZ = Pos1.z, worldMaxZ = Pos2.z - 1;

    const int cellsX = (worldMaxX - worldMinX + CELL_SIZE) / CELL_SIZE;
    const int cellsZ = (worldMaxZ - worldMinZ + CELL_SIZE) / CELL_SIZE;

    auto makeInt = [](const std::string& name, std::int32_t v) {
        auto t = std::make_shared<TagInt>();
        t->type = TagType::TAG_INT; t->name = name; t->value = v; return t;
        };
    auto makeShort = [](const std::string& name, std::int16_t v) {
        auto t = std::make_shared<TagShort>();
        t->type = TagType::TAG_SHORT; t->name = name; t->value = v; return t;
        };

    for (int ix = 0; ix < cellsX; ++ix) {
        for (int iz = 0; iz < cellsZ; ++iz) {
            const int minX = worldMinX + ix * CELL_SIZE;
            const int maxX = std::min(minX + CELL_SIZE - 1, worldMaxX);
            const int minZ = worldMinZ + iz * CELL_SIZE;
            const int maxZ = std::min(minZ + CELL_SIZE - 1, worldMaxZ);

            auto blocks = getBlocksInArea(minX, worldMinY, minZ, maxX, worldMaxY, maxZ);
            if (blocks.empty()) continue;

            std::unordered_map<int32_t, int32_t> globalToLocal;
            std::vector<std::string> paletteNames{ "minecraft:air" };
            globalToLocal.reserve(256);

            for (const auto& b : blocks) {
                if (globalToLocal.count(b.blockId)) continue;
                std::string name = palette.getName(b.blockId);
                if (name.empty()) globalToLocal[b.blockId] = 0;
                else {
                    globalToLocal[b.blockId] = (int32_t)paletteNames.size();
                    paletteNames.push_back(std::move(name));
                }
            }

            const int width = maxX - minX + 1;
            const int height = worldMaxY - worldMinY + 1;
            const int length = maxZ - minZ + 1;

            const std::size_t volume =
                (std::size_t)width * height * length;
            std::vector<int32_t> indices(volume, 0);

            for (const auto& b : blocks) {
                const std::size_t index =
                    ((std::size_t)(b.y - worldMinY) * length +
                        (std::size_t)(b.z - minZ)) * width +
                    (std::size_t)(b.x - minX);
                indices[index] = globalToLocal[b.blockId];
            }

            std::vector<uint8_t> blockData;
            blockData.reserve(indices.size());
            for (int32_t value : indices) {
                uint32_t v = (uint32_t)value;
                do {
                    uint8_t byte = (uint8_t)(v & 0x7F);
                    v >>= 7;
                    if (v != 0) byte |= 0x80;
                    blockData.push_back(byte);
                } while (v != 0);
            }

            auto root = std::make_shared<TagCompound>();
            root->type = TagType::TAG_COMPOUND;
            root->name = "";

            auto schem = std::make_shared<TagCompound>();
            schem->type = TagType::TAG_COMPOUND;
            schem->name = "Schematic";

            schem->children["Version"] = makeInt("Version", 3);
            schem->children["DataVersion"] = makeInt("DataVersion", 3700);
            schem->children["Width"] = makeShort("Width", width);
            schem->children["Height"] = makeShort("Height", height);
            schem->children["Length"] = makeShort("Length", length);

            auto offset = std::make_shared<TagIntArray>();
            offset->type = TagType::TAG_INT_ARRAY;
            offset->name = "Offset";
            offset->value = { minX, worldMinY, minZ };
            schem->children["Offset"] = offset;

            auto metadata = std::make_shared<TagCompound>();
            metadata->type = TagType::TAG_COMPOUND;
            metadata->name = "Metadata";
            auto author = std::make_shared<TagString>();
            author->type = TagType::TAG_STRING; author->name = "author"; author->value = "MapEditor";
            auto mname = std::make_shared<TagString>();
            mname->type = TagType::TAG_STRING; mname->name = "name"; mname->value = "ME-BTE-schematic";
            metadata->children["author"] = author;
            metadata->children["name"] = mname;
            schem->children["Metadata"] = metadata;

            auto blocksTag = std::make_shared<TagCompound>();
            blocksTag->type = TagType::TAG_COMPOUND;
            blocksTag->name = "Blocks";

            auto paletteTag = std::make_shared<TagCompound>();
            paletteTag->type = TagType::TAG_COMPOUND;
            paletteTag->name = "Palette";
            for (std::size_t i = 0; i < paletteNames.size(); ++i)
                paletteTag->children[paletteNames[i]] = makeInt(paletteNames[i], (int32_t)i);
            blocksTag->children["Palette"] = paletteTag;

            auto dataTag = std::make_shared<TagByteArray>();
            dataTag->type = TagType::TAG_BYTE_ARRAY;
            dataTag->name = "Data";
            dataTag->value = std::move(blockData);
            blocksTag->children["Data"] = dataTag;

            schem->children["Blocks"] = blocksTag;
            root->children["Schematic"] = schem;

            std::vector<uint8_t> nbt;
            writeTag(root, nbt);
            auto compressed = gzipCompress(nbt);

            std::string outName = baseName + "-" + std::to_string(ix) + "x" + std::to_string(iz) + ".schem";
            fs::path output = outDir / outName;
            std::ofstream out(output, std::ios::binary);
            if (!out) { std::cerr << "Failed to create " << output << "\n"; continue; }
            out.write(reinterpret_cast<const char*>(compressed.data()),
                static_cast<std::streamsize>(compressed.size()));
            std::cout << "Exported " << output << " (" << blocks.size() << " blocks)\n";
        }
    }
}