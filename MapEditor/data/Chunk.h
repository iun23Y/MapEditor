#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <limits>

class Chunk {
public:
    static constexpr int SIZE = 100;
    static constexpr size_t VOLUME = static_cast<size_t>(SIZE) * SIZE * SIZE;
    static constexpr uint16_t AIR = 0;

    Chunk() : blocks(VOLUME, AIR) {}

    inline uint16_t get(int x, int y, int z) const {
        return blocks[index(x, y, z)];
    }

    inline void set(int x, int y, int z, uint16_t id) {
        auto& b = blocks[index(x, y, z)];
        if (b == id) return;

        if (b == AIR && id != AIR) {
            if (y > maxY_) maxY_ = y;
        }
        else if (b != AIR && id == AIR) {
            if (y == maxY_) maxYTight_ = false;
        }
        b = id;
        dirty = true;
    }

    int  maxY() const {
        if (!maxYTight_) recomputeMaxY();
        return maxY_;
    }

    bool isEmpty() const { return maxY() < 0; }

    void clear() {
        std::fill(blocks.begin(), blocks.end(), AIR);
        maxY_ = -1;
        maxYTight_ = true;
        dirty = true;
    }

    std::vector<uint8_t> encodeRLE() const;
    bool decodeRLE(const std::vector<uint8_t>& data);

    bool dirty = false;

private:
    std::vector<uint16_t> blocks;
    mutable int  maxY_ = -1;
    mutable bool maxYTight_ = true;

    inline static size_t index(int x, int y, int z) {
        return (static_cast<size_t>(y) * SIZE + z) * SIZE + x;
    }

    void recomputeMaxY() const {
        maxY_ = -1;
        for (int y = SIZE - 1; y >= 0; --y) {
            for (int z = 0; z < SIZE; ++z) {
                for (int x = 0; x < SIZE; ++x) {
                    if (blocks[index(x, y, z)] != AIR) {
                        maxY_ = y;
                        maxYTight_ = true;
                        return;
                    }
                }
            }
        }
        maxYTight_ = true;
    }
};