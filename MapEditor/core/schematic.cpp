#include "schematic.h"
#include "helper.h"
#include "NBTManager.h"
#include "BlockPalette.h"
#include "BOunds.h"

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

bool Schematic::loadForSchematic(const std::string& filename,
    BlockPalette& palette,
    std::vector<int>& outBlocks,
    Bounds& outBounds)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    std::vector<uint8_t> nbtData;
    if (fileData.size() >= 2 && fileData[0] == 0x1F && fileData[1] == 0x8B)
        nbtData = gzipDecompress(fileData);
    else
        nbtData = std::move(fileData);

    NBTReader reader(nbtData);
    auto rootTag = reader.readTag();
    if (!rootTag || rootTag->type != TagType::TAG_COMPOUND) return false;

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
    if (width <= 0 || height <= 0 || length <= 0) return false;

    const std::int64_t volume64 = (std::int64_t)width * height * length;
    if (volume64 > std::numeric_limits<int>::max()) return false;
    const int volume = (int)volume64;

    auto blocksIt = schem->children.find("Blocks");
    if (blocksIt == schem->children.end() || blocksIt->second->type != TagType::TAG_COMPOUND)
        return false;
    auto blocksTag = std::dynamic_pointer_cast<TagCompound>(blocksIt->second);

    std::unordered_map<int, int> idRemap;
    auto paletteIt = blocksTag->children.find("Palette");
    if (paletteIt != blocksTag->children.end() && paletteIt->second->type == TagType::TAG_COMPOUND) {
        auto pal = std::dynamic_pointer_cast<TagCompound>(paletteIt->second);
        for (const auto& [name, tag] : pal->children) {
            if (tag->type != TagType::TAG_INT) continue;
            if (name == "minecraft:__reserved__" || name == "__reserved__") continue;

            const int schematicId = std::dynamic_pointer_cast<TagInt>(tag)->value;
            int worldId;
            if (palette.hasBlock(name))
                worldId = palette.getId(name);
            else
                worldId = palette.addBlock(name);

            idRemap[schematicId] = worldId;
        }
    }

    // ---------- offset ----------
    int offsetX = 0, offsetY = 0, offsetZ = 0;
    auto offsetIt = schem->children.find("Offset");
    if (offsetIt != schem->children.end() && offsetIt->second->type == TagType::TAG_INT_ARRAY) {
        auto arr = std::dynamic_pointer_cast<TagIntArray>(offsetIt->second)->value;
        if (arr.size() >= 3) { offsetX = arr[0]; offsetY = arr[1]; offsetZ = arr[2]; }
    }

    std::vector<uint8_t> rawData;
    auto dataIt = blocksTag->children.find("Data");
    if (dataIt != blocksTag->children.end() && dataIt->second->type == TagType::TAG_BYTE_ARRAY)
        rawData = std::dynamic_pointer_cast<TagByteArray>(dataIt->second)->value;
    else {
        auto bdIt = blocksTag->children.find("BlockData");
        if (bdIt != blocksTag->children.end() && bdIt->second->type == TagType::TAG_BYTE_ARRAY)
            rawData = std::dynamic_pointer_cast<TagByteArray>(bdIt->second)->value;
        else return false;
    }

    auto indices = decodeBlockIndices(rawData, volume);

    int airId = 0;
    for (const auto& [id, name] : palette.nameById)
        if (name == "minecraft:air" || name == "air") { airId = id; break; }

    outBlocks.assign(volume, airId);
    for (int y = 0; y < height; ++y)
        for (int z = 0; z < length; ++z)
            for (int x = 0; x < width; ++x) {
                const std::int64_t index = ((std::int64_t)y * length + z) * width + x;
                if (index >= (std::int64_t)indices.size()) continue;

                auto remap = idRemap.find(indices[(std::size_t)index]);
                if (remap == idRemap.end()) continue;
                outBlocks[(std::size_t)index] = remap->second;
            }

    outBounds.min = { offsetX, offsetY, offsetZ };
    outBounds.max = { offsetX + width, offsetY + height, offsetZ + length };  // EXCLUSIVE
    return true;
}

bool Schematic::exportToSchematic(const std::vector<int>& data,
    const Bounds& bound,
    const BlockPalette& palette,
    const std::string& outputDir,
    const std::string& baseName)
{
    fs::path outDir(outputDir);
    fs::create_directories(outDir);

    constexpr int CELL_SIZE = 2000;

    const int worldMinX = bound.min.x, worldMinY = bound.min.y, worldMinZ = bound.min.z;
    const int worldMaxX = bound.max.x, worldMaxY = bound.max.y, worldMaxZ = bound.max.z;

    const int totalW = worldMaxX - worldMinX;
    const int totalH = worldMaxY - worldMinY;
    const int totalL = worldMaxZ - worldMinZ;
    if (totalW <= 0 || totalH <= 0 || totalL <= 0) return false;

    if ((std::size_t)totalW * totalH * totalL != data.size()) {
        std::cerr << "export: data.size() mismatch with bounds\n";
        return false;
    }

    std::unordered_set<int> airIds;
    for (const auto& [id, name] : palette.nameById)
        if (name == "minecraft:air" || name == "air" ||
            name == "minecraft:__reserved__" || name == "__reserved__")
            airIds.insert(id);

    auto makeInt = [](const std::string& n, std::int32_t v) {
        auto t = std::make_shared<TagInt>();
        t->type = TagType::TAG_INT; t->name = n; t->value = v; return t; };
    auto makeShort = [](const std::string& n, std::int16_t v) {
        auto t = std::make_shared<TagShort>();
        t->type = TagType::TAG_SHORT; t->name = n; t->value = v; return t; };

    const int cellsX = (totalW + CELL_SIZE - 1) / CELL_SIZE;
    const int cellsZ = (totalL + CELL_SIZE - 1) / CELL_SIZE;

    for (int ix = 0; ix < cellsX; ++ix) {
        for (int iz = 0; iz < cellsZ; ++iz) {
            const int minX = worldMinX + ix * CELL_SIZE;
            const int maxX = std::min(minX + CELL_SIZE, worldMaxX);
            const int minZ = worldMinZ + iz * CELL_SIZE;
            const int maxZ = std::min(minZ + CELL_SIZE, worldMaxZ);

            const int width = maxX - minX;
            const int height = totalH;
            const int length = maxZ - minZ;
            if (width <= 0 || length <= 0) continue;

            std::vector<std::string> paletteNames{ "minecraft:air" };
            std::unordered_map<int, int> idToLocal;
            idToLocal.reserve(256);

            std::vector<int> localIndices((std::size_t)width * height * length, 0);
            int nonAir = 0;

            for (int y = 0; y < height; ++y) {
                const int wy = worldMinY + y;
                for (int z = 0; z < length; ++z) {
                    const int wz = minZ + z;
                    const std::size_t rowBase =
                        ((std::size_t)(wy - worldMinY) * totalL + (wz - worldMinZ)) * totalW
                        + (minX - worldMinX);

                    for (int x = 0; x < width; ++x) {
                        const int gid = data[rowBase + x];
                        if (airIds.count(gid)) continue;

                        auto it = idToLocal.find(gid);
                        if (it == idToLocal.end()) {
                            std::string name = palette.getName(gid);
                            if (name.empty()) continue;
                            const int local = (int)paletteNames.size();
                            idToLocal[gid] = local;
                            paletteNames.push_back(std::move(name));
                            it = idToLocal.find(gid);
                        }
                        const std::size_t out =
                            ((std::size_t)y * length + z) * width + x;
                        localIndices[out] = it->second;
                        ++nonAir;
                    }
                }
            }

            std::vector<uint8_t> blockData;
            blockData.reserve(localIndices.size());
            for (int32_t v : localIndices) {
                uint32_t u = (uint32_t)v;
                do {
                    uint8_t b = (uint8_t)(u & 0x7F);
                    u >>= 7;
                    if (u != 0) b |= 0x80;
                    blockData.push_back(b);
                } while (u != 0);
            }

            auto root = std::make_shared<TagCompound>();
            root->type = TagType::TAG_COMPOUND;
            root->name = "";

            auto schem = std::make_shared<TagCompound>();
            schem->type = TagType::TAG_COMPOUND;
            schem->name = "Schematic";
            schem->children["Version"] = makeInt("Version", 3);
            schem->children["DataVersion"] = makeInt("DataVersion", 3700);
            schem->children["Width"] = makeShort("Width", (std::int16_t)width);
            schem->children["Height"] = makeShort("Height", (std::int16_t)height);
            schem->children["Length"] = makeShort("Length", (std::int16_t)length);

            auto offset = std::make_shared<TagIntArray>();
            offset->type = TagType::TAG_INT_ARRAY;
            offset->name = "Offset";
            offset->value = { minX, worldMinY, minZ };
            schem->children["Offset"] = offset;

            auto metadata = std::make_shared<TagCompound>();
            metadata->type = TagType::TAG_COMPOUND;
            metadata->name = "Metadata";
            auto author = std::make_shared<TagString>();
            author->type = TagType::TAG_STRING; author->name = "author"; author->value = "MAP";
            auto mname = std::make_shared<TagString>();
            mname->type = TagType::TAG_STRING; mname->name = "name"; mname->value = "MP-schematic";
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
                (std::streamsize)compressed.size());
            std::cout << "Exported " << output << " (" << nonAir << " blocks)\n";
        }
    }
    return true;
}