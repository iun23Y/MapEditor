#pragma once
#include "Chunk.h"
#include "BlockPalette.h"
#include "Bounds.h"

#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace Schematic {
    bool loadForSchematic(const std::string& filename,
        BlockPalette& palette,
        std::vector<int>& outBlocks,
        Bounds& outBounds);
    bool exportToSchematic(const std::vector<int>& data,
        const Bounds& bound,
        const BlockPalette& palette,
        const std::string& outputDir,
        const std::string& baseName);
};