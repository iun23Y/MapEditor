#include "Chunk.h"
#include <stdexcept>

namespace {
    void writeVarint(std::vector<uint8_t>& out, uint32_t v) {
        while (v >= 0x80) {
            out.push_back(static_cast<uint8_t>(v) | 0x80);
            v >>= 7;
        }
        out.push_back(static_cast<uint8_t>(v));
    }

    uint32_t readVarint(const uint8_t*& p, const uint8_t* end) {
        uint32_t v = 0;
        int shift = 0;
        while (p < end) {
            uint8_t b = *p++;
            v |= static_cast<uint32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
            shift += 7;
            if (shift > 28) throw std::runtime_error("varint overflow");
        }
        throw std::runtime_error("varint truncated");
    }
}

std::vector<uint8_t> Chunk::encodeRLE() const {
    std::vector<uint8_t> out;
    out.reserve(8192);

    size_t i = 0;
    while (i < VOLUME) {
        const uint16_t v = blocks[i];
        size_t j = i + 1;
        while (j < VOLUME && blocks[j] == v) ++j;

        writeVarint(out, static_cast<uint32_t>(j - i));
        writeVarint(out, v);
        i = j;
    }
    return out;
}

bool Chunk::decodeRLE(const std::vector<uint8_t>& data) {
    std::fill(blocks.begin(), blocks.end(), AIR);
    maxY_ = -1;
    maxYTight_ = false;

    const uint8_t* p = data.data();
    const uint8_t* end = p + data.size();

    try {
        size_t i = 0;
        while (i < VOLUME && p < end) {
            const uint32_t count = readVarint(p, end);
            const uint32_t id = readVarint(p, end);
            if (count == 0 || i + count > VOLUME) return false;

            for (uint32_t k = 0; k < count; ++k)
                blocks[i + k] = static_cast<uint16_t>(id);
            i += count;
        }
        if (i != VOLUME) return false;
    }
    catch (...) {
        return false;
    }

    // пересчитать maxY после декода
    recomputeMaxY();
    dirty = false;
    return true;
}