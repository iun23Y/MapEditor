#include "schematic.h"
#include "helper.h"

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

namespace fs = std::filesystem;

// =============================================================================// NBT Reader (anonymous namespace)
// =============================================================================namespace {
namespace {
    enum class TagType : std::uint8_t {
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

    struct TagByte : Tag { std::int8_t  value = 0; };
    struct TagShort : Tag { std::int16_t value = 0; };
    struct TagInt : Tag { std::int32_t value = 0; };
    struct TagLong : Tag { std::int64_t value = 0; };
    struct TagString : Tag { std::string  value; };
    struct TagByteArray : Tag { std::vector<std::uint8_t> value; };
    struct TagIntArray : Tag { std::vector<std::int32_t> value; };
    struct TagLongArray : Tag { std::vector<std::int64_t> value; };

    struct TagList : Tag {
        TagType elementType = TagType::TAG_END;
        std::vector<std::shared_ptr<Tag>> elements;
    };

    struct TagCompound : Tag {
        std::unordered_map<std::string, std::shared_ptr<Tag>> children;
    };

    struct RegionPos {
        int32_t x;
        int32_t y;
        int32_t z;
        bool operator==(RegionPos const& o) const noexcept {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct RegionPosHash {
        std::size_t operator()(RegionPos const& p) const noexcept {
            std::size_t h1 = std::hash<int32_t>{}(p.x);
            std::size_t h2 = std::hash<int32_t>{}(p.y);
            std::size_t h3 = std::hash<int32_t>{}(p.z);
            std::size_t seed = h1;
            seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    static std::unordered_map<RegionPos, int32_t, RegionPosHash> loadRegionBlocks(const fs::path& filePath) {
        std::unordered_map<RegionPos, int32_t, RegionPosHash> result;
        if (!fs::exists(filePath)) return result;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file) return result;

        auto fileSize = file.tellg();
        if (fileSize <= 0) return result;
        file.seekg(0, std::ios::beg);

        int32_t lx, ly, lz, bid;
        while (file.read(reinterpret_cast<char*>(&lx), 4) &&
               file.read(reinterpret_cast<char*>(&ly), 4) &&
               file.read(reinterpret_cast<char*>(&lz), 4) &&
               file.read(reinterpret_cast<char*>(&bid), 4)) {
            result[{lx, ly, lz}] = bid;
        }

        return result;
    }

    static void writeRegionBlocks(const fs::path& filePath, const std::unordered_map<RegionPos, int32_t, RegionPosHash>& blocks) {
        if (blocks.empty()) {
            std::error_code ec;
            fs::remove(filePath, ec);
            return;
        }

        std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
        if (!outFile)
            throw std::runtime_error("Cannot open region file for writing: " + filePath.string());

        for (const auto& [pos, bid] : blocks) {
            outFile.write(reinterpret_cast<const char*>(&pos.x), 4);
            outFile.write(reinterpret_cast<const char*>(&pos.y), 4);
            outFile.write(reinterpret_cast<const char*>(&pos.z), 4);
            outFile.write(reinterpret_cast<const char*>(&bid), 4);
        }
    }

    class NBTReader {
        std::vector<std::uint8_t> data;
        std::size_t pos = 0;

    public:
        explicit NBTReader(const std::vector<std::uint8_t>& d) : data(d) {}

        std::uint8_t readByte() { return data[pos++]; }

        std::int16_t readShort() {
            std::int16_t value = static_cast<std::int16_t>(
                (static_cast<std::uint16_t>(data[pos]) << 8) | data[pos + 1]);
            pos += 2;
            return value;
        }

        std::int32_t readInt() {
            std::int32_t value =
                (static_cast<std::int32_t>(data[pos]) << 24) |
                (static_cast<std::int32_t>(data[pos + 1]) << 16) |
                (static_cast<std::int32_t>(data[pos + 2]) << 8) |
                static_cast<std::int32_t>(data[pos + 3]);
            pos += 4;
            return value;
        }

        std::int64_t readLong() {
            std::int64_t value = 0;
            for (int i = 0; i < 8; ++i) {
                value = (value << 8) | data[pos++];
            }
            return value;
        }

        std::string readString() {
            std::uint16_t length = static_cast<std::uint16_t>(readShort());
            std::string value(data.begin() + pos, data.begin() + pos + length);
            pos += length;
            return value;
        }

        std::shared_ptr<Tag> readTag() {
            std::uint8_t type = readByte();
            if (type == static_cast<std::uint8_t>(TagType::TAG_END))
                return nullptr;
            std::string name = readString();
            return readTagPayload(static_cast<TagType>(type), name);
        }

        std::shared_ptr<Tag> readTagPayload(TagType type, const std::string& name) {
            switch (type) {
            case TagType::TAG_BYTE: {
                auto tag = std::make_shared<TagByte>();
                tag->type = TagType::TAG_BYTE; tag->name = name;
                tag->value = static_cast<std::int8_t>(readByte());
                return tag;
            }
            case TagType::TAG_SHORT: {
                auto tag = std::make_shared<TagShort>();
                tag->type = TagType::TAG_SHORT; tag->name = name;
                tag->value = readShort();
                return tag;
            }
            case TagType::TAG_INT: {
                auto tag = std::make_shared<TagInt>();
                tag->type = TagType::TAG_INT; tag->name = name;
                tag->value = readInt();
                return tag;
            }
            case TagType::TAG_LONG: {
                auto tag = std::make_shared<TagLong>();
                tag->type = TagType::TAG_LONG; tag->name = name;
                tag->value = readLong();
                return tag;
            }
            case TagType::TAG_STRING: {
                auto tag = std::make_shared<TagString>();
                tag->type = TagType::TAG_STRING; tag->name = name;
                tag->value = readString();
                return tag;
            }
            case TagType::TAG_BYTE_ARRAY: {
                std::int32_t len = readInt();
                auto tag = std::make_shared<TagByteArray>();
                tag->type = TagType::TAG_BYTE_ARRAY; tag->name = name;
                tag->value.assign(data.begin() + pos, data.begin() + pos + len);
                pos += len;
                return tag;
            }
            case TagType::TAG_INT_ARRAY: {
                std::int32_t len = readInt();
                auto tag = std::make_shared<TagIntArray>();
                tag->type = TagType::TAG_INT_ARRAY; tag->name = name;
                tag->value.reserve(len);
                for (std::int32_t i = 0; i < len; ++i)
                    tag->value.push_back(readInt());
                return tag;
            }
            case TagType::TAG_LONG_ARRAY: {
                std::int32_t len = readInt();
                auto tag = std::make_shared<TagLongArray>();
                tag->type = TagType::TAG_LONG_ARRAY; tag->name = name;
                tag->value.reserve(len);
                for (std::int32_t i = 0; i < len; ++i)
                    tag->value.push_back(readLong());
                return tag;
            }
            case TagType::TAG_LIST: {
                auto tag = std::make_shared<TagList>();
                tag->type = TagType::TAG_LIST; tag->name = name;
                tag->elementType = static_cast<TagType>(readByte());
                std::int32_t count = readInt();
                tag->elements.reserve(count);
                for (std::int32_t i = 0; i < count; ++i)
                    tag->elements.push_back(readTagPayload(tag->elementType, ""));
                return tag;
            }
            case TagType::TAG_COMPOUND: {
                auto tag = std::make_shared<TagCompound>();
                tag->type = TagType::TAG_COMPOUND; tag->name = name;
                while (true) {
                    std::uint8_t subType = readByte();
                    if (subType == static_cast<std::uint8_t>(TagType::TAG_END))
                        break;
                    std::string subName = readString();
                    auto child = readTagPayload(static_cast<TagType>(subType), subName);
                    if (child) tag->children[subName] = child;
                }
                return tag;
            }
            default:
                throw std::runtime_error("Unknown NBT tag type");
            }
        }
    };

    const std::unordered_map<std::string, sf::Color> blockColors = {
        {"minecraft:grass_block",     sf::Color(87, 160,  52)},
        {"minecraft:dirt",            sf::Color(120,  84,  50)},
        {"minecraft:stone",           sf::Color(128, 128, 128)},
        {"minecraft:cobblestone",     sf::Color(100, 100, 100)},
        {"minecraft:oak_log",         sf::Color(140, 105,  60)},
        {"minecraft:oak_leaves",      sf::Color(60, 120,  40)},
        {"minecraft:water",           sf::Color(30,  90, 200)},
        {"minecraft:lava",            sf::Color(255,  80,   0)},
        {"minecraft:sand",            sf::Color(210, 190, 140)},
        {"minecraft:sandstone",       sf::Color(180, 160, 120)},
        {"minecraft:planks",          sf::Color(170, 130,  70)},
        {"minecraft:glass",           sf::Color(180, 220, 240)},
        {"minecraft:white_wool",      sf::Color(220, 220, 220)},
        {"minecraft:orange_wool",     sf::Color(240, 150,  50)},
        {"minecraft:magenta_wool",    sf::Color(200,  80, 200)},
        {"minecraft:light_blue_wool", sf::Color(100, 180, 240)},
        {"minecraft:yellow_wool",     sf::Color(240, 240,  50)},
        {"minecraft:lime_wool",       sf::Color(120, 220,  50)},
        {"minecraft:pink_wool",       sf::Color(240, 150, 200)},
        {"minecraft:gray_wool",       sf::Color(130, 130, 130)},
        {"minecraft:light_gray_wool", sf::Color(200, 200, 200)},
        {"minecraft:cyan_wool",       sf::Color(50, 180, 200)},
        {"minecraft:purple_wool",     sf::Color(130,  50, 200)},
        {"minecraft:blue_wool",       sf::Color(50,  50, 240)},
        {"minecraft:brown_wool",      sf::Color(130,  80,  50)},
        {"minecraft:green_wool",      sf::Color(50, 130,  50)},
        {"minecraft:red_wool",        sf::Color(240,  50,  50)},
        {"minecraft:black_wool",      sf::Color(30,  30,  30)},
        {"minecraft:snow",            sf::Color(240, 240, 255)},
        {"minecraft:bedrock",         sf::Color(60,  60,  60)},
        {"minecraft:gold_block",      sf::Color(250, 210,  50)},
        {"minecraft:iron_block",      sf::Color(200, 200, 210)},
        {"minecraft:diamond_block",   sf::Color(50, 200, 250)},
        {"minecraft:netherrack",      sf::Color(150,  50,  50)},
        {"minecraft:end_stone",       sf::Color(200, 200, 150)},
        {"minecraft:emerald_block",   sf::Color(69, 255, 109)}
    };
    


    void writeByte(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
    void writeShort(std::vector<uint8_t>& out, int16_t v) {
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    void writeInt(std::vector<uint8_t>& out, int32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    void writeLong(std::vector<uint8_t>& out, int64_t v) {
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    void writeString(std::vector<uint8_t>& out, const std::string& s) {
        writeShort(out, static_cast<int16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    void writeTagType(std::vector<uint8_t>& out, TagType type) {
        out.push_back(static_cast<uint8_t>(type));
    }

    // Запись payload (без имени) — используется для списков
    void writeTagPayload(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out) {
        if (!tag) return;
        switch (tag->type) {
        case TagType::TAG_BYTE: {
            auto t = std::dynamic_pointer_cast<TagByte>(tag);
            writeByte(out, static_cast<uint8_t>(t->value));
            break;
        }
        case TagType::TAG_SHORT: {
            auto t = std::dynamic_pointer_cast<TagShort>(tag);
            writeShort(out, t->value);
            break;
        }
        case TagType::TAG_INT: {
            auto t = std::dynamic_pointer_cast<TagInt>(tag);
            writeInt(out, t->value);
            break;
        }
        case TagType::TAG_LONG: {
            auto t = std::dynamic_pointer_cast<TagLong>(tag);
            writeLong(out, t->value);
            break;
        }
        case TagType::TAG_STRING: {
            auto t = std::dynamic_pointer_cast<TagString>(tag);
            writeString(out, t->value);
            break;
        }
        case TagType::TAG_BYTE_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagByteArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            out.insert(out.end(), t->value.begin(), t->value.end());
            break;
        }
        case TagType::TAG_INT_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagIntArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            for (int32_t v : t->value) writeInt(out, v);
            break;
        }
        case TagType::TAG_LONG_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagLongArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            for (int64_t v : t->value) writeLong(out, v);
            break;
        }
        case TagType::TAG_LIST: {
            auto t = std::dynamic_pointer_cast<TagList>(tag);
            writeTagType(out, t->elementType);
            writeInt(out, static_cast<int32_t>(t->elements.size()));
            for (auto& child : t->elements)
                writeTagPayload(child, out);
            break;
        }
        case TagType::TAG_COMPOUND: {
            auto t = std::dynamic_pointer_cast<TagCompound>(tag);
            // Compound в списке не используется в Sponge Schematic, но на всякий случай запишем все дочерние теги
            for (auto& [name, child] : t->children) {
                writeTagType(out, child->type);
                writeString(out, name);
                // рекурсивно пишем payload без имени? Но у нас есть writeTag, который пишет полный тег с именем.
                // Для компаунда внутри списка нужно писать без имени, но имена уже есть в children.
                // Проще использовать writeTag, который пишет тип+имя+payload.
                // Но тогда это нарушит структуру, так как список ожидает последовательность тегов без имён.
                // Такого в реальности не бывает, поэтому кинем исключение.
                throw std::runtime_error("Nested compound inside list is not supported");
            }
            break;
        }
        default:
            throw std::runtime_error("Unsupported tag type for payload");
        }
    }

    // Запись полного тега (тип + имя + payload)
    void writeTag(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out) {
        if (!tag) return;
        writeTagType(out, tag->type);
        writeString(out, tag->name);

        switch (tag->type) {
        case TagType::TAG_BYTE: {
            auto t = std::dynamic_pointer_cast<TagByte>(tag);
            writeByte(out, static_cast<uint8_t>(t->value));
            break;
        }
        case TagType::TAG_SHORT: {
            auto t = std::dynamic_pointer_cast<TagShort>(tag);
            writeShort(out, t->value);
            break;
        }
        case TagType::TAG_INT: {
            auto t = std::dynamic_pointer_cast<TagInt>(tag);
            writeInt(out, t->value);
            break;
        }
        case TagType::TAG_LONG: {
            auto t = std::dynamic_pointer_cast<TagLong>(tag);
            writeLong(out, t->value);
            break;
        }
        case TagType::TAG_STRING: {
            auto t = std::dynamic_pointer_cast<TagString>(tag);
            writeString(out, t->value);
            break;
        }
        case TagType::TAG_BYTE_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagByteArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            out.insert(out.end(), t->value.begin(), t->value.end());
            break;
        }
        case TagType::TAG_INT_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagIntArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            for (int32_t v : t->value) writeInt(out, v);
            break;
        }
        case TagType::TAG_LONG_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagLongArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            for (int64_t v : t->value) writeLong(out, v);
            break;
        }
        case TagType::TAG_LIST: {
            auto t = std::dynamic_pointer_cast<TagList>(tag);
            writeTagType(out, t->elementType);
            writeInt(out, static_cast<int32_t>(t->elements.size()));
            for (auto& child : t->elements)
                writeTagPayload(child, out);
            break;
        }
        case TagType::TAG_COMPOUND: {
            auto t = std::dynamic_pointer_cast<TagCompound>(tag);
            for (auto& [name, child] : t->children) {
                writeTag(child, out); // рекурсивно записываем каждый дочерний тег (с именем)
            }
            writeTagType(out, TagType::TAG_END); // завершающий тег
            break;
        }
        default:
            throw std::runtime_error("Unsupported tag type for serialization");
        }
    }

    // Сжатие gzip (для экспорта)
    std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& data) {
        z_stream strm{};
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            throw std::runtime_error("deflateInit2 failed");

        strm.avail_in = static_cast<uInt>(data.size());
        strm.next_in = const_cast<Bytef*>(data.data());

        std::vector<uint8_t> output;
        constexpr std::size_t CHUNK = 8192;
        uint8_t buffer[CHUNK];
        int ret = Z_OK;

        do {
            strm.avail_out = CHUNK;
            strm.next_out = buffer;
            ret = deflate(&strm, Z_FINISH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                deflateEnd(&strm);
                throw std::runtime_error("deflate error");
            }
            std::size_t have = CHUNK - strm.avail_out;
            output.insert(output.end(), buffer, buffer + have);
        } while (ret != Z_STREAM_END);

        deflateEnd(&strm);
        return output;
    }
} // anonymous namespace

// =============================================================================// BlockPalette
void BlockPalette::addBlock(int id, const std::string& name) {
    if (idByName.find(name) != idByName.end() || nameById.find(id) != nameById.end())
        return;
    idByName[name] = id;
    nameById[id] = name;
}

bool BlockPalette::hasBlock(int id) const {
    return nameById.find(id) != nameById.end();
}

bool BlockPalette::hasBlock(const std::string& name) const {
    return idByName.find(name) != idByName.end();
}

int BlockPalette::getId(const std::string& name) const {
    auto it = idByName.find(name);
    return (it != idByName.end()) ? it->second : -1;
}

std::string BlockPalette::getName(int id) const {
    auto it = nameById.find(id);
    return (it != nameById.end()) ? it->second : "";
}

// =============================================================================// Free helpers
std::vector<std::uint8_t> gzipDecompress(const std::vector<std::uint8_t>& compressed) {
    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(compressed.size());
    strm.next_in = const_cast<Bytef*>(compressed.data());

    if (inflateInit2(&strm, 31) != Z_OK)
        throw std::runtime_error("inflateInit2 failed");

    std::vector<std::uint8_t> output;
    constexpr std::size_t CHUNK = 8192;
    std::uint8_t buffer[CHUNK];
    int ret = Z_OK;

    do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            throw std::runtime_error("inflate error: " + std::to_string(ret));
        }
        std::size_t have = CHUNK - strm.avail_out;
        output.insert(output.end(), buffer, buffer + have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return output;
}

std::pair<std::int32_t, std::size_t> readVarint(const std::vector<std::uint8_t>& data, std::size_t offset) {
    std::int32_t result = 0;
    int shift = 0;
    while (offset < data.size()) {
        std::uint8_t byte = data[offset++];
        result |= static_cast<std::int32_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            break;
        shift += 7;
    }
    return { result, offset };
}

std::vector<std::int32_t> decodeBlockIndices(const std::vector<std::uint8_t>& rawData, int expectedCount) {
    std::vector<std::int32_t> indices;
    indices.reserve(expectedCount);
    std::size_t offset = 0;

    while (offset < rawData.size() && static_cast<int>(indices.size()) < expectedCount) {
        auto [value, newOff] = readVarint(rawData, offset);
        offset = newOff;
        indices.push_back(value);
    }

    if (indices.size() < static_cast<std::size_t>(expectedCount))
        indices.resize(expectedCount, 0);
    else if (indices.size() > static_cast<std::size_t>(expectedCount))
        indices.resize(expectedCount);

    return indices;
}

static constexpr int REGION_SIZE = 1000;

std::vector<SchematicMap::RegionBlock> SchematicMap::getBlocksInArea(
    int minX, int minY, int minZ, int maxX, int maxY, int maxZ) const {
    std::vector<RegionBlock> result;

    // Which regions intersect the area
    int startRegionX = (minX >= 0 ? minX / REGION_SIZE : (minX - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int endRegionX = (maxX >= 0 ? maxX / REGION_SIZE : (maxX - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int startRegionY = minY;
    int endRegionY = maxY;
    int startRegionZ = (minZ >= 0 ? minZ / REGION_SIZE : (minZ - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int endRegionZ = (maxZ >= 0 ? maxZ / REGION_SIZE : (maxZ - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;

    for (int ry = startRegionY; ry <= endRegionY; ++ry) {
        for (int rx = startRegionX; rx <= endRegionX; rx += REGION_SIZE) {
            for (int rz = startRegionZ; rz <= endRegionZ; rz += REGION_SIZE) {
                auto regionBlocks = getRegionBlocks(rx, ry, rz);
                for (auto& b : regionBlocks) {
                    if (b.x >= minX && b.x <= maxX &&
                        b.y >= minY && b.y <= maxY &&
                        b.z >= minZ && b.z <= maxZ) {
                        result.push_back(b);
                    }
                }
            }
        }
    }

    return result;
}
std::vector<SchematicMap::RegionBlock> SchematicMap::getTopBlocksInArea(
    int minX, int minZ, int maxX, int maxZ) const {
    std::unordered_map<std::pair<int, int>, RegionBlock, PairHash> topMap;

    int minY = Pos1.y;
    int maxY = Pos2.y - 1;

    int startRX = (minX >= 0 ? minX / REGION_SIZE : (minX - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int endRX = (maxX >= 0 ? maxX / REGION_SIZE : (maxX - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int startRZ = (minZ >= 0 ? minZ / REGION_SIZE : (minZ - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int endRZ = (maxZ >= 0 ? maxZ / REGION_SIZE : (maxZ - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;

    for (int ry = minY; ry <= maxY; ++ry) {
        for (int rx = startRX; rx <= endRX; rx += REGION_SIZE) {
            for (int rz = startRZ; rz <= endRZ; rz += REGION_SIZE) {
                auto blocks = getRegionBlocks(rx, ry, rz);
                for (const auto& b : blocks) {
                    if (b.x >= minX && b.x <= maxX && b.z >= minZ && b.z <= maxZ) {
                        auto key = std::make_pair(b.x, b.z);
                        auto it = topMap.find(key);
                        if (it == topMap.end() || b.y > it->second.y) {
                            topMap[key] = b;
                        }
                    }
                }
            }
        }
    }

    // Преобразуем карту в вектор
    std::vector<RegionBlock> result;
    result.reserve(topMap.size());
    for (const auto& pair : topMap) {
        result.push_back(pair.second);
    }
    return result;
}


std::string SchematicMap::getRegionName(int regionX, int regionY, int regionZ) {
    return std::to_string(regionX) + "_" + std::to_string(regionY) + "_" + std::to_string(regionZ);
}

fs::path SchematicMap::getRegionPath(const fs::path& regionsDir, const std::string& regionName) {
    return regionsDir / (regionName + ".region");
}

SchematicMap::SchematicMap(const std::string& filename, const std::string& worldDir) {
    worldPath = fs::path(worldDir);
    loadFromFile(filename);
}

void SchematicMap::loadFromFile(const std::string& filename) {
    // 1. Read file
    std::ifstream file(filename, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open file: " + filename);

    std::vector<std::uint8_t> fileData(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    // 2. Decompress gzip
    std::vector<std::uint8_t> nbtData;
    if (fileData.size() >= 2 && fileData[0] == 0x1F && fileData[1] == 0x8B) {
        nbtData = gzipDecompress(fileData);
    } else {
        nbtData = std::move(fileData);
    }

    // 3. Parse NBT
    NBTReader reader(nbtData);
    auto rootTag = reader.readTag();
    if (!rootTag || rootTag->type != TagType::TAG_COMPOUND)
        throw std::runtime_error("Root tag is not a compound");

    auto root = std::dynamic_pointer_cast<TagCompound>(rootTag);

    std::shared_ptr<TagCompound> schemTag = root;
    auto itSchem = root->children.find("Schematic");
    if (itSchem != root->children.end() && itSchem->second->type == TagType::TAG_COMPOUND)
        schemTag = std::dynamic_pointer_cast<TagCompound>(itSchem->second);

    auto getNumeric = [&](const std::string& name) -> int {
        auto it = schemTag->children.find(name);
        if (it != schemTag->children.end()) {
            if (it->second->type == TagType::TAG_SHORT)
                return std::dynamic_pointer_cast<TagShort>(it->second)->value;
            if (it->second->type == TagType::TAG_INT)
                return std::dynamic_pointer_cast<TagInt>(it->second)->value;
        }
        return 0;
    };

    int width = getNumeric("Width");
    int height = getNumeric("Height");
    int length = getNumeric("Length");

    if (width == 0 || height == 0 || length == 0)
        throw std::runtime_error("Missing schematic dimensions");

    // 4. Find blocks
    std::shared_ptr<TagCompound> blocksTag;
    auto itBlocks = schemTag->children.find("Blocks");
    if (itBlocks != schemTag->children.end() && itBlocks->second->type == TagType::TAG_COMPOUND) {
        blocksTag = std::dynamic_pointer_cast<TagCompound>(itBlocks->second);
    } else {
        auto itRootBlocks = root->children.find("Blocks");
        if (itRootBlocks != root->children.end() && itRootBlocks->second->type == TagType::TAG_COMPOUND)
            blocksTag = std::dynamic_pointer_cast<TagCompound>(itRootBlocks->second);
        else
            throw std::runtime_error("Blocks compound not found");
    }

    // 5. Palette
    std::unordered_map<int, int> idRemap;

    auto itPal = blocksTag->children.find("Palette");
    if (itPal != blocksTag->children.end() && itPal->second->type == TagType::TAG_COMPOUND) {
        auto palTag = std::dynamic_pointer_cast<TagCompound>(itPal->second);
        for (auto& [name, child] : palTag->children) {
            if (child->type == TagType::TAG_INT) {
                int schematicId = std::dynamic_pointer_cast<TagInt>(child)->value;

                int worldId;
                if (palette.hasBlock(name)) {
                    worldId = palette.getId(name);
                }
                else {
                    worldId = static_cast<int>(palette.nameById.size());
                    palette.addBlock(worldId, name);
                }
                idRemap[schematicId] = worldId;
            }
        }
    }

    // 6. Block data
    std::vector<std::uint8_t> rawData;
    auto itData = blocksTag->children.find("Data");
    if (itData != blocksTag->children.end() && itData->second->type == TagType::TAG_BYTE_ARRAY) {
        rawData = std::dynamic_pointer_cast<TagByteArray>(itData->second)->value;
    } else {
        auto itBlockData = blocksTag->children.find("BlockData");
        if (itBlockData != blocksTag->children.end() && itBlockData->second->type == TagType::TAG_BYTE_ARRAY)
            rawData = std::dynamic_pointer_cast<TagByteArray>(itBlockData->second)->value;
        else
            throw std::runtime_error("Block data not found");
    }

    // 7. Offset
    int offsetX = 0, offsetY = 0, offsetZ = 0;
    auto itOffset = schemTag->children.find("Offset");
    if (itOffset != schemTag->children.end() && itOffset->second->type == TagType::TAG_INT_ARRAY) {
        auto off = std::dynamic_pointer_cast<TagIntArray>(itOffset->second)->value;
        if (off.size() >= 3) {
            offsetX = off[0]; offsetY = off[1]; offsetZ = off[2];
        }
    }

    Pos1 = { offsetX, offsetY, offsetZ };
    Pos2 = { width + offsetX, height + offsetY, length + offsetZ };

    // 8. Decode indices
    int volume = width * height * length;
    auto indices = decodeBlockIndices(rawData, volume);

    // 9. Which IDs to ignore (air, etc.)
    std::unordered_set<int> ignoreIds;
    for (const auto& [id, blockName] : palette.nameById) {
        if (blockName == "minecraft:air" || blockName == "___reserved___" || blockName == "void")
            ignoreIds.insert(id);
    }

    // 9. Update world bounds
    if (!hasBounds) {
        Pos1 = { offsetX, offsetY, offsetZ };
        Pos2 = { offsetX + width, offsetY + height, offsetZ + length };
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

    // 10. === IMPORTANT: group blocks by region, then update files ===

    fs::path regionsDir = worldPath / "regions";
    fs::create_directories(regionsDir);

    // regionName -> list of new blocks from this schematic
    std::unordered_map<std::string, std::vector<std::tuple<int32_t, int32_t, int32_t, int32_t>>> newRegionBlocks;

    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < length; ++z) {
            for (int y = 0; y < height; ++y) {
                int idx = (y * length + z) * width + x;
                int bid = indices[idx];

                if (ignoreIds.find(bid) != ignoreIds.end())
                    continue;

                auto remapIt = idRemap.find(bid);
                if (remapIt == idRemap.end()) continue;
                int worldBid = remapIt->second;

                int absX = x + offsetX;
                int absY = y + offsetY;
                int absZ = z + offsetZ;

                int regionX = (absX >= 0 ? absX / 1000 : (absX - 999) / 1000) * 1000;
                int regionZ = (absZ >= 0 ? absZ / 1000 : (absZ - 999) / 1000) * 1000;
                int regionY = absY;

                int localX = absX - regionX;
                int localZ = absZ - regionZ;
                int localY = absY - regionY;

                std::string regionName = getRegionName(regionX, regionY, regionZ);
                newRegionBlocks[regionName].push_back({ localX, localY, localZ, worldBid });
            }
        }
    }

    // 11. For each region: read existing, update, rewrite
    for (auto& [regionName, newBlocks] : newRegionBlocks) {
        fs::path filePath = getRegionPath(regionsDir, regionName);

        // Existing blocks: key "x,y,z" -> blockId
        std::unordered_map<std::string, int32_t> existing;

        // Read existing file if present
        if (fs::exists(filePath)) {
            std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
            if (inFile) {
                auto fileSize = inFile.tellg();
                inFile.seekg(0, std::ios::beg);

                uint32_t count = static_cast<uint32_t>(fileSize / 16);
                for (uint32_t i = 0; i < count; ++i) {
                    int32_t lx, ly, lz, bid;
                    inFile.read(reinterpret_cast<char*>(&lx), 4);
                    inFile.read(reinterpret_cast<char*>(&ly), 4);
                    inFile.read(reinterpret_cast<char*>(&lz), 4);
                    inFile.read(reinterpret_cast<char*>(&bid), 4);

                    std::string key = std::to_string(lx) + "," + std::to_string(ly) + "," + std::to_string(lz);
                    existing[key] = bid;
                }
            }
        }

        // Update existing blocks with new ones (overwrite)
        for (auto& [lx, ly, lz, bid] : newBlocks) {
            std::string key = std::to_string(lx) + "," + std::to_string(ly) + "," + std::to_string(lz);
            existing[key] = bid;
        }

        // Rewrite the file
        std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
        for (auto& [key, bid] : existing) {
            // Parse key back
            size_t p1 = key.find(',');
            size_t p2 = key.find(',', p1 + 1);
            int32_t lx = std::stoi(key.substr(0, p1));
            int32_t ly = std::stoi(key.substr(p1 + 1, p2 - p1 - 1));
            int32_t lz = std::stoi(key.substr(p2 + 1));

            int32_t data[4] = { lx, ly, lz, bid };
            outFile.write(reinterpret_cast<const char*>(data), sizeof(data));
        }
    }
}

void SchematicMap::exportToSchematic(const std::string& baseName, const std::string& outputDir) const {
    fs::path outPath = fs::path(outputDir);
    fs::create_directories(outPath);

    if (!hasBounds) {
        std::cerr << "World has no blocks to export.\n";
        return;
    }

    // Константа размера ячейки (2000)
    const int CELL_SIZE = 2000;

    // Границы мира
    int worldMinX = Pos1.x;
    int worldMaxX = Pos2.x - 1;
    int worldMinY = Pos1.y;
    int worldMaxY = Pos2.y - 1;
    int worldMinZ = Pos1.z;
    int worldMaxZ = Pos2.z - 1;

    // Размеры схемы в блоках
    int sizeX = worldMaxX - worldMinX + 1;
    int sizeZ = worldMaxZ - worldMinZ + 1;

    // Количество ячеек по X и Z (округление вверх)
    int cellsX = (sizeX + CELL_SIZE - 1) / CELL_SIZE;
    int cellsZ = (sizeZ + CELL_SIZE - 1) / CELL_SIZE;

    // Проходим по локальной сетке (индексы ячеек 0,1,2...)
    for (int ix = 0; ix < cellsX; ++ix) {
        for (int iz = 0; iz < cellsZ; ++iz) {
            // Глобальные координаты текущей ячейки
            int minX = worldMinX + ix * CELL_SIZE;
            int maxX = std::min(minX + CELL_SIZE - 1, worldMaxX);
            int minZ = worldMinZ + iz * CELL_SIZE;
            int maxZ = std::min(minZ + CELL_SIZE - 1, worldMaxZ);

            // Получаем все блоки в этой ячейке (по полной высоте)
            auto blocks = getBlocksInArea(minX, worldMinY, minZ, maxX, worldMaxY, maxZ);
            if (blocks.empty()) continue;

            // ---------- Палитра ----------
            std::unordered_map<int32_t, int32_t> globalToLocal;
            std::vector<std::string> paletteNames = { "minecraft:air" };
            for (const auto& b : blocks) {
                if (globalToLocal.find(b.blockId) == globalToLocal.end()) {
                    std::string name = palette.getName(b.blockId);
                    if (name.empty()) name = "minecraft:air";
                    globalToLocal[b.blockId] = static_cast<int32_t>(paletteNames.size());
                    paletteNames.push_back(name);
                }
            }

            // Размеры ячейки (локальные)
            int width = maxX - minX + 1;
            int height = worldMaxY - worldMinY + 1;
            int length = maxZ - minZ + 1;
            int volume = width * height * length;

            // Индексы блоков (локальные координаты внутри ячейки)
            std::vector<int32_t> indices(volume, 0);
            for (const auto& b : blocks) {
                int localX = b.x - minX;
                int localY = b.y - worldMinY;
                int localZ = b.z - minZ;
                int idx = (localY * length + localZ) * width + localX;
                auto it = globalToLocal.find(b.blockId);
                if (it != globalToLocal.end())
                    indices[idx] = it->second;
            }

            // VarInt-кодирование (Sponge v3)
            std::vector<uint8_t> blockData;
            for (int32_t val : indices) {
                uint32_t v = static_cast<uint32_t>(val);
                do {
                    uint8_t byte = v & 0x7F;
                    v >>= 7;
                    if (v != 0) byte |= 0x80;
                    blockData.push_back(byte);
                } while (v != 0);
            }

            // Плоский массив для совместимости (Classic)
            std::vector<uint8_t> flatData;
            flatData.reserve(indices.size());
            for (int32_t idx : indices) {
                flatData.push_back(static_cast<uint8_t>(std::min(idx, 255)));
            }

            // ---------- Сборка NBT ----------
            auto root = std::make_shared<TagCompound>();
            root->type = TagType::TAG_COMPOUND;
            root->name = "";

            auto schemCompound = std::make_shared<TagCompound>();
            schemCompound->type = TagType::TAG_COMPOUND;
            schemCompound->name = "Schematic";

            // Version
            auto versionTag = std::make_shared<TagInt>();
            versionTag->type = TagType::TAG_INT;
            versionTag->name = "Version";
            versionTag->value = 3;
            schemCompound->children["Version"] = versionTag;

            // DataVersion
            auto dataVersionTag = std::make_shared<TagInt>();
            dataVersionTag->type = TagType::TAG_INT;
            dataVersionTag->name = "DataVersion";
            dataVersionTag->value = 3700;
            schemCompound->children["DataVersion"] = dataVersionTag;

            // Width, Height, Length
            auto widthTag = std::make_shared<TagShort>();
            widthTag->type = TagType::TAG_SHORT;
            widthTag->name = "Width";
            widthTag->value = static_cast<int16_t>(width);
            schemCompound->children["Width"] = widthTag;

            auto heightTag = std::make_shared<TagShort>();
            heightTag->type = TagType::TAG_SHORT;
            heightTag->name = "Height";
            heightTag->value = static_cast<int16_t>(height);
            schemCompound->children["Height"] = heightTag;

            auto lengthTag = std::make_shared<TagShort>();
            lengthTag->type = TagType::TAG_SHORT;
            lengthTag->name = "Length";
            lengthTag->value = static_cast<int16_t>(length);
            schemCompound->children["Length"] = lengthTag;

            // Offset – глобальные координаты ячейки
            auto offsetArray = std::make_shared<TagIntArray>();
            offsetArray->type = TagType::TAG_INT_ARRAY;
            offsetArray->name = "Offset";
            offsetArray->value = { minX, worldMinY, minZ };
            schemCompound->children["Offset"] = offsetArray;

            // Метаданные
            auto metadata = std::make_shared<TagCompound>();
            metadata->type = TagType::TAG_COMPOUND;
            metadata->name = "Metadata";

            auto authorTag = std::make_shared<TagString>();
            authorTag->type = TagType::TAG_STRING;
            authorTag->name = "author";
            authorTag->value = "MapEditor";

            auto nameTag = std::make_shared<TagString>();
            nameTag->type = TagType::TAG_STRING;
            nameTag->name = "name";
            nameTag->value = "ME-BTE-schematic";

            metadata->children["author"] = authorTag;
            metadata->children["name"] = nameTag;
            schemCompound->children["Metadata"] = metadata;

            // Blocks
            auto blocksCompound = std::make_shared<TagCompound>();
            blocksCompound->type = TagType::TAG_COMPOUND;
            blocksCompound->name = "Blocks";

            // Palette
            auto paletteCompound = std::make_shared<TagCompound>();
            paletteCompound->type = TagType::TAG_COMPOUND;
            paletteCompound->name = "Palette";
            for (size_t i = 0; i < paletteNames.size(); ++i) {
                auto idTag = std::make_shared<TagInt>();
                idTag->type = TagType::TAG_INT;
                idTag->name = paletteNames[i];
                idTag->value = static_cast<int32_t>(i);
                paletteCompound->children[paletteNames[i]] = idTag;
            }
            blocksCompound->children["Palette"] = paletteCompound;

            // Data (Classic)
            auto dataTag = std::make_shared<TagByteArray>();
            dataTag->type = TagType::TAG_BYTE_ARRAY;
            dataTag->name = "Data";
            dataTag->value = std::move(flatData);
            blocksCompound->children["Data"] = dataTag;

            schemCompound->children["Blocks"] = blocksCompound;
            root->children["Schematic"] = schemCompound;

            // Сериализация
            std::vector<uint8_t> nbtData;
            writeTag(root, nbtData);
            auto compressed = gzipCompress(nbtData);

            // Имя файла: baseName-ixxiz.schem
            std::string filename = baseName + "-" + std::to_string(ix) + "x" + std::to_string(iz) + ".schem";
            fs::path filepath = outPath / filename;
            std::ofstream outFile(filepath, std::ios::binary);
            if (!outFile) {
                std::cerr << "Failed to create " << filepath << "\n";
                continue;
            }
            outFile.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            std::cout << "Exported " << filepath << " (" << blocks.size() << " blocks)\n";
        }
    }
}

int SchematicMap::getBlock(int x, int y, int z) const {
    int regionX = (x >= 0 ? x / REGION_SIZE : (x - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int regionY = y;
    int regionZ = (z >= 0 ? z / REGION_SIZE : (z - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;

    std::string regionName = getRegionName(regionX, regionY, regionZ);
    fs::path filePath = getRegionPath(worldPath / "regions", regionName);

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) return -1;

    auto fileSize = file.tellg();
    if (fileSize <= 0) return -1;
    file.seekg(0, std::ios::beg);

    int32_t localX = x - regionX;
    int32_t localY = y - regionY;
    int32_t localZ = z - regionZ;

    // Linear search in file (16 bytes per record)
    // For optimization, an index can be added later
    int32_t bx, by, bz, bid;
    while (file.read(reinterpret_cast<char*>(&bx), 4) &&
           file.read(reinterpret_cast<char*>(&by), 4) &&
           file.read(reinterpret_cast<char*>(&bz), 4) &&
           file.read(reinterpret_cast<char*>(&bid), 4)) {
        if (bx == localX && by == localY && bz == localZ)
            return bid;
    }

    return -1; // not found = air
}

void SchematicMap::setBlock(int x, int y, int z, int blockId) {
    int regionX = (x >= 0 ? x / REGION_SIZE : (x - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
    int regionY = y;
    int regionZ = (z >= 0 ? z / REGION_SIZE : (z - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;

    std::string regionName = getRegionName(regionX, regionY, regionZ);
    fs::path filePath = getRegionPath(worldPath / "regions", regionName);

    auto existing = loadRegionBlocks(filePath);

    int32_t localX = x - regionX;
    int32_t localY = y - regionY;
    int32_t localZ = z - regionZ;
    RegionPos key{ localX, localY, localZ };

    if (blockId < 0) {
        existing.erase(key);
    } else {
        existing[key] = blockId;
        if (!hasBounds) {
            Pos1 = { x, y, z };
            Pos2 = { x + 1, y + 1, z + 1 };
            hasBounds = true;
        } else {
            Pos1.x = std::min(Pos1.x, x);
            Pos1.y = std::min(Pos1.y, y);
            Pos1.z = std::min(Pos1.z, z);
            Pos2.x = std::max(Pos2.x, x + 1);
            Pos2.y = std::max(Pos2.y, y + 1);
            Pos2.z = std::max(Pos2.z, z + 1);
        }
    }

    writeRegionBlocks(filePath, existing);
}

void SchematicMap::setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks) {
    // Группируем изменения по регионам
    std::unordered_map<std::string, std::unordered_map<RegionPos, int32_t, RegionPosHash>> regionChanges;

    for (const auto& [x, y, z, id] : blocks) {
        int regionX = (x >= 0 ? x / REGION_SIZE : (x - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
        int regionY = y;
        int regionZ = (z >= 0 ? z / REGION_SIZE : (z - (REGION_SIZE - 1)) / REGION_SIZE) * REGION_SIZE;
        std::string regionName = getRegionName(regionX, regionY, regionZ);

        RegionPos pos{ x - regionX, y - regionY, z - regionZ };
        regionChanges[regionName][pos] = id; // перезаписываем, если уже есть
    }

    // Применяем изменения для каждого региона
    for (auto& [regionName, changes] : regionChanges) {
        fs::path filePath = getRegionPath(worldPath / "regions", regionName);
        auto existing = loadRegionBlocks(filePath);

        for (auto& [pos, id] : changes) {
            if (id < 0) {
                existing.erase(pos);
            }
            else {
                existing[pos] = id;
            }
        }

        writeRegionBlocks(filePath, existing);
    }
}

void SchematicMap::removeBlock(int x, int y, int z) {
    setBlock(x, y, z, -1);
}

bool SchematicMap::hasBlock(int x, int y, int z) const {
    return getBlock(x, y, z) >= 0;
}

sf::Color SchematicMap::getBlockColor(int blockId) const {
    if (blockId < 0)
        return sf::Color::Black;

    std::string blockName = palette.getName(blockId);
    if (blockName.empty())
        return sf::Color(200, 200, 200, 255);

    auto it = blockColors.find(blockName);
    if (it != blockColors.end())
        return it->second;

    return sf::Color(200, 200, 200, 255);
}

sf::Color SchematicMap::getBlockColor(int x, int y, int z) const {
    return getBlockColor(getBlock(x, y, z));
}

std::vector<SchematicMap::RegionBlock> SchematicMap::getRegionBlocks(int regionX, int regionY, int regionZ) const {
    std::vector<RegionBlock> result;

    std::string regionName = getRegionName(regionX, regionY, regionZ);
    fs::path filePath = getRegionPath(worldPath / "regions", regionName);

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) return result;

    auto fileSize = file.tellg();
    if (fileSize <= 0) return result;
    file.seekg(0, std::ios::beg);

    uint32_t count = static_cast<uint32_t>(fileSize / 16);
    result.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        RegionBlock b;
        file.read(reinterpret_cast<char*>(&b.x), 4);
        file.read(reinterpret_cast<char*>(&b.y), 4);
        file.read(reinterpret_cast<char*>(&b.z), 4);
        file.read(reinterpret_cast<char*>(&b.blockId), 4);
        // Convert local coordinates to absolute
        b.x += regionX;
        b.y += regionY;
        b.z += regionZ;
        result.push_back(b);
    }

    return result;
}

