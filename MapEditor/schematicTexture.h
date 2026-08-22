#pragma once

#include "helper.h"
#include "schematic.h"

#include <SFML/Graphics.hpp>
#include <unordered_map>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>

using namespace nlohmann;

class schematicTexture {
private:
	std::unordered_map<std::string, sf::Color> blockColors;
	std::unordered_map<std::string, sf::Texture> blockTextures;
    std::unordered_map<std::string, sf::Image> blockImages;
	std::string pathBlockTextures;
    std::string pathBlockJson;
	std::string pathName;
	SchematicMap* schematic;

	int width;
	int length;
	const int regionSize = 128;

    std::unordered_map<std::pair<int, int>, sf::Texture, PairHash> regionCache;
	const int cacheAddSize = 3;
	const int maxSize = 15;
    sf::Vector2i cachedMinRegion = { 0, 0 };
    sf::Vector2i cachedMaxRegion = { 0, 0 };
    bool cacheInitialized = false;

    void loadBlockData() {

        std::ifstream file(pathBlockJson);
        if (!file.is_open()) return;
        json data;
        try { file >> data; }
        catch (const json::parse_error&) {
            return;
        }

        for (auto& [blockName, obj] : data.items()) {
            sf::Color color = sf::Color::Magenta;
            if (obj.contains("color") && obj["color"].is_string()) {
                std::string colorStr = obj["color"].get<std::string>();
                if (colorStr.size() == 7 && colorStr[0] == '#') {
                    int r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
                    int g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
                    int b = std::stoi(colorStr.substr(5, 2), nullptr, 16);
                    color = sf::Color(r, g, b);
                }
            }
            blockColors[blockName] = color;

            sf::Texture texture;
            bool loaded = false;

            if (obj.contains("texture") && obj["texture"].is_string()) {
                std::string texRelPath = obj["texture"].get<std::string>();
                std::string fullPath = pathBlockTextures + "/" + texRelPath;
                if (std::filesystem::exists(fullPath)) {
                    loaded = texture.loadFromFile(fullPath);
                }
            }
            if (!loaded) {
                sf::Image img({ 16, 16 }, color);
                loaded = texture.loadFromImage(img);
            }
            if (loaded) {
                blockTextures.emplace(blockName, std::move(texture));
            }
   
            auto it = blockTextures.find(blockName);
            if (it != blockTextures.end()) {
                sf::Image img = it->second.copyToImage();
                blockImages[blockName] = std::move(img);
            } else {
                sf::Image img({ 16, 16 }, blockColors[blockName]);
                blockImages[blockName] = std::move(img);
            }
        }
    }

    bool isRegionInsideSchematic(int rx, int rz) {
        sf::Vector3i pos1 = schematic->getPos1();
        sf::Vector3i pos2 = schematic->getPos2();
        int regionMinX = rx;
        int regionMaxX = rx + regionSize;
        int regionMinZ = rz;
        int regionMaxZ = rz + regionSize;
        return (regionMinX < pos2.x && regionMaxX > pos1.x && regionMinZ < pos2.z && regionMaxZ > pos1.z);
    }

    void createTextureRegion(int rx, int rz) {
        if (!isRegionInsideSchematic(rx, rz)) return;
        sf::Image regionImage({ static_cast<unsigned int>(regionSize * 16), static_cast<unsigned int>(regionSize * 16) }, sf::Color(0, 0, 0, 0));

        std::vector<SchematicMap::RegionBlock> blocks = schematic->getTopBlocksInArea(rx, rz, rx + regionSize, rz + regionSize);

        for (auto& b : blocks) {
            int lx = b.x - rx;
            int lz = b.z - rz;

            std::string blockName = schematic->getPalette().getName(b.blockId);
            auto img = blockImages.find(blockName);
            if (img != blockImages.end()) {
                static_cast<void>(regionImage.copy(img->second, { unsigned(lx * 16), unsigned(lz * 16) }, sf::IntRect({ 0, 0 }, { 16, 16 }), true));
            }
            else {
                std::cerr << "Warning: Block texture not found for block '" << blockName << "\n";
            }
        }
        std::string regionFileName = std::to_string(rx) + "_" + std::to_string(rz) + ".png";
        std::string regionFilePath = pathName + "/" + regionFileName;
        static_cast<void>(regionImage.saveToFile(regionFilePath));
    }

    void removeOutdatedRegions(int minRx, int minRz, int maxRx, int maxRz) {
        std::vector<std::pair<int, int>> toRemove;
        for (const auto& [key, _] : regionCache) {
            int rx = key.first;
            int rz = key.second;
            if (rx < minRx || rx >= maxRx || rz < minRz || rz >= maxRz) {
                toRemove.push_back(key);
            }
        }
        for (const auto& key : toRemove) {
            regionCache.erase(key);
        }
    }
    bool isRegionCached(int rx, int rz) const {
        return regionCache.find({ rx, rz }) != regionCache.end();
    }
    void loadRegion(int rx, int rz) {
        std::string regionFileName = std::to_string(rx) + "_" + std::to_string(rz) + ".png";
        std::string regionFilePath = pathName + "/" + regionFileName;
        sf::Texture texture;

        if (!std::filesystem::exists(regionFilePath)) {
            createTextureRegion(rx, rz);
        }

        if (texture.loadFromFile(regionFilePath)) {
            regionCache[{rx, rz}] = std::move(texture);
        }
        else {
            createTextureRegion(rx, rz);
            if (texture.loadFromFile(regionFilePath)) {
                regionCache[{rx, rz}] = std::move(texture);
            }
            else {
                std::cerr << "Error: Failed to load-generate region " << rx << "," << rz << "\n";
            }
        }
    }
    void updateCache(sf::View view) {
        sf::Vector2f center = view.getCenter();
        sf::Vector2f size = view.getSize();
        float left = center.x - size.x / 2.f;
        float top = center.y - size.y / 2.f;
        float right = center.x + size.x / 2.f;
        float bottom = center.y + size.y / 2.f;

        int minRx = static_cast<int>(std::floor(left / regionSize)) * regionSize;
        int minRz = static_cast<int>(std::floor(top / regionSize)) * regionSize;
        int maxRx = static_cast<int>(std::ceil(right / regionSize)) * regionSize;
        int maxRz = static_cast<int>(std::ceil(bottom / regionSize)) * regionSize;

        minRx -= cacheAddSize * regionSize;
        minRz -= cacheAddSize * regionSize;
        maxRx += cacheAddSize * regionSize;
        maxRz += cacheAddSize * regionSize;

        int centerRx = static_cast<int>(std::round(center.x / regionSize)) * regionSize;
        int centerRz = static_cast<int>(std::round(center.y / regionSize)) * regionSize;
        minRx = std::max(minRx, centerRx - maxSize * regionSize);
        minRz = std::max(minRz, centerRz - maxSize * regionSize);
        maxRx = std::min(maxRx, centerRx + maxSize * regionSize);
        maxRz = std::min(maxRz, centerRz + maxSize * regionSize);

        if (cacheInitialized &&
            minRx == cachedMinRegion.x && maxRx == cachedMaxRegion.x &&
            minRz == cachedMinRegion.y && maxRz == cachedMaxRegion.y) {
            return;
        }

        for (int rx = minRx; rx < maxRx; rx += regionSize) {
            for (int rz = minRz; rz < maxRz; rz += regionSize) {
                if (!isRegionCached(rx, rz)) {
                    if (!isRegionInsideSchematic(rx, rz)) continue;
                    loadRegion(rx, rz);
                }
            }
        }

        removeOutdatedRegions(minRx, minRz, maxRx, maxRz);

        cachedMinRegion.x = minRx;
        cachedMaxRegion.x = maxRx;
        cachedMinRegion.y = minRz;
        cachedMaxRegion.y = maxRz;
        cacheInitialized = true;
    }

public:
	schematicTexture(SchematicMap* schematic) : schematic(schematic) {
        pathName = getExeDirectory() + "World\\Textures";
        pathBlockJson = getExeDirectory() + "Resources\\blockTextures.json";
        pathBlockTextures = getExeDirectory() + "Resources\\Textures";

        if (!std::filesystem::exists(pathName))
            std::filesystem::create_directories(pathName);

		width = schematic->getPos2().x - schematic->getPos1().x;
		length = schematic->getPos2().z - schematic->getPos1().z;
        loadBlockData();
	}
    void draw(sf::RenderTarget& target, sf::RenderStates states) {
        sf::View view = target.getView();
        updateCache(view);

        for (const auto& [key, texture] : regionCache) {
            sf::Sprite sprite(texture);
            sprite.setPosition({ static_cast<float>(key.first), static_cast<float>(key.second) });
            sprite.setScale({ 1.f / 16, 1.f / 16 });
            target.draw(sprite, states);
        }
    }
    void updateRegion(sf::Vector2i pos) {
        sf::Vector2i regionPos = { static_cast<int>(std::floor(pos.x / regionSize) * regionSize), static_cast<int>(std::floor(pos.y / regionSize) * regionSize) };
        createTextureRegion(regionPos.x, regionPos.y);
		loadRegion(regionPos.x, regionPos.y);
    }
};