//Этот класс будет сохранять и загружать\стримить из диска в RAM 
//Он загружает только маленькие объекты, инстансе и МЕТУ больших объектов
//Хранятся данные в файлах с названием Чанка для быстро чтения/загрузки ТОЛЬКО определенного чанка
//Данные хранятся в своём формате

//.Region хранит в себе только id объектов для загрузки

//.object хранит в себе данные объектов / разные magic меняют тип объекта

//.instance хранит в себе данные инстансов не только своего но и общих по id

//om и oc - дата больших объектов

//.om хранит в себе общую информацию о больших объектах

//ChunkPersistence - класс за загрузку и сохранение чанков : .oc - покоординатная загрузка чанков

#pragma once
#include "Data.h"
#include "BigObject.h"
#include "SmallObject.h"
#include "MultiObject.h"
#include "ChunkPersistence.h"
#include "helper.h"
#include "ObjectLayer.h"
#include <filesystem>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <iostream>

namespace fs = std::filesystem;

namespace DataPersistence {
    inline constexpr int REGION_SIZE = 512;

    inline constexpr uint32_t MAGIC_SOBJ = 0x4A424F53;
    inline constexpr uint32_t MAGIC_BOBJ = 0x4A424F42;
    inline constexpr uint32_t MAGIC_MOBJ = 0x4A424F4D;
    inline constexpr uint32_t MAGIC_REGN = 0x4E474552;
    inline constexpr uint8_t  VERSION = 1;

    bool save(const Data& data, const fs::path& root = getExeDirectory());
    bool load(Data& data, const fs::path& root);
    ObjectID nextObjectId(const fs::path& root);

    bool saveObject(const Object& obj, const fs::path& root, const std::optional<Bounds>& oldBounds = std::nullopt);
    bool moveObject(Object& obj, glm::ivec3 delta, const fs::path& root);
    bool deleteObject(ObjectID id, const fs::path& root);

    void loadRegion(glm::ivec2 region, Data& data, const fs::path& root);
    void updateRegion(glm::ivec2 region, Data& data, const fs::path& root);
    void unloadRegion(glm::ivec2 region, Data& data, const fs::path& root,
        const std::unordered_set<glm::ivec2>& regionsToKeep);

    inline glm::ivec2 regionOf(glm::ivec3 pos) {
        return { (int)std::floor((float)pos.x / REGION_SIZE),
                 (int)std::floor((float)pos.z / REGION_SIZE) };
    }
}