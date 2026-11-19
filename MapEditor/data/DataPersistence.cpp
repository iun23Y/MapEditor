#include "DataPersistence.h"

#include <limits>

namespace DataPersistence {

    namespace {
        fs::path worldDir(const fs::path& root) {
            return root / "world";
        }
        fs::path objFile(const fs::path& baseDir, ObjectID id) {
            return baseDir / (std::to_string(id) + ".object");
        }
        fs::path chunkDir(const fs::path& baseDir, ObjectID id) {
            return baseDir / std::to_string(id);
        }
        fs::path childrenDir(const fs::path& baseDir, ObjectID parentId) {
            return baseDir / std::to_string(parentId) / "children";
        }
        fs::path regionPath(const fs::path& root, glm::ivec2 r) {
            return root / "world" / "regions" /
                ("r_" + std::to_string(r.x) + "_" + std::to_string(r.y) + ".region");
        }

        bool saveSmallImpl(const SmallObject& so, const fs::path& baseDir);
        bool saveBigImpl(const BigObject& bo, const fs::path& baseDir);
        bool saveBigMetaImpl(const BigObject& bo, const fs::path& baseDir);
        bool saveMultiImpl(const MultiObject& mo, const fs::path& baseDir);
        bool saveBigRegion(BigObject& bo, glm::ivec2 region, const fs::path& baseDir);

        std::unique_ptr<SmallObject> loadSmallImpl(int id, const fs::path& baseDir);
        std::unique_ptr<BigObject>  loadBigImpl(int id, const fs::path& baseDir);
        std::unique_ptr<BigObject>  loadBigMetaImpl(int id, const fs::path& baseDir);
        std::unique_ptr<MultiObject> loadMultiImpl(int id, const fs::path& baseDir);
        void loadBigRegion(BigObject& bo, glm::ivec2 region, const fs::path& baseDir);

        std::vector<uint32_t> readRegion(const fs::path& root, glm::ivec2 r);
        void writeRegion(const fs::path& root, glm::ivec2 r, const std::vector<uint32_t>& ids);
        void rebuildRegions(const Data& data, const fs::path& root);
        void addObjectToRegionBounds(ObjectID id, const Bounds& b, const fs::path& root);
        void removeObjectFromRegionBounds(ObjectID id, const Bounds& b, const fs::path& root);
        void removeObjectFromAllRegions(ObjectID id, const fs::path& root);

        bool saveAny(const Object& obj, const fs::path& baseDir);
        std::unique_ptr<Object> loadAny(int id, const fs::path& baseDir);

        bool saveSmallImpl(const SmallObject& so, const fs::path& baseDir) {
            ObjectID id = so.id();
            fs::path file = objFile(baseDir, id);
            fs::create_directories(file.parent_path());

            std::ofstream out(file, std::ios::binary);
            if (!out) return false;

            out.write((char*)&MAGIC_SOBJ, 4);
            out.write((char*)&VERSION, 1);
            out.write((char*)&id, 4);
            out.write((char*)&so.position, sizeof(glm::ivec3));

            int32_t layer = (int32_t)so.layer();
            int32_t priority = so.priority();
            out.write((char*)&layer, 4);
            out.write((char*)&priority, 4);

            Bounds b = so.bounds();
            out.write((char*)&b.min, sizeof(glm::ivec3));
            out.write((char*)&b.max, sizeof(glm::ivec3));

            const auto& raw = so.data().raw();
            uint32_t count = (uint32_t)raw.size();
            out.write((char*)&count, 4);

            for (auto& [lp, bid] : raw) {
                out.write((char*)&lp, sizeof(glm::ivec3));
                uint16_t b16 = (uint16_t)bid;
                out.write((char*)&b16, 2);
            }
            return out.good();
        }

        bool saveBigMetaImpl(const BigObject& bo, const fs::path& baseDir) {
            ObjectID id = bo.id();
            fs::path file = objFile(baseDir, id);
            fs::create_directories(file.parent_path());

            std::ofstream out(file, std::ios::binary);
            if (!out) return false;

            out.write((char*)&MAGIC_BOBJ, 4);
            out.write((char*)&VERSION, 1);
            out.write((char*)&id, 4);
            out.write((char*)&bo.position, sizeof(glm::ivec3));

            int32_t layer = (int32_t)bo.layer();
            int32_t priority = bo.priority();
            out.write((char*)&layer, 4);
            out.write((char*)&priority, 4);

            Bounds b = bo.bounds();
            out.write((char*)&b.min, sizeof(glm::ivec3));
            out.write((char*)&b.max, sizeof(glm::ivec3));

            const auto& table = bo.getChunkTable();
            uint32_t count = (uint32_t)table.size();
            out.write((char*)&count, 4);
            for (auto& e : table) {
                out.write((char*)&e.pos, sizeof(glm::ivec3));
                out.write((char*)&e.chunkId, 4);
            }
            return out.good();
        }

        bool saveBigImpl(const BigObject& bo, const fs::path& baseDir) {
            ObjectID id = bo.id();
            fs::path chunkRoot = chunkDir(baseDir, id);
            fs::create_directories(chunkRoot);

            for (auto& e : bo.getChunkTable()) {
                const Chunk* chunk = bo.data().tryGetChunk(e.pos);
                if (!chunk || chunk->isEmpty()) continue;
                ChunkPersistenceMeta meta{ (int)id, (int)e.chunkId, e.pos };
                ChunkPersistence::saveChunk(meta, *chunk, chunkRoot);
            }
            return saveBigMetaImpl(bo, baseDir);
        }

        bool saveMultiImpl(const MultiObject& mo, const fs::path& baseDir) {
            ObjectID id = mo.id();
            fs::path file = objFile(baseDir, id);
            fs::create_directories(file.parent_path());

            std::ofstream out(file, std::ios::binary);
            if (!out) return false;

            out.write((char*)&MAGIC_MOBJ, 4);
            out.write((char*)&VERSION, 1);
            out.write((char*)&id, 4);
            out.write((char*)&mo.position, sizeof(glm::ivec3));

            int32_t layer = (int32_t)mo.layer();
            int32_t priority = mo.priority();
            out.write((char*)&layer, 4);
            out.write((char*)&priority, 4);

            Bounds b = mo.bounds();
            out.write((char*)&b.min, sizeof(glm::ivec3));
            out.write((char*)&b.max, sizeof(glm::ivec3));

            const auto& ch = mo.children();
            uint32_t count = (uint32_t)ch.size();
            out.write((char*)&count, 4);
            for (auto& c : ch) {
                ObjectID cid = c->id();
                out.write((char*)&cid, 4);
            }
            out.close();

            fs::path chDir = childrenDir(baseDir, id);
            fs::create_directories(chDir);

            for (auto& c : ch) {
                if (!saveAny(*c, chDir)) return false;
            }
            return true;
        }

        bool saveBigRegion(BigObject& bo, glm::ivec2 region, const fs::path& baseDir) {
            ObjectID id = bo.id();
            fs::path chunkRoot = chunkDir(baseDir, id);

            const int rMinX = region.x * REGION_SIZE, rMaxX = rMinX + REGION_SIZE;
            const int rMinZ = region.y * REGION_SIZE, rMaxZ = rMinZ + REGION_SIZE;

            bool anySaved = false;
            for (auto& e : bo.getChunkTable()) {
                const int cMinX = bo.position.x + e.pos.x, cMaxX = cMinX + Chunk::SIZE;
                const int cMinZ = bo.position.z + e.pos.z, cMaxZ = cMinZ + Chunk::SIZE;
                if (cMinX >= rMaxX || cMaxX <= rMinX) continue;
                if (cMinZ >= rMaxZ || cMaxZ <= rMinZ) continue;

                Chunk* chunk = const_cast<Chunk*>(bo.data().tryGetChunk(e.pos));
                if (!chunk || !chunk->isDirty()) continue;

                ChunkPersistenceMeta meta{ (int)id, (int)e.chunkId, e.pos };
                if (ChunkPersistence::saveChunk(meta, *chunk, chunkRoot)) {
                    chunk->markClean();
                    anySaved = true;
                }
            }

            if (anySaved || bo.isMetaDirty()) {
                if (!saveBigMetaImpl(bo, baseDir)) return false;
                bo.markMetaClean();
            }
            return true;
        }

        std::unique_ptr<SmallObject> loadSmallImpl(int id, const fs::path& baseDir) {
            fs::path file = objFile(baseDir, id);
            std::ifstream in(file, std::ios::binary);
            if (!in) return nullptr;

            uint32_t magic = 0; uint8_t ver = 0;
            in.read((char*)&magic, 4); if (magic != MAGIC_SOBJ) return nullptr;
            in.read((char*)&ver, 1);   if (ver != VERSION) return nullptr;

            ObjectID fileId = 0;
            in.read((char*)&fileId, 4);
            if ((int)fileId != id) return nullptr;

            glm::ivec3 pos{ 0 };
            in.read((char*)&pos, sizeof(glm::ivec3));

            int32_t layer = 0, priority = 0;
            in.read((char*)&layer, 4);
            in.read((char*)&priority, 4);

            Bounds b;
            in.read((char*)&b.min, sizeof(glm::ivec3));
            in.read((char*)&b.max, sizeof(glm::ivec3));
            if (!in) return nullptr;

            auto obj = std::make_unique<SmallObject>();
            obj->setId(fileId);
            obj->position = pos;
            obj->setLayer((ObjectLayer)layer);
            obj->setPriority(priority);

            uint32_t count = 0;
            in.read((char*)&count, 4);
            if (!in) return nullptr;

            for (uint32_t i = 0; i < count; ++i) {
                glm::ivec3 lp{ 0 }; uint16_t b16 = 0;
                in.read((char*)&lp, sizeof(glm::ivec3));
                in.read((char*)&b16, 2);
                if (!in) break;
                obj->data().setBlock(lp, b16);
            }
            return obj;
        }

        std::unique_ptr<BigObject> loadBigMetaImpl(int id, const fs::path& baseDir) {
            fs::path file = objFile(baseDir, id);
            std::ifstream in(file, std::ios::binary);
            if (!in) return nullptr;

            uint32_t magic = 0; uint8_t ver = 0;
            in.read((char*)&magic, 4); if (magic != MAGIC_BOBJ) return nullptr;
            in.read((char*)&ver, 1);   if (ver != VERSION) return nullptr;

            ObjectID fileId = 0;
            in.read((char*)&fileId, 4);
            if ((int)fileId != id) return nullptr;

            glm::ivec3 pos{ 0 };
            in.read((char*)&pos, sizeof(glm::ivec3));

            int32_t layer = 0, priority = 0;
            in.read((char*)&layer, 4);
            in.read((char*)&priority, 4);

            Bounds b;
            in.read((char*)&b.min, sizeof(glm::ivec3));
            in.read((char*)&b.max, sizeof(glm::ivec3));

            uint32_t chunkCount = 0;
            in.read((char*)&chunkCount, 4);
            if (!in) return nullptr;

            std::vector<ChunkIndexEntry> table;
            table.reserve(chunkCount);
            for (uint32_t i = 0; i < chunkCount; ++i) {
                glm::ivec3 cpos{ 0 }; uint32_t chunkId = 0;
                in.read((char*)&cpos, sizeof(glm::ivec3));
                in.read((char*)&chunkId, 4);
                if (!in) break;
                table.push_back({ cpos, chunkId });
            }

            auto obj = std::make_unique<BigObject>();
            obj->setId(fileId);
            obj->position = pos;
            obj->setLayer((ObjectLayer)layer);
            obj->setPriority(priority);
            obj->setChunkTable(std::move(table));
            return obj;
        }

        std::unique_ptr<BigObject> loadBigImpl(int id, const fs::path& baseDir) {
            auto obj = loadBigMetaImpl(id, baseDir);
            if (!obj) return nullptr;

            fs::path chunkRoot = chunkDir(baseDir, id);
            for (auto& e : obj->getChunkTable()) {
                ChunkPersistenceMeta meta{ id, (int)e.chunkId, e.pos };
                auto chunk = ChunkPersistence::load(meta, chunkRoot);
                if (!chunk) continue;
                chunk->markClean();
                obj->addLoadedChunk(std::move(*chunk));
            }
            return obj;
        }

        std::unique_ptr<MultiObject> loadMultiImpl(int id, const fs::path& baseDir) {
            fs::path file = objFile(baseDir, id);
            std::ifstream in(file, std::ios::binary);
            if (!in) return nullptr;

            uint32_t magic = 0; uint8_t ver = 0;
            in.read((char*)&magic, 4); if (magic != MAGIC_MOBJ) return nullptr;
            in.read((char*)&ver, 1);   if (ver != VERSION) return nullptr;

            ObjectID fileId = 0;
            in.read((char*)&fileId, 4);
            if ((int)fileId != id) return nullptr;

            glm::ivec3 pos{ 0 };
            in.read((char*)&pos, sizeof(glm::ivec3));

            int32_t layer = 0, priority = 0;
            in.read((char*)&layer, 4);
            in.read((char*)&priority, 4);

            Bounds b;
            in.read((char*)&b.min, sizeof(glm::ivec3));
            in.read((char*)&b.max, sizeof(glm::ivec3));

            uint32_t count = 0;
            in.read((char*)&count, 4);
            std::vector<ObjectID> childIds(count);
            for (uint32_t i = 0; i < count; ++i)
                in.read((char*)&childIds[i], 4);
            in.close();

            auto mo = std::make_unique<MultiObject>();
            mo->setId(fileId);
            mo->position = pos;
            mo->setLayer((ObjectLayer)layer);
            mo->setPriority(priority);

            fs::path chDir = childrenDir(baseDir, id);
            for (auto cid : childIds) {
                if (auto c = loadAny((int)cid, chDir))
                    mo->addChild(std::move(c));
            }
            mo->markMetaClean();
            return mo;
        }

        void loadBigRegion(BigObject& bo, glm::ivec2 region, const fs::path& baseDir) {
            ObjectID id = bo.id();
            fs::path chunkRoot = chunkDir(baseDir, id);

            const int rMinX = region.x * REGION_SIZE, rMaxX = rMinX + REGION_SIZE;
            const int rMinZ = region.y * REGION_SIZE, rMaxZ = rMinZ + REGION_SIZE;

            for (auto& e : bo.getChunkTable()) {
                if (bo.isChunkLoaded(e.pos)) continue;

                const int cMinX = bo.position.x + e.pos.x, cMaxX = cMinX + Chunk::SIZE;
                const int cMinZ = bo.position.z + e.pos.z, cMaxZ = cMinZ + Chunk::SIZE;
                if (cMinX >= rMaxX || cMaxX <= rMinX) continue;
                if (cMinZ >= rMaxZ || cMaxZ <= rMinZ) continue;

                ChunkPersistenceMeta meta{ (int)id, (int)e.chunkId, e.pos };
                auto chunk = ChunkPersistence::load(meta, chunkRoot);
                if (!chunk) continue;
                chunk->markClean();
                bo.addLoadedChunk(std::move(*chunk));
            }
        }

        bool saveAny(const Object& obj, const fs::path& baseDir) {
            if (obj.isType(ObjectType::SmallObject))
                return saveSmallImpl(static_cast<const SmallObject&>(obj), baseDir);
            if (obj.isType(ObjectType::BigObject))
                return saveBigImpl(static_cast<const BigObject&>(obj), baseDir);
            if (obj.isType(ObjectType::MultiObject))
                return saveMultiImpl(static_cast<const MultiObject&>(obj), baseDir);
            return false;
        }

        std::unique_ptr<Object> loadAny(int id, const fs::path& baseDir) {
            fs::path file = objFile(baseDir, id);
            std::ifstream in(file, std::ios::binary);
            if (!in) return nullptr;
            uint32_t magic = 0;
            in.read((char*)&magic, 4);
            in.close();

            if (magic == MAGIC_SOBJ) return loadSmallImpl(id, baseDir);
            if (magic == MAGIC_BOBJ) return loadBigImpl(id, baseDir);
            if (magic == MAGIC_MOBJ) return loadMultiImpl(id, baseDir);
            return nullptr;
        }

        std::vector<uint32_t> readRegion(const fs::path& root, glm::ivec2 r) {
            fs::path file = regionPath(root, r);
            std::ifstream in(file, std::ios::binary);
            if (!in) return {};

            uint32_t magic = 0; uint8_t ver = 0;
            in.read((char*)&magic, 4); if (magic != MAGIC_REGN) return {};
            in.read((char*)&ver, 1);   if (ver != VERSION) return {};

            int32_t rx = 0, rz = 0;
            in.read((char*)&rx, 4);
            in.read((char*)&rz, 4);
            if (rx != r.x || rz != r.y) return {};

            uint32_t count = 0;
            in.read((char*)&count, 4);
            if (!in || count > 100000) return {};

            std::vector<uint32_t> ids(count);
            if (count > 0) in.read((char*)ids.data(), count * 4);
            return ids;
        }

        void writeRegion(const fs::path& root, glm::ivec2 r, const std::vector<uint32_t>& ids) {
            fs::path file = regionPath(root, r);
            fs::create_directories(file.parent_path());

            std::ofstream out(file, std::ios::binary);
            if (!out) return;

            int32_t rx = r.x, rz = r.y;
            uint32_t count = (uint32_t)ids.size();
            out.write((char*)&MAGIC_REGN, 4);
            out.write((char*)&VERSION, 1);
            out.write((char*)&rx, 4);
            out.write((char*)&rz, 4);
            out.write((char*)&count, 4);
            if (count > 0) out.write((char*)ids.data(), count * 4);
        }

        void addObjectToRegionBounds(ObjectID id, const Bounds& b, const fs::path& root) {
            if (b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z) return;

            glm::ivec2 rmin = regionOf(b.min);
            glm::ivec2 rmax = regionOf(b.max);

            for (int rx = rmin.x; rx <= rmax.x; ++rx)
                for (int rz = rmin.y; rz <= rmax.y; ++rz) {
                    auto ids = readRegion(root, { rx, rz });
                    if (std::find(ids.begin(), ids.end(), (uint32_t)id) != ids.end())
                        continue;
                    ids.push_back((uint32_t)id);
                    writeRegion(root, { rx, rz }, ids);
                }
        }

        void removeObjectFromRegionBounds(ObjectID id, const Bounds& b, const fs::path& root) {
            if (b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z) return;

            glm::ivec2 rmin = regionOf(b.min);
            glm::ivec2 rmax = regionOf(b.max);

            for (int rx = rmin.x; rx <= rmax.x; ++rx)
                for (int rz = rmin.y; rz <= rmax.y; ++rz) {
                    auto ids = readRegion(root, { rx, rz });
                    auto it = std::find(ids.begin(), ids.end(), (uint32_t)id);
                    if (it == ids.end()) continue;
                    ids.erase(it);
                    writeRegion(root, { rx, rz }, ids);
                }
        }

        void removeObjectFromAllRegions(ObjectID id, const fs::path& root) {
            fs::path regDir = root / "world" / "regions";
            if (!fs::exists(regDir)) return;
            for (auto& e : fs::directory_iterator(regDir)) {
                if (e.path().extension() != ".region") continue;
                int rx, rz;
                auto stem = e.path().stem().string();
                if (stem.rfind("r_", 0) != 0) continue;
                const size_t separator = stem.find('_', 2);
                if (separator == std::string::npos) continue;
                try {
                    rx = std::stoi(stem.substr(2, separator - 2));
                    rz = std::stoi(stem.substr(separator + 1));
                } catch (const std::exception&) {
                    continue;
                }

                auto ids = readRegion(root, { rx, rz });
                auto it = std::find(ids.begin(), ids.end(), (uint32_t)id);
                if (it == ids.end()) continue;
                ids.erase(it);
                writeRegion(root, { rx, rz }, ids);
            }
        }

        void rebuildRegions(const Data& data, const fs::path& root) {
            fs::path regDir = root / "world" / "regions";
            fs::create_directories(regDir);
            for (auto& e : fs::directory_iterator(regDir))
                if (e.path().extension() == ".region")
                    fs::remove(e.path());

            std::unordered_map<int64_t, std::vector<uint32_t>> regions;
            auto pack = [](glm::ivec2 r) -> int64_t {
                return ((int64_t)r.x << 32) | (uint32_t)r.y;
                };

            for (auto& [id, obj] : data.renderObjects()) {
                Bounds b = obj->bounds();
                if (b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z)
                    continue;
                glm::ivec2 rmin = regionOf(b.min), rmax = regionOf(b.max);
                for (int rx = rmin.x; rx <= rmax.x; ++rx)
                    for (int rz = rmin.y; rz <= rmax.y; ++rz)
                        regions[pack({ rx, rz })].push_back((uint32_t)id);
            }

            for (auto& [k, ids] : regions) {
                glm::ivec2 r{ (int32_t)(k >> 32), (int32_t)(k & 0xFFFFFFFF) };
                writeRegion(root, r, ids);
            }
        }
    }

    bool save(const Data& data, const fs::path& root) {
        fs::path baseDir = worldDir(root);
        fs::create_directories(baseDir);

        for (auto& [id, obj] : data.renderObjects())
            if (!saveAny(*obj, baseDir)) return false;

        rebuildRegions(data, root);
        return true;
    }

    bool load(Data& data, const fs::path& root) {
        fs::path baseDir = worldDir(root);
        if (!fs::exists(baseDir)) return false;

        for (auto& entry : fs::directory_iterator(baseDir)) {
            if (entry.path().extension() != ".object") continue;
            if (!entry.is_regular_file()) continue;
            int id = 0;
            try { id = std::stoi(entry.path().stem().string()); }
            catch (...) { continue; }
            if (auto obj = loadAny(id, baseDir))
                data.addLoaded(std::move(obj), ObjectRenderType::Renderer);
        }
        return true;
    }

    ObjectID nextObjectId(const fs::path& root) {
        const fs::path baseDir = worldDir(root);
        if (!fs::exists(baseDir)) return 1;

        uint32_t maxId = 0;
        for (const auto& entry : fs::recursive_directory_iterator(baseDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".object")
                continue;

            const std::string stem = entry.path().stem().string();
            size_t parsed = 0;
            try {
                const unsigned long long id = std::stoull(stem, &parsed);
                if (parsed != stem.size() || id > std::numeric_limits<uint32_t>::max())
                    continue;
                maxId = std::max(maxId, static_cast<uint32_t>(id));
            }
            catch (const std::exception&) {
                continue;
            }
        }

        if (maxId == std::numeric_limits<uint32_t>::max())
            throw std::overflow_error("Object ID limit reached");
        return std::max<uint32_t>(1, maxId + 1);
    }

    bool saveObject(const Object& obj, const fs::path& root, const std::optional<Bounds>& oldBounds) {
        fs::path baseDir = worldDir(root);
        fs::create_directories(baseDir);

        if (!saveAny(obj, baseDir)) return false;

        if (oldBounds && oldBounds->min.x <= oldBounds->max.x)
            removeObjectFromRegionBounds(obj.id(), *oldBounds, root);

        addObjectToRegionBounds(obj.id(), obj.bounds(), root);
        return true;
    }

    bool moveObject(Object& obj, glm::ivec3 delta, const fs::path& root) {
        Bounds oldB = obj.bounds();

        if (obj.isType(ObjectType::MultiObject))
            static_cast<MultiObject&>(obj).move(delta);
        else
            obj.position += delta;

        return saveObject(obj, root, oldB);
    }

    bool deleteObject(ObjectID id, const fs::path& root) {
        fs::path baseDir = worldDir(root);
        fs::path file = objFile(baseDir, id);

        std::ifstream in(file, std::ios::binary);
        if (in) {
            uint32_t magic = 0;
            in.read((char*)&magic, 4);
            if (magic == MAGIC_MOBJ) {
                in.seekg(4 + 1 + 4 + 12 + 4 + 4 + 24, std::ios::cur);
                uint32_t count = 0;
                in.read((char*)&count, 4);
                std::vector<ObjectID> cids(count);
                for (uint32_t i = 0; i < count; ++i)
                    in.read((char*)&cids[i], 4);
                in.close();

                fs::path chDir = childrenDir(baseDir, id);
                for (auto cid : cids) {
                    fs::remove(objFile(chDir, cid));
                    fs::remove_all(chunkDir(chDir, cid));
                }
                fs::remove_all(baseDir / std::to_string(id));
                fs::remove(file);
                removeObjectFromAllRegions(id, root);
                return true;
            }
            in.close();
        }

        fs::remove(file);
        fs::remove_all(chunkDir(baseDir, id));
        removeObjectFromAllRegions(id, root);
        return true;
    }

    void loadRegion(glm::ivec2 region, Data& data, const fs::path& root) {
        auto ids = readRegion(root, region);
        fs::path baseDir = worldDir(root);

        for (uint32_t id : ids) {
            auto* existing = data.get((ObjectID)id);
            if (existing) {
                if (existing->isType(ObjectType::BigObject))
                    loadBigRegion(*static_cast<BigObject*>(existing), region, baseDir);
                continue;
            }

            fs::path file = objFile(baseDir, id);
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            uint32_t magic = 0;
            in.read((char*)&magic, 4);
            in.close();

            if (magic == MAGIC_BOBJ) {
                auto meta = loadBigMetaImpl((int)id, baseDir);
                if (!meta) continue;
                loadBigRegion(*meta, region, baseDir);
                data.addLoaded(std::move(meta), ObjectRenderType::Renderer);
            }
            else if (magic == MAGIC_SOBJ) {
                if (auto obj = loadSmallImpl((int)id, baseDir))
                    data.addLoaded(std::move(obj), ObjectRenderType::Renderer);
            }
            else if (magic == MAGIC_MOBJ) {
                if (auto obj = loadMultiImpl((int)id, baseDir))
                    data.addLoaded(std::move(obj), ObjectRenderType::Renderer);
            }
        }
    }

    void updateRegion(glm::ivec2 region, Data& data, const fs::path& root) {
        auto ids = readRegion(root, region);
        fs::path baseDir = worldDir(root);

        for (uint32_t id : ids) {
            auto* obj = data.get((ObjectID)id);
            if (!obj) continue;

            if (obj->isType(ObjectType::BigObject)) {
                saveBigRegion(*static_cast<BigObject*>(obj), region, baseDir);
            }
            else if (obj->isType(ObjectType::SmallObject)) {
                saveSmallImpl(*static_cast<SmallObject*>(obj), baseDir);
            }
            else if (obj->isType(ObjectType::MultiObject)) {
                saveMultiImpl(*static_cast<MultiObject*>(obj), baseDir);
            }
        }
    }

    void unloadRegion(glm::ivec2 region, Data& data, const fs::path& root,
        const std::unordered_set<glm::ivec2>& regionsToKeep) {
        const int rMinX = region.x * REGION_SIZE, rMaxX = rMinX + REGION_SIZE;
        const int rMinZ = region.y * REGION_SIZE, rMaxZ = rMinZ + REGION_SIZE;

        auto ids = readRegion(root, region);
        std::unordered_set<uint32_t> idsToKeep;
        for (const glm::ivec2 keepRegion : regionsToKeep) {
            const auto keepIds = readRegion(root, keepRegion);
            idsToKeep.insert(keepIds.begin(), keepIds.end());
        }

        for (uint32_t rawId : ids) {
            auto* obj = data.get((ObjectID)rawId);
            if (!obj) continue;

            if (obj->isType(ObjectType::BigObject)) {
                auto& bo = *static_cast<BigObject*>(obj);
                std::vector<glm::ivec3> toRemove;
                for (const auto& [cc, chunk] : bo.data().getChunks()) {
                    (void)cc;
                    const glm::ivec3 chunkMin = chunk.getMinPos();
                    const int cMinX = bo.position.x + chunkMin.x;
                    const int cMaxX = cMinX + Chunk::SIZE;
                    const int cMinZ = bo.position.z + chunkMin.z;
                    const int cMaxZ = cMinZ + Chunk::SIZE;
                    if (cMinX >= rMaxX || cMaxX <= rMinX) continue;
                    if (cMinZ >= rMaxZ || cMaxZ <= rMinZ) continue;

                    const bool neededByKeptRegion = std::any_of(
                        regionsToKeep.begin(), regionsToKeep.end(),
                        [&](glm::ivec2 keepRegion) {
                            const int keepMinX = keepRegion.x * REGION_SIZE;
                            const int keepMaxX = keepMinX + REGION_SIZE;
                            const int keepMinZ = keepRegion.y * REGION_SIZE;
                            const int keepMaxZ = keepMinZ + REGION_SIZE;
                            return cMinX < keepMaxX && cMaxX > keepMinX &&
                                cMinZ < keepMaxZ && cMaxZ > keepMinZ;
                        });
                    if (!neededByKeptRegion)
                        toRemove.push_back(chunkMin);
                }
                for (const auto& chunkMin : toRemove)
                    bo.data().removeChunk(chunkMin);
            }

            if (!idsToKeep.contains(rawId))
                data.remove((ObjectID)rawId);
        }
    }
}