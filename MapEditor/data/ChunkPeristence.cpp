#include "ChunkPersistence.h"
namespace ChunkPersistence
{
    bool saveChunk(const ChunkPersistenceMeta& meta, const Chunk& chunk, const fs::path& objectDir) {
        fs::create_directories(objectDir);
        fs::path fullPath = objectDir / (std::to_string(meta.idChunk) + ".cobject");

        std::ofstream out(fullPath, std::ios::binary);
        if (!out) return false;

        const glm::ivec3 cm = chunk.getMinPos();
        const auto rle = chunk.encodeRLE();
        const uint32_t size = (uint32_t)rle.size();

        out.write((char*)&MAGIC, 4);
        out.write((char*)&VERSION, 1);
        out.write((char*)&cm.x, 4);
        out.write((char*)&cm.y, 4);
        out.write((char*)&cm.z, 4);
        out.write((char*)&size, 4);
        out.write((char*)rle.data(), size);
        return out.good();
    }
    std::optional<Chunk> load(const ChunkPersistenceMeta& meta, const fs::path& objectDir) {
        fs::path file = objectDir / (std::to_string(meta.idChunk) + ".cobject");

        std::ifstream in(file, std::ios::binary);
        if (!in) {
            std::cerr << "load: cannot open " << file << "\n";
            return std::nullopt;
        }

        uint32_t magic = 0;
        in.read(reinterpret_cast<char*>(&magic), 4);
        if (!in || magic != static_cast<uint32_t>(MAGIC)) {
            std::cerr << "load: invalid magic in " << file << "\n";
            return std::nullopt;
        }

        uint8_t version = 0;
        in.read(reinterpret_cast<char*>(&version), 1);
        if (!in || version != VERSION) {
            std::cerr << "load: unsupported version in " << file << "\n";
            return std::nullopt;
        }

        glm::ivec3 cm{ 0 };
        in.read(reinterpret_cast<char*>(&cm.x), 4);
        in.read(reinterpret_cast<char*>(&cm.y), 4);
        in.read(reinterpret_cast<char*>(&cm.z), 4);
        if (!in) return std::nullopt;

        if (cm != meta.pos) {
            std::cerr << "load: position mismatch in " << file
                << " (expected " << meta.pos.x << "," << meta.pos.y << "," << meta.pos.z
                << ", got " << cm.x << "," << cm.y << "," << cm.z << ")\n";
            return std::nullopt;
        }

        uint32_t size = 0;
        in.read(reinterpret_cast<char*>(&size), 4);
        if (!in || size == 0) return std::nullopt;

        std::vector<uint8_t> rle(size);
        in.read(reinterpret_cast<char*>(rle.data()), size);
        if (!in) return std::nullopt;

        Chunk chunk(cm, cm + glm::ivec3(Chunk::SIZE));
        if (!chunk.decodeRLE(rle)) {
            std::cerr << "load: RLE decode failed for " << file << "\n";
            return std::nullopt;
        }

        return chunk;
    }
}