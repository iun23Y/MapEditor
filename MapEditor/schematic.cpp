#include "schematic.h"
#include "helper.h"

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

namespace fs = std::filesystem;

namespace {

    enum class TagType : std::uint8_t {
        TAG_END = 0, TAG_BYTE = 1, TAG_SHORT = 2, TAG_INT = 3, TAG_LONG = 4,
        TAG_FLOAT = 5, TAG_DOUBLE = 6, TAG_BYTE_ARRAY = 7, TAG_STRING = 8,
        TAG_LIST = 9, TAG_COMPOUND = 10, TAG_INT_ARRAY = 11, TAG_LONG_ARRAY = 12
    };

    struct Tag {
        TagType type = TagType::TAG_END;
        std::string name;
        virtual ~Tag() = default;
    };

    struct TagByte : Tag { std::int8_t value = 0; };
    struct TagShort : Tag { std::int16_t value = 0; };
    struct TagInt : Tag { std::int32_t value = 0; };
    struct TagLong : Tag { std::int64_t value = 0; };
    struct TagFloat : Tag { float value = 0.0f; };
    struct TagDouble : Tag { double value = 0.0; };
    struct TagString : Tag { std::string value; };
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

    int floorRegion(int value) {
        if (value >= 0) return (value / SchematicMap::REGION_SIZE) * SchematicMap::REGION_SIZE;
        return ((value - (SchematicMap::REGION_SIZE - 1)) / SchematicMap::REGION_SIZE) * SchematicMap::REGION_SIZE;
    }

    struct IntPairHash {
        std::size_t operator()(const std::pair<int, int>& p) const noexcept {
            std::size_t seed = std::hash<int>{}(p.first);
            seed ^= std::hash<int>{}(p.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
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
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }

    void writeFloat(std::vector<uint8_t>& out, float v) {
        std::uint32_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        writeInt(out, static_cast<std::int32_t>(raw));
    }

    void writeDouble(std::vector<uint8_t>& out, double v) {
        std::uint64_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>((raw >> (i * 8)) & 0xFF));
    }

    void writeString(std::vector<uint8_t>& out, const std::string& s) {
        writeShort(out, static_cast<int16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }

    class NBTReader {
        std::vector<uint8_t> data;
        std::size_t pos = 0;

        void check(std::size_t count) {
            if (pos + count > data.size()) throw std::runtime_error("Invalid NBT data");
        }

    public:
        explicit NBTReader(const std::vector<uint8_t>& d) : data(d) {}

        uint8_t readByte() {
            check(1);
            return data[pos++];
        }

        int16_t readShort() {
            check(2);
            int16_t v = static_cast<int16_t>((static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1]);
            pos += 2;
            return v;
        }

        int32_t readInt() {
            check(4);
            int32_t v = (static_cast<int32_t>(data[pos]) << 24) | (static_cast<int32_t>(data[pos + 1]) << 16)
                | (static_cast<int32_t>(data[pos + 2]) << 8) | static_cast<int32_t>(data[pos + 3]);
            pos += 4;
            return v;
        }

        int64_t readLong() {
            check(8);
            int64_t v = 0;
            for (int i = 0; i < 8; ++i) v = (v << 8) | data[pos++];
            return v;
        }

        std::string readString() {
            uint16_t len = static_cast<uint16_t>(readShort());
            check(len);
            std::string result(data.begin() + pos, data.begin() + pos + len);
            pos += len;
            return result;
        }

        std::shared_ptr<Tag> readTag() {
            uint8_t type = readByte();
            if (type == 0) return nullptr;
            std::string name = readString();
            return readTagPayload(static_cast<TagType>(type), name);
        }

        std::shared_ptr<Tag> readTagPayload(TagType type, const std::string& name) {
            switch (type) {
            case TagType::TAG_BYTE: {
                auto t = std::make_shared<TagByte>();
                t->type = type; t->name = name;
                t->value = static_cast<int8_t>(readByte());
                return t;
            }
            case TagType::TAG_SHORT: {
                auto t = std::make_shared<TagShort>();
                t->type = type; t->name = name;
                t->value = readShort();
                return t;
            }
            case TagType::TAG_INT: {
                auto t = std::make_shared<TagInt>();
                t->type = type; t->name = name;
                t->value = readInt();
                return t;
            }
            case TagType::TAG_LONG: {
                auto t = std::make_shared<TagLong>();
                t->type = type; t->name = name;
                t->value = readLong();
                return t;
            }
            case TagType::TAG_FLOAT: {
                auto t = std::make_shared<TagFloat>();
                t->type = type; t->name = name;
                std::int32_t raw = readInt();
                std::memcpy(&t->value, &raw, sizeof(float));
                return t;
            }
            case TagType::TAG_DOUBLE: {
                auto t = std::make_shared<TagDouble>();
                t->type = type; t->name = name;
                std::int64_t raw = readLong();
                std::memcpy(&t->value, &raw, sizeof(double));
                return t;
            }
            case TagType::TAG_STRING: {
                auto t = std::make_shared<TagString>();
                t->type = type; t->name = name;
                t->value = readString();
                return t;
            }
            case TagType::TAG_BYTE_ARRAY: {
                int32_t len = readInt();
                if (len < 0) throw std::runtime_error("Invalid byte array length");
                auto t = std::make_shared<TagByteArray>();
                t->type = type; t->name = name;
                t->value.reserve(len);
                for (int32_t i = 0; i < len; ++i) t->value.push_back(readByte());
                return t;
            }
            case TagType::TAG_INT_ARRAY: {
                int32_t len = readInt();
                if (len < 0) throw std::runtime_error("Invalid int array length");
                auto t = std::make_shared<TagIntArray>();
                t->type = type; t->name = name;
                t->value.reserve(len);
                for (int32_t i = 0; i < len; ++i) t->value.push_back(readInt());
                return t;
            }
            case TagType::TAG_LONG_ARRAY: {
                int32_t len = readInt();
                if (len < 0) throw std::runtime_error("Invalid long array length");
                auto t = std::make_shared<TagLongArray>();
                t->type = type; t->name = name;
                t->value.reserve(len);
                for (int32_t i = 0; i < len; ++i) t->value.push_back(readLong());
                return t;
            }
            case TagType::TAG_LIST: {
                auto t = std::make_shared<TagList>();
                t->type = type; t->name = name;
                t->elementType = static_cast<TagType>(readByte());
                int32_t count = readInt();
                if (count < 0) throw std::runtime_error("Invalid list length");
                t->elements.reserve(count);
                for (int32_t i = 0; i < count; ++i)
                    t->elements.push_back(readTagPayload(t->elementType, ""));
                return t;
            }
            case TagType::TAG_COMPOUND: {
                auto t = std::make_shared<TagCompound>();
                t->type = type; t->name = name;
                while (true) {
                    uint8_t childType = readByte();
                    if (childType == 0) break;
                    std::string childName = readString();
                    auto child = readTagPayload(static_cast<TagType>(childType), childName);
                    if (child) t->children[childName] = child;
                }
                return t;
            }
            default:
                throw std::runtime_error("Unsupported NBT tag type");
            }
        }
    };

    void writeTagPayload(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out);

    void writeTag(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out) {
        if (!tag) return;
        out.push_back(static_cast<uint8_t>(tag->type));
        writeString(out, tag->name);
        writeTagPayload(tag, out);
    }

    void writeTagPayload(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out) {
        if (!tag) return;

        switch (tag->type) {
        case TagType::TAG_BYTE: writeByte(out, static_cast<uint8_t>(std::dynamic_pointer_cast<TagByte>(tag)->value)); break;
        case TagType::TAG_SHORT: writeShort(out, std::dynamic_pointer_cast<TagShort>(tag)->value); break;
        case TagType::TAG_INT: writeInt(out, std::dynamic_pointer_cast<TagInt>(tag)->value); break;
        case TagType::TAG_LONG: writeLong(out, std::dynamic_pointer_cast<TagLong>(tag)->value); break;
        case TagType::TAG_FLOAT: writeFloat(out, std::dynamic_pointer_cast<TagFloat>(tag)->value); break;
        case TagType::TAG_DOUBLE: writeDouble(out, std::dynamic_pointer_cast<TagDouble>(tag)->value); break;
        case TagType::TAG_STRING: writeString(out, std::dynamic_pointer_cast<TagString>(tag)->value); break;
        case TagType::TAG_BYTE_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagByteArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            out.insert(out.end(), t->value.begin(), t->value.end());
            break;
        }
        case TagType::TAG_INT_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagIntArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            for (auto v : t->value) writeInt(out, v);
            break;
        }
        case TagType::TAG_LONG_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagLongArray>(tag);
            writeInt(out, static_cast<int32_t>(t->value.size()));
            for (auto v : t->value) writeLong(out, v);
            break;
        }
        case TagType::TAG_LIST: {
            auto t = std::dynamic_pointer_cast<TagList>(tag);
            writeByte(out, static_cast<uint8_t>(t->elementType));
            writeInt(out, static_cast<int32_t>(t->elements.size()));
            for (const auto& child : t->elements) writeTagPayload(child, out);
            break;
        }
        case TagType::TAG_COMPOUND: {
            auto t = std::dynamic_pointer_cast<TagCompound>(tag);
            for (const auto& [childName, child] : t->children) writeTag(child, out);
            writeByte(out, 0);
            break;
        }
        default:
            throw std::runtime_error("Unsupported NBT tag");
        }
    }

    std::vector<uint8_t> gzipCompress(const std::vector<uint8_t>& data) {
        z_stream strm{};
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
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
            std::size_t count = sizeof(buffer) - strm.avail_out;
            output.insert(output.end(), buffer, buffer + count);
        } while (ret != Z_STREAM_END);

        deflateEnd(&strm);
        return output;
    }

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
            std::size_t count = sizeof(buffer) - strm.avail_out;
            output.insert(output.end(), buffer, buffer + count);
        } while (ret != Z_STREAM_END);

        inflateEnd(&strm);
        return output;
    }

    // ---------- низкоуровневый ввод/вывод .region файлов ----------

    using Record = SchematicMap::RegionRecord;
    static_assert(sizeof(Record) == 16, "RegionRecord must be exactly 16 bytes (disk format)");

    void writeRegionFile(const fs::path& path, const std::vector<Record>& records) {
        if (records.empty()) {
            std::error_code ec;
            fs::remove(path, ec);
            return;
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot write region: " + path.string());
        file.write(reinterpret_cast<const char*>(records.data()),
            static_cast<std::streamsize>(records.size() * sizeof(Record)));
        if (!file) throw std::runtime_error("Failed writing region: " + path.string());
    }

    void appendRegionRecord(const fs::path& path, const Record& rec) {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file) throw std::runtime_error("Cannot append to region: " + path.string());
        file.write(reinterpret_cast<const char*>(&rec), sizeof(Record));
        if (!file) throw std::runtime_error("Failed writing region: " + path.string());
    }

    void rewriteRegionRecord(const fs::path& path, std::size_t recordIndex, const Record& rec) {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file) throw std::runtime_error("Cannot update region: " + path.string());
        file.seekp(static_cast<std::streamoff>(recordIndex * sizeof(Record)));
        file.write(reinterpret_cast<const char*>(&rec), sizeof(Record));
        if (!file) throw std::runtime_error("Failed writing region: " + path.string());
    }

    void truncateRegionFile(const fs::path& path, std::size_t recordCount) {
        if (recordCount == 0) {
            std::error_code ec;
            fs::remove(path, ec);
            return;
        }
        std::error_code ec;
        fs::resize_file(path, static_cast<std::uintmax_t>(recordCount * sizeof(Record)), ec);
        if (ec) throw std::runtime_error("Cannot truncate region: " + path.string());
    }

    std::vector<Record> readRegionFile(const fs::path& path) {
        std::vector<Record> records;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return records;

        const std::streamsize size = file.tellg();
        if (size <= 0) return records;

        const std::size_t count = static_cast<std::size_t>(size) / sizeof(Record);
        records.resize(count);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(records.data()),
            static_cast<std::streamsize>(count * sizeof(Record)));
        records.resize(static_cast<std::size_t>(file.gcount()) / sizeof(Record));

        auto valid = [](const Record& r) {
            return r.y == 0 &&
                r.x >= 0 && r.x < SchematicMap::REGION_SIZE &&
                r.z >= 0 && r.z < SchematicMap::REGION_SIZE;
            };

        bool needsRepair = size % static_cast<std::streamsize>(sizeof(Record)) != 0;

        std::vector<std::uint32_t> keys(records.size());
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (!valid(records[i])) { needsRepair = true; break; }
            keys[i] = static_cast<std::uint32_t>(records[i].x) * SchematicMap::REGION_SIZE +
                static_cast<std::uint32_t>(records[i].z);
        }
        if (!needsRepair) {
            std::sort(keys.begin(), keys.end());
            needsRepair = std::adjacent_find(keys.begin(), keys.end()) != keys.end();
        }
        if (!needsRepair) return records;
        std::unordered_map<std::uint32_t, std::size_t> lastAt;
        lastAt.reserve(records.size() * 2);
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (!valid(records[i])) continue;
            lastAt[static_cast<std::uint32_t>(records[i].x) * SchematicMap::REGION_SIZE +
                static_cast<std::uint32_t>(records[i].z)] = i;
        }
        std::vector<Record> clean;
        clean.reserve(lastAt.size());
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (!valid(records[i])) continue;
            if (lastAt[static_cast<std::uint32_t>(records[i].x) * SchematicMap::REGION_SIZE +
                static_cast<std::uint32_t>(records[i].z)] == i)
                clean.push_back(records[i]);
        }
        records = std::move(clean);
        std::cerr << "Warning: repaired region file " << path.string() << "\n";
        writeRegionFile(path, records);
        return records;
    }
}

void BlockPalette::addBlock(int id, const std::string& name) {
    if (id == -1) id = 0;
    if (idByName.find(name) != idByName.end() || nameById.find(id) != nameById.end()) return;
    nameById[id] = name;
    idByName[name] = id;
}

bool BlockPalette::hasBlock(int id) const {
    return nameById.find(id) != nameById.end();
}

bool BlockPalette::hasBlock(const std::string& name) const {
    return idByName.find(name) != idByName.end();
}

int BlockPalette::getId(const std::string& name) const {
    auto it = idByName.find(name);
    return it == idByName.end() ? -1 : it->second;
}

std::string BlockPalette::getName(int id) const {
    auto it = nameById.find(id);
    return it == nameById.end() ? "" : it->second;
}

void SchematicMap::rebuildRegionDirIndex() const {
    knownRegionFiles.clear();
    std::error_code ec;
    for (fs::directory_iterator it(worldPath / "regions", ec), end; it != end; ++it) {
        std::string name = it->path().filename().string();
        if (name.size() < 11 || name.compare(name.size() - 7, 7, ".region") != 0)
            continue;
        const std::string core = name.substr(0, name.size() - 7);
        const std::size_t p1 = core.find('_');
        const std::size_t p2 = (p1 == std::string::npos) ? std::string::npos
            : core.find('_', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos ||
            core.find('_', p2 + 1) != std::string::npos)
            continue;
        try {
            knownRegionFiles.insert(RegionKey{
                std::stoi(core.substr(0, p1)),
                std::stoi(core.substr(p1 + 1, p2 - p1 - 1)),
                std::stoi(core.substr(p2 + 1)) });
        }
        catch (...) {}
    }
    dirIndexValid = true;
}

std::pair<std::int32_t, std::size_t> readVarint(const std::vector<std::uint8_t>& data, std::size_t offset) {
    std::int32_t result = 0;
    int shift = 0;

    while (offset < data.size()) {
        uint8_t byte = data[offset++];
        result |= static_cast<std::int32_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80)) break;
        shift += 7;
        if (shift >= 32) break;
    }

    return { result, offset };
}

std::vector<std::int32_t> decodeBlockIndices(const std::vector<std::uint8_t>& rawData, int expectedCount) {
    std::vector<std::int32_t> result;
    result.reserve(expectedCount);

    std::size_t offset = 0;

    while (offset < rawData.size() && static_cast<int>(result.size()) < expectedCount) {
        auto [value, newOffset] = readVarint(rawData, offset);
        if (newOffset <= offset) break;
        offset = newOffset;
        result.push_back(value);
    }

    result.resize(expectedCount, 0);
    return result;
}

int SchematicMap::getRegionCoord(int coordinate) {
    return floorRegion(coordinate);
}

std::string SchematicMap::getRegionName(int regionX, int regionY, int regionZ) {
    return std::to_string(regionX) + "_" + std::to_string(regionY) + "_" + std::to_string(regionZ);
}

fs::path SchematicMap::getRegionPath(const fs::path& regionsDir, const std::string& regionName) {
    return regionsDir / (regionName + ".region");
}

SchematicMap::SchematicMap(const std::string& filename, const std::string& worldDir) {
    worldPath = fs::path(worldDir);
    fs::create_directories(worldPath / "regions");
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
        catch (const std::exception&) {
        }
    }
}

void SchematicMap::savePalette() const {
    std::vector<std::pair<int, std::string>> entries(palette.nameById.begin(), palette.nameById.end());
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::ofstream out(worldPath / "palette.dat", std::ios::trunc);
    if (!out) return;
    for (const auto& [id, name] : entries)
        out << id << ' ' << name << '\n';
}

void SchematicMap::loadMeta() {
    struct MetaData {
        int32_t hasBounds;
        int32_t x1, y1, z1;
        int32_t x2, y2, z2;
    };

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
    struct MetaData {
        int32_t hasBounds;
        int32_t x1, y1, z1;
        int32_t x2, y2, z2;
    };

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
}

void SchematicMap::RegionData::ensureIndex() {
    if (indexed) return;

    index.assign(static_cast<std::size_t>(REGION_SIZE) * REGION_SIZE, -1);

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const RegionRecord& b = blocks[i];
        index[static_cast<std::size_t>(b.x) * REGION_SIZE + static_cast<std::size_t>(b.z)] =
            static_cast<int32_t>(i);
    }

    indexed = true;
}

std::shared_ptr<SchematicMap::RegionData> SchematicMap::getRegionData(int regionX, int regionY, int regionZ) const {
    const RegionKey key{ regionX, regionY, regionZ };

    if (!dirIndexValid) rebuildRegionDirIndex();
    if (knownRegionFiles.find(key) == knownRegionFiles.end()) {
        auto data = std::make_shared<RegionData>();
        data->lastUsed = ++cacheClock;
        regionCache.emplace(key, data);
        return data;
    }

    auto it = regionCache.find(key);
    if (it != regionCache.end()) {
        it->second->lastUsed = ++cacheClock;
        return it->second;
    }

    if (regionCache.size() >= MAX_CACHED_REGIONS) {
        auto victim = regionCache.begin();
        for (auto i = regionCache.begin(); i != regionCache.end(); ++i)
            if (i->second->lastUsed < victim->second->lastUsed)
                victim = i;
        regionCache.erase(victim);
    }

    auto data = std::make_shared<RegionData>();
    data->blocks = readRegionFile(getRegionPath(worldPath / "regions",
        getRegionName(regionX, regionY, regionZ)));
    data->lastUsed = ++cacheClock;
    regionCache.emplace(key, data);
    return data;
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

void SchematicMap::loadFromFile(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open schematic: " + filename);

    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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

    const std::int64_t volume64 = static_cast<std::int64_t>(width) * height * length;
    if (volume64 > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
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

            const int schematicId = std::dynamic_pointer_cast<TagInt>(tag)->value;
            int worldId;

            if (palette.hasBlock(name)) {
                worldId = palette.getId(name);
            }
            else {
                worldId = static_cast<int>(palette.nameById.size());
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
        if (name == "minecraft:air" || name == "air" || name == "___reserved___" || name == "void")
            ignored.insert(id);

    fs::path regionsDir = worldPath / "regions";
    fs::create_directories(regionsDir);

    std::unordered_map<RegionKey, std::vector<RegionRecord>, RegionKeyHash> grouped;

    for (int y = 0; y < height; ++y) {
        for (int z = 0; z < length; ++z) {
            for (int x = 0; x < width; ++x) {
                const std::int64_t index = (static_cast<std::int64_t>(y) * length + z) * width + x;
                if (index >= static_cast<std::int64_t>(indices.size())) continue;

                const int schematicId = indices[static_cast<std::size_t>(index)];
                auto remap = idRemap.find(schematicId);
                if (remap == idRemap.end()) continue;
                if (ignored.count(remap->second)) continue;

                const int absX = offsetX + x;
                const int absY = offsetY + y;
                const int absZ = offsetZ + z;

                RegionKey key{ floorRegion(absX), absY, floorRegion(absZ) };
                grouped[key].push_back({ absX - key.x, 0, absZ - key.z, remap->second });
            }
        }
    }

    for (auto& [key, newRecords] : grouped) {
        const fs::path path = getRegionPath(regionsDir, getRegionName(key.x, key.y, key.z));
        const auto oldRecords = readRegionFile(path);

        std::unordered_map<sf::Vector3i, int32_t> merged;
        merged.reserve(oldRecords.size() + newRecords.size());
        for (const auto& b : oldRecords) merged[{ b.x, b.y, b.z }] = b.id;
        for (const auto& b : newRecords) merged[{ b.x, b.y, b.z }] = b.id;

        std::vector<RegionRecord> records;
        records.reserve(merged.size());
        for (const auto& [pos, id] : merged)
            records.push_back({ pos.x, pos.y, pos.z, id });

        writeRegionFile(path, records);
    }

    regionCache.clear();
    dirIndexValid = false;
    saveWorldState();
}

std::vector<SchematicMap::RegionBlock> SchematicMap::getRegionBlocks(int regionX, int regionY, int regionZ) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto data = getRegionData(regionX, regionY, regionZ);

    std::vector<RegionBlock> result;
    result.reserve(data->blocks.size());

    for (const auto& b : data->blocks)
        result.push_back({ b.x + regionX, b.y + regionY, b.z + regionZ, b.id });

    return result;
}

std::vector<SchematicMap::RegionBlock> SchematicMap::getBlocksInArea(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::vector<RegionBlock> result;
    if (minX > maxX || minY > maxY || minZ > maxZ) return result;

    for (int ry = minY; ry <= maxY; ++ry) {
        for (int rx = floorRegion(minX); rx <= floorRegion(maxX); rx += REGION_SIZE) {
            for (int rz = floorRegion(minZ); rz <= floorRegion(maxZ); rz += REGION_SIZE) {
                auto data = getRegionData(rx, ry, rz);

                for (const auto& b : data->blocks) {
                    const int32_t ax = b.x + rx;
                    const int32_t ay = b.y + ry;
                    const int32_t az = b.z + rz;
                    if (ax < minX || ax > maxX || ay < minY || ay > maxY || az < minZ || az > maxZ) continue;
                    result.push_back({ ax, ay, az, b.id });
                }
            }
        }
    }

    return result;
}

std::vector<SchematicMap::RegionBlock> SchematicMap::getTopBlocksInArea(int minX, int minZ, int maxX, int maxZ) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!hasBounds || minX > maxX || minZ > maxZ) return {};

    std::unordered_map<std::pair<int, int>, RegionBlock, IntPairHash> topMap;

    const int minY = Pos1.y;
    const int maxY = Pos2.y - 1;

    const std::size_t totalColumns =
        static_cast<std::size_t>(maxX - minX + 1) * static_cast<std::size_t>(maxZ - minZ + 1);

    // »дЄм сверху вниз: первый встреченный блок колонки Ч самый высокий.
    for (int ry = maxY; ry >= minY; --ry) {
        for (int rx = floorRegion(minX); rx <= floorRegion(maxX); rx += REGION_SIZE) {
            for (int rz = floorRegion(minZ); rz <= floorRegion(maxZ); rz += REGION_SIZE) {
                auto data = getRegionData(rx, ry, rz);

                for (const auto& b : data->blocks) {
                    const int32_t ax = b.x + rx;
                    const int32_t az = b.z + rz;
                    if (ax < minX || ax > maxX || az < minZ || az > maxZ) continue;

                    topMap.emplace(std::make_pair(static_cast<int>(ax), static_cast<int>(az)),
                        RegionBlock{ ax, ry + b.y, az, b.id });
                }
            }
        }

        if (topMap.size() >= totalColumns) break; // все колонки уже найдены
    }

    std::vector<RegionBlock> result;
    result.reserve(topMap.size());
    for (const auto& [key, block] : topMap)
        result.push_back(block);

    return result;
}

int SchematicMap::getBlock(int x, int y, int z) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const int regionX = floorRegion(x);
    const int regionZ = floorRegion(z);

    auto data = getRegionData(regionX, y, regionZ);
    data->ensureIndex();

    const int32_t lx = static_cast<int32_t>(x - regionX);
    const int32_t lz = static_cast<int32_t>(z - regionZ);

    const int32_t idx = data->index[static_cast<std::size_t>(lx) * REGION_SIZE + static_cast<std::size_t>(lz)];
    if (idx < 0) return -1;
    return data->blocks[static_cast<std::size_t>(idx)].id;
}

void SchematicMap::setBlock(int x, int y, int z, int blockId) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const int regionX = floorRegion(x);
    const int regionY = y;
    const int regionZ = floorRegion(z);

    const RegionKey key{ regionX, regionY, regionZ };

    auto data = getRegionData(regionX, regionY, regionZ);
    data->ensureIndex();

    const int32_t lx = static_cast<int32_t>(x - regionX);
    const int32_t lz = static_cast<int32_t>(z - regionZ);
    const std::size_t slot = static_cast<std::size_t>(lx) * REGION_SIZE + static_cast<std::size_t>(lz);
    const int32_t idx = data->index[slot];

    const fs::path path = getRegionPath(worldPath / "regions",
        getRegionName(regionX, regionY, regionZ));

    if (blockId >= 0) {
        const RegionRecord rec{ lx, 0, lz, blockId };

        if (idx >= 0) {
            data->blocks[static_cast<std::size_t>(idx)].id = blockId;
            rewriteRegionRecord(path, static_cast<std::size_t>(idx), rec);
        }
        else {
            data->blocks.push_back(rec);
            data->index[slot] = static_cast<int32_t>(data->blocks.size() - 1);
            appendRegionRecord(path, rec);
            knownRegionFiles.insert(key);
        }

        extendBounds(x, y, z);
        return;
    }
    if (idx < 0) return;

    const std::size_t removeAt = static_cast<std::size_t>(idx);
    const std::size_t last = data->blocks.size() - 1;

    if (removeAt != last) {
        const RegionRecord moved = data->blocks[last];
        data->blocks[removeAt] = moved;
        rewriteRegionRecord(path, removeAt, moved);
        data->index[static_cast<std::size_t>(moved.x) * REGION_SIZE + static_cast<std::size_t>(moved.z)] =
            static_cast<int32_t>(removeAt);
    }

    data->blocks.pop_back();
    truncateRegionFile(path, data->blocks.size());
    data->index[slot] = -1;

    if (data->blocks.empty())
        knownRegionFiles.erase(key);
}

void SchematicMap::setBlocks(const std::vector<std::tuple<int, int, int, int>>& blocks) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (blocks.empty()) return;

    std::unordered_map<RegionKey, std::vector<std::tuple<int, int, int, int>>, RegionKeyHash> grouped;

    for (const auto& entry : blocks) {
        const auto& [x, y, z, id] = entry;
        (void)id;
        grouped[RegionKey{ floorRegion(x), y, floorRegion(z) }].push_back(entry);
    }

    for (auto& [key, changes] : grouped) {
        auto data = getRegionData(key.x, key.y, key.z);

        std::unordered_map<sf::Vector3i, int32_t> merged;
        merged.reserve(data->blocks.size() + changes.size());

        for (const auto& b : data->blocks)
            merged[{ b.x, b.y, b.z }] = b.id;

        for (const auto& [x, y, z, id] : changes) {
            const sf::Vector3i local(x - key.x, 0, z - key.z);
            if (id >= 0)
                merged[local] = id;
            else
                merged.erase(local);
        }

        auto fresh = std::make_shared<RegionData>();
        fresh->blocks.reserve(merged.size());
        for (const auto& [pos, id] : merged)
            fresh->blocks.push_back({ pos.x, pos.y, pos.z, id });

        const fs::path path = getRegionPath(worldPath / "regions",
            getRegionName(key.x, key.y, key.z));

        writeRegionFile(path, fresh->blocks);
        if (fresh->blocks.empty()) knownRegionFiles.erase(key);
        else knownRegionFiles.insert(key);

        regionCache[key] = std::move(fresh);

        for (const auto& [x, y, z, id] : changes)
            if (id >= 0) extendBounds(x, y, z);
    }
}

void SchematicMap::removeBlock(int x, int y, int z) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    setBlock(x, y, z, -1);
}

bool SchematicMap::hasBlock(int x, int y, int z) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return getBlock(x, y, z) >= 0;
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

    const int worldMinX = Pos1.x;
    const int worldMaxX = Pos2.x - 1;
    const int worldMinY = Pos1.y;
    const int worldMaxY = Pos2.y - 1;
    const int worldMinZ = Pos1.z;
    const int worldMaxZ = Pos2.z - 1;

    const int cellsX = (worldMaxX - worldMinX + CELL_SIZE) / CELL_SIZE;
    const int cellsZ = (worldMaxZ - worldMinZ + CELL_SIZE) / CELL_SIZE;

    auto makeInt = [](const std::string& name, std::int32_t value) {
        auto t = std::make_shared<TagInt>();
        t->type = TagType::TAG_INT;
        t->name = name;
        t->value = value;
        return t;
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
                if (name.empty()) {
                    globalToLocal[b.blockId] = 0; // неизвестный id -> air
                }
                else {
                    globalToLocal[b.blockId] = static_cast<int32_t>(paletteNames.size());
                    paletteNames.push_back(std::move(name));
                }
            }

            const int width = maxX - minX + 1;
            const int height = worldMaxY - worldMinY + 1;
            const int length = maxZ - minZ + 1;

            const std::size_t volume =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                static_cast<std::size_t>(length);
            std::vector<int32_t> indices(volume, 0);

            for (const auto& b : blocks) {
                const std::size_t index =
                    (static_cast<std::size_t>(b.y - worldMinY) * static_cast<std::size_t>(length) +
                        static_cast<std::size_t>(b.z - minZ)) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(b.x - minX);
                indices[index] = globalToLocal[b.blockId];
            }

            std::vector<uint8_t> blockData;
            blockData.reserve(indices.size());

            for (int32_t value : indices) {
                uint32_t v = static_cast<uint32_t>(value);
                do {
                    uint8_t byte = static_cast<uint8_t>(v & 0x7F);
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
            schem->children["Width"] = makeInt("Width", width);
            schem->children["Height"] = makeInt("Height", height);
            schem->children["Length"] = makeInt("Length", length);

            auto offset = std::make_shared<TagIntArray>();
            offset->type = TagType::TAG_INT_ARRAY;
            offset->name = "Offset";
            offset->value = { minX, worldMinY, minZ };
            schem->children["Offset"] = offset;

            auto metadata = std::make_shared<TagCompound>();
            metadata->type = TagType::TAG_COMPOUND;
            metadata->name = "Metadata";

            auto author = std::make_shared<TagString>();
            author->type = TagType::TAG_STRING;
            author->name = "author";
            author->value = "MapEditor";

            auto name = std::make_shared<TagString>();
            name->type = TagType::TAG_STRING;
            name->name = "name";
            name->value = "ME-BTE-schematic";

            metadata->children["author"] = author;
            metadata->children["name"] = name;
            schem->children["Metadata"] = metadata;

            auto blocksTag = std::make_shared<TagCompound>();
            blocksTag->type = TagType::TAG_COMPOUND;
            blocksTag->name = "Blocks";

            auto paletteTag = std::make_shared<TagCompound>();
            paletteTag->type = TagType::TAG_COMPOUND;
            paletteTag->name = "Palette";

            for (std::size_t i = 0; i < paletteNames.size(); ++i)
                paletteTag->children[paletteNames[i]] = makeInt(paletteNames[i], static_cast<std::int32_t>(i));

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
            if (!out) {
                std::cerr << "Failed to create " << output << "\n";
                continue;
            }

            out.write(reinterpret_cast<const char*>(compressed.data()),
                static_cast<std::streamsize>(compressed.size()));
            std::cout << "Exported " << output << " (" << blocks.size() << " blocks)\n";
        }
    }
}

std::unordered_map<sf::Vector3i, int32_t> SchematicMap::loadRegionBlocks(const fs::path& filePath) {
    std::unordered_map<sf::Vector3i, int32_t> result;
    auto records = readRegionFile(filePath);
    result.reserve(records.size());
    for (const auto& b : records)
        result[{ b.x, b.y, b.z }] = b.id;
    return result;
}

void SchematicMap::writeRegionBlocks(const fs::path& filePath, const std::unordered_map<sf::Vector3i, int32_t>& blocks) {
    std::vector<RegionRecord> records;
    records.reserve(blocks.size());
    for (const auto& [pos, id] : blocks)
        records.push_back({ pos.x, pos.y, pos.z, id });
    writeRegionFile(filePath, records);
}

void SchematicMap::invalidateRegionCache(int regionX, int regionZ) {
    for (auto it = regionCache.begin(); it != regionCache.end();) {
        if (it->first.x == regionX && it->first.z == regionZ)
            it = regionCache.erase(it);
        else
            ++it;
    }
}

void SchematicMap::clearRegionCache() {
    regionCache.clear();
}