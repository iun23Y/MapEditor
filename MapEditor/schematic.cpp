#include "schematic.h"

#include <SFML/Graphics/Color.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <zlib.h>

namespace {

enum class TagType : uint8_t {
    TAG_END = 0,
    TAG_BYTE = 1,
    TAG_SHORT = 2,
    TAG_INT = 3,
    TAG_LONG = 4,
    TAG_FLOAT = 5,
    TAG_DOUBLE = 6,
    TAG_BYTE_ARRAY = 7,
    TAG_STRING = 8,
    TAG_LIST = 9,
    TAG_COMPOUND = 10,
    TAG_INT_ARRAY = 11,
    TAG_LONG_ARRAY = 12
};

struct Tag {
    TagType type = TagType::TAG_END;
    std::string name;
    virtual ~Tag() = default;
};

struct TagByte : Tag {
    int8_t value = 0;
};

struct TagShort : Tag {
    int16_t value = 0;
};

struct TagInt : Tag {
    int32_t value = 0;
};

struct TagLong : Tag {
    int64_t value = 0;
};

struct TagString : Tag {
    std::string value;
};

struct TagByteArray : Tag {
    std::vector<uint8_t> value;
};

struct TagIntArray : Tag {
    std::vector<int32_t> value;
};

struct TagLongArray : Tag {
    std::vector<int64_t> value;
};

struct TagList : Tag {
    TagType elementType = TagType::TAG_END;
    std::vector<std::shared_ptr<Tag>> elements;
};

struct TagCompound : Tag {
    std::unordered_map<std::string, std::shared_ptr<Tag>> children;
};

class NBTReader {
    std::vector<uint8_t> data;
    size_t pos = 0;

public:
    explicit NBTReader(const std::vector<uint8_t>& d) : data(d) {}

    uint8_t readByte() {
        return data[pos++];
    }

    int16_t readShort() {
        int16_t value = static_cast<int16_t>((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]);
        pos += 2;
        return value;
    }

    int32_t readInt() {
        int32_t value = (static_cast<int32_t>(data[pos]) << 24) |
            (static_cast<int32_t>(data[pos + 1]) << 16) |
            (static_cast<int32_t>(data[pos + 2]) << 8) |
            data[pos + 3];
        pos += 4;
        return value;
    }

    int64_t readLong() {
        int64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value = (value << 8) | data[pos++];
        }
        return value;
    }

    std::string readString() {
        uint16_t length = static_cast<uint16_t>(readShort());
        std::string value(data.begin() + pos, data.begin() + pos + length);
        pos += length;
        return value;
    }

    std::shared_ptr<Tag> readTag() {
        uint8_t type = readByte();
        if (type == static_cast<uint8_t>(TagType::TAG_END)) return nullptr;
        std::string name = readString();
        return readTagPayload(static_cast<TagType>(type), name);
    }

    std::shared_ptr<Tag> readTagPayload(TagType type, const std::string& name) {
        switch (type) {
        case TagType::TAG_BYTE: {
            auto tag = std::make_shared<TagByte>();
            tag->type = TagType::TAG_BYTE;
            tag->name = name;
            tag->value = static_cast<int8_t>(readByte());
            return tag;
        }
        case TagType::TAG_SHORT: {
            auto tag = std::make_shared<TagShort>();
            tag->type = TagType::TAG_SHORT;
            tag->name = name;
            tag->value = readShort();
            return tag;
        }
        case TagType::TAG_INT: {
            auto tag = std::make_shared<TagInt>();
            tag->type = TagType::TAG_INT;
            tag->name = name;
            tag->value = readInt();
            return tag;
        }
        case TagType::TAG_LONG: {
            auto tag = std::make_shared<TagLong>();
            tag->type = TagType::TAG_LONG;
            tag->name = name;
            tag->value = readLong();
            return tag;
        }
        case TagType::TAG_STRING: {
            auto tag = std::make_shared<TagString>();
            tag->type = TagType::TAG_STRING;
            tag->name = name;
            tag->value = readString();
            return tag;
        }
        case TagType::TAG_BYTE_ARRAY: {
            int32_t len = readInt();
            auto tag = std::make_shared<TagByteArray>();
            tag->type = TagType::TAG_BYTE_ARRAY;
            tag->name = name;
            tag->value.assign(data.begin() + pos, data.begin() + pos + len);
            pos += len;
            return tag;
        }
        case TagType::TAG_INT_ARRAY: {
            int32_t len = readInt();
            auto tag = std::make_shared<TagIntArray>();
            tag->type = TagType::TAG_INT_ARRAY;
            tag->name = name;
            for (int32_t i = 0; i < len; ++i) {
                tag->value.push_back(readInt());
            }
            return tag;
        }
        case TagType::TAG_LONG_ARRAY: {
            int32_t len = readInt();
            auto tag = std::make_shared<TagLongArray>();
            tag->type = TagType::TAG_LONG_ARRAY;
            tag->name = name;
            for (int32_t i = 0; i < len; ++i) {
                tag->value.push_back(readLong());
            }
            return tag;
        }
        case TagType::TAG_LIST: {
            auto tag = std::make_shared<TagList>();
            tag->type = TagType::TAG_LIST;
            tag->name = name;
            tag->elementType = static_cast<TagType>(readByte());
            int32_t count = readInt();
            for (int32_t i = 0; i < count; ++i) {
                tag->elements.push_back(readTagPayload(tag->elementType, ""));
            }
            return tag;
        }
        case TagType::TAG_COMPOUND: {
            auto tag = std::make_shared<TagCompound>();
            tag->type = TagType::TAG_COMPOUND;
            tag->name = name;
            while (true) {
                uint8_t subType = readByte();
                if (subType == static_cast<uint8_t>(TagType::TAG_END)) break;
                std::string subName = readString();
                auto child = readTagPayload(static_cast<TagType>(subType), subName);
                if (child) tag->children[subName] = child;
            }
            return tag;
        }
        default:
            throw std::runtime_error("Unknown tag type");
        }
    }
};

std::unordered_map<std::string, sf::Color> blockColors = {
    {"minecraft:grass_block", sf::Color(87, 160, 52)},
    {"minecraft:dirt", sf::Color(120, 84, 50)},
    {"minecraft:stone", sf::Color(128, 128, 128)},
    {"minecraft:cobblestone", sf::Color(100, 100, 100)},
    {"minecraft:oak_log", sf::Color(140, 105, 60)},
    {"minecraft:oak_leaves", sf::Color(60, 120, 40)},
    {"minecraft:water", sf::Color(30, 90, 200)},
    {"minecraft:lava", sf::Color(255, 80, 0)},
    {"minecraft:sand", sf::Color(210, 190, 140)},
    {"minecraft:sandstone", sf::Color(180, 160, 120)},
    {"minecraft:planks", sf::Color(170, 130, 70)},
    {"minecraft:glass", sf::Color(180, 220, 240)},
    {"minecraft:white_wool", sf::Color(220, 220, 220)},
    {"minecraft:orange_wool", sf::Color(240, 150, 50)},
    {"minecraft:magenta_wool", sf::Color(200, 80, 200)},
    {"minecraft:light_blue_wool", sf::Color(100, 180, 240)},
    {"minecraft:yellow_wool", sf::Color(240, 240, 50)},
    {"minecraft:lime_wool", sf::Color(120, 220, 50)},
    {"minecraft:pink_wool", sf::Color(240, 150, 200)},
    {"minecraft:gray_wool", sf::Color(130, 130, 130)},
    {"minecraft:light_gray_wool", sf::Color(200, 200, 200)},
    {"minecraft:cyan_wool", sf::Color(50, 180, 200)},
    {"minecraft:purple_wool", sf::Color(130, 50, 200)},
    {"minecraft:blue_wool", sf::Color(50, 50, 240)},
    {"minecraft:brown_wool", sf::Color(130, 80, 50)},
    {"minecraft:green_wool", sf::Color(50, 130, 50)},
    {"minecraft:red_wool", sf::Color(240, 50, 50)},
    {"minecraft:black_wool", sf::Color(30, 30, 30)},
    {"minecraft:snow", sf::Color(240, 240, 255)},
    {"minecraft:bedrock", sf::Color(60, 60, 60)},
    {"minecraft:gold_block", sf::Color(250, 210, 50)},
    {"minecraft:iron_block", sf::Color(200, 200, 210)},
    {"minecraft:diamond_block", sf::Color(50, 200, 250)},
    {"minecraft:netherrack", sf::Color(150, 50, 50)},
    {"minecraft:end_stone", sf::Color(200, 200, 150)},
    {"minecraft:emerald_block", sf::Color(69, 255, 109)}
};

} // namespace

SchematicMap::SchematicMap(int w, int h, int l,
    const std::unordered_map<int, std::string>& pal,
    std::vector<std::vector<int>>&& tBlocks,
    std::vector<std::vector<int>>&& tHeights)
    : width(w), height(h), length(l), palette(pal),
      topBlocks(std::move(tBlocks)), topHeights(std::move(tHeights)) {
    for (const auto& [id, name] : palette) {
        nameToId[name] = id;
    }
}

std::vector<uint8_t> gzipDecompress(const std::vector<uint8_t>& compressed) {
    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(compressed.size());
    strm.next_in = const_cast<Bytef*>(compressed.data());
    if (inflateInit2(&strm, 31) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }

    std::vector<uint8_t> output;
    const size_t CHUNK = 8192;
    uint8_t buffer[CHUNK];
    int ret;
    do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            throw std::runtime_error("inflate error: " + std::to_string(ret));
        }
        size_t have = CHUNK - strm.avail_out;
        output.insert(output.end(), buffer, buffer + have);
    } while (ret != Z_STREAM_END);
    inflateEnd(&strm);
    return output;
}

std::pair<int32_t, size_t> readVarint(const std::vector<uint8_t>& data, size_t offset) {
    int32_t result = 0;
    int shift = 0;
    while (offset < data.size()) {
        uint8_t byte = data[offset++];
        result |= (byte & 0x7F) << shift;
        if (!(byte & 0x80)) break;
        shift += 7;
    }
    return {result, offset};
}

std::vector<int32_t> decodeBlockIndices(const std::vector<uint8_t>& rawData, int expectedCount) {
    std::vector<int32_t> indices;
    size_t offset = 0;
    while (offset < rawData.size() && indices.size() < expectedCount) {
        auto [value, newOff] = readVarint(rawData, offset);
        offset = newOff;
        indices.push_back(value);
    }
    if (indices.size() < expectedCount) {
        indices.resize(expectedCount, 0);
    }
    else if (indices.size() > expectedCount) {
        indices.resize(expectedCount);
    }
    return indices;
}

sf::Color getBlockColor(int blockId, const std::unordered_map<int, std::string>& palette) {
    if (blockId < 0) return sf::Color::Black;
    auto it = palette.find(blockId);
    if (it != palette.end()) {
        auto cit = blockColors.find(it->second);
        if (cit != blockColors.end()) return cit->second;
    }
    return sf::Color(200, 200, 200);
}

SchematicMap loadSchematic(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open file: " + filename);
    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::vector<uint8_t> nbtData;
    if (fileData.size() >= 2 && fileData[0] == 0x1F && fileData[1] == 0x8B) {
        nbtData = gzipDecompress(fileData);
    }
    else {
        nbtData = fileData;
    }

    NBTReader reader(nbtData);
    auto rootTag = reader.readTag();
    if (!rootTag || rootTag->type != TagType::TAG_COMPOUND) {
        throw std::runtime_error("Root tag is not a compound");
    }
    auto root = std::dynamic_pointer_cast<TagCompound>(rootTag);

    std::shared_ptr<TagCompound> schemTag = root;
    auto itSchem = root->children.find("Schematic");
    if (itSchem != root->children.end() && itSchem->second->type == TagType::TAG_COMPOUND) {
        schemTag = std::dynamic_pointer_cast<TagCompound>(itSchem->second);
    }

    auto getNumeric = [&](const std::string& name) -> int {
        auto it = schemTag->children.find(name);
        if (it != schemTag->children.end()) {
            if (it->second->type == TagType::TAG_SHORT) return std::dynamic_pointer_cast<TagShort>(it->second)->value;
            if (it->second->type == TagType::TAG_INT) return std::dynamic_pointer_cast<TagInt>(it->second)->value;
        }
        return 0;
    };

    int width = getNumeric("Width");
    int height = getNumeric("Height");
    int length = getNumeric("Length");
    if (width == 0 || height == 0 || length == 0) throw std::runtime_error("Missing dimensions");

    std::shared_ptr<TagCompound> blocksTag;
    auto itBlocks = schemTag->children.find("Blocks");
    if (itBlocks != schemTag->children.end() && itBlocks->second->type == TagType::TAG_COMPOUND) {
        blocksTag = std::dynamic_pointer_cast<TagCompound>(itBlocks->second);
    }
    else {
        auto itRootBlocks = root->children.find("Blocks");
        if (itRootBlocks != root->children.end() && itRootBlocks->second->type == TagType::TAG_COMPOUND) {
            blocksTag = std::dynamic_pointer_cast<TagCompound>(itRootBlocks->second);
        }
        else {
            throw std::runtime_error("Blocks compound not found");
        }
    }

    std::unordered_map<int, std::string> palette;
    auto itPal = blocksTag->children.find("Palette");
    if (itPal != blocksTag->children.end() && itPal->second->type == TagType::TAG_COMPOUND) {
        auto palTag = std::dynamic_pointer_cast<TagCompound>(itPal->second);
        for (auto& [name, child] : palTag->children) {
            if (child->type == TagType::TAG_INT) {
                int id = std::dynamic_pointer_cast<TagInt>(child)->value;
                palette[id] = name;
            }
        }
    }

    std::vector<uint8_t> rawData;
    auto itData = blocksTag->children.find("Data");
    if (itData != blocksTag->children.end() && itData->second->type == TagType::TAG_BYTE_ARRAY) {
        rawData = std::dynamic_pointer_cast<TagByteArray>(itData->second)->value;
    }
    else {
        auto itBlockData = blocksTag->children.find("BlockData");
        if (itBlockData != blocksTag->children.end() && itBlockData->second->type == TagType::TAG_BYTE_ARRAY) {
            rawData = std::dynamic_pointer_cast<TagByteArray>(itBlockData->second)->value;
        }
        else {
            throw std::runtime_error("Data not found");
        }
    }

    int volume = width * height * length;
    auto indices = decodeBlockIndices(rawData, volume);

    std::unordered_set<int> ignoreIds;
    for (const auto& [id, blockName] : palette) {
        if (blockName == "minecraft:air" || blockName == "___reserved___") {
            ignoreIds.insert(id);
        }
    }

    std::vector<std::vector<int>> topBlocks(width, std::vector<int>(length, -1));
    std::vector<std::vector<int>> topHeights(width, std::vector<int>(length, -1));

    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < length; ++z) {
            for (int y = height - 1; y >= 0; --y) {
                int idx = (y * length + z) * width + x;
                int bid = indices[idx];
                if (ignoreIds.find(bid) == ignoreIds.end()) {
                    topBlocks[x][z] = bid;
                    topHeights[x][z] = y;
                    break;
                }
            }
        }
    }

    return SchematicMap(width, height, length, palette, std::move(topBlocks), std::move(topHeights));
}
