#pragma once

#include <glm/glm.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <fstream> 
#include <optional>
#include "Chunk.h"
#include "helper.h"
#include <iostream>

namespace fs = std::filesystem;

struct ChunkPersistenceMeta {
	int idObject;
	int idChunk;
	glm::ivec3 pos;
};

namespace ChunkPersistence
{
    inline constexpr uint32_t MAGIC = 0x4A424F43;
    inline constexpr uint8_t VERSION = 1;

    bool saveChunk(const ChunkPersistenceMeta& meta, const Chunk& chunk, const fs::path& objectDir);
    std::optional<Chunk> load(const ChunkPersistenceMeta& meta, const fs::path& objectDir);
};

