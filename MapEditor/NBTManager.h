#pragma once
#include "helper.h"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
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
namespace NBT {
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

    inline int floorRegion(int value) {
        if (value >= 0) return (value / 1000) * 1000;
        return ((value - (1000 - 1)) / 1000) * 1000;
    }

    struct IntPairHash {
        std::size_t operator()(const std::pair<int, int>& p) const noexcept {
            std::size_t seed = std::hash<int>{}(p.first);
            seed ^= std::hash<int>{}(p.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    inline void writeByte(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
    inline void writeShort(std::vector<uint8_t>& out, int16_t v) {
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    inline void writeInt(std::vector<uint8_t>& out, std::int32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    inline void writeLong(std::vector<uint8_t>& out, int64_t v) {
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    inline void writeFloat(std::vector<uint8_t>& out, float v) {
        std::int32_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        writeInt(out, static_cast<std::int32_t>(raw));
    }
    inline void writeDouble(std::vector<uint8_t>& out, double v) {
        std::uint64_t raw = 0;
        std::memcpy(&raw, &v, sizeof(raw));
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>((raw >> (i * 8)) & 0xFF));
    }
    inline void writeString(std::vector<uint8_t>& out, const std::string& s) {
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

        std::int32_t readInt() {
            check(4);
            std::int32_t v = (static_cast<std::int32_t>(data[pos]) << 24) | (static_cast<std::int32_t>(data[pos + 1]) << 16)
                | (static_cast<std::int32_t>(data[pos + 2]) << 8) | static_cast<std::int32_t>(data[pos + 3]);
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
                std::int32_t len = readInt();
                if (len < 0) throw std::runtime_error("Invalid byte array length");
                auto t = std::make_shared<TagByteArray>();
                t->type = type; t->name = name;
                t->value.reserve(len);
                for (std::int32_t i = 0; i < len; ++i) t->value.push_back(readByte());
                return t;
            }
            case TagType::TAG_INT_ARRAY: {
                std::int32_t len = readInt();
                if (len < 0) throw std::runtime_error("Invalid int array length");
                auto t = std::make_shared<TagIntArray>();
                t->type = type; t->name = name;
                t->value.reserve(len);
                for (std::int32_t i = 0; i < len; ++i) t->value.push_back(readInt());
                return t;
            }
            case TagType::TAG_LONG_ARRAY: {
                std::int32_t len = readInt();
                if (len < 0) throw std::runtime_error("Invalid long array length");
                auto t = std::make_shared<TagLongArray>();
                t->type = type; t->name = name;
                t->value.reserve(len);
                for (std::int32_t i = 0; i < len; ++i) t->value.push_back(readLong());
                return t;
            }
            case TagType::TAG_LIST: {
                auto t = std::make_shared<TagList>();
                t->type = type; t->name = name;
                t->elementType = static_cast<TagType>(readByte());
                std::int32_t count = readInt();
                if (count < 0) throw std::runtime_error("Invalid list length");
                t->elements.reserve(count);
                for (std::int32_t i = 0; i < count; ++i)
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

    inline void writeTagPayload(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out);
    inline void writeTag(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out) {
        if (!tag) return;
        out.push_back(static_cast<uint8_t>(tag->type));
        writeString(out, tag->name);
        writeTagPayload(tag, out);
    }
    inline void writeTagPayload(const std::shared_ptr<Tag>& tag, std::vector<uint8_t>& out) {
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
            writeInt(out, static_cast<std::int32_t>(t->value.size()));
            out.insert(out.end(), t->value.begin(), t->value.end());
            break;
        }
        case TagType::TAG_INT_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagIntArray>(tag);
            writeInt(out, static_cast<std::int32_t>(t->value.size()));
            for (auto v : t->value) writeInt(out, v);
            break;
        }
        case TagType::TAG_LONG_ARRAY: {
            auto t = std::dynamic_pointer_cast<TagLongArray>(tag);
            writeInt(out, static_cast<std::int32_t>(t->value.size()));
            for (auto v : t->value) writeLong(out, v);
            break;
        }
        case TagType::TAG_LIST: {
            auto t = std::dynamic_pointer_cast<TagList>(tag);
            writeByte(out, static_cast<uint8_t>(t->elementType));
            writeInt(out, static_cast<std::int32_t>(t->elements.size()));
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

    inline std::pair<std::int32_t, std::size_t> readVarint(const std::vector<std::uint8_t>& data, std::size_t offset) {
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
    inline std::vector<std::int32_t> decodeBlockIndices(const std::vector<std::uint8_t>& rawData, int expectedCount) {
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
};

