#include "textureManager.h"
#include "helper.h"
#include <fstream>
#include <nlohmann/json.hpp>
using namespace nlohmann;

textureManager::textureManager() {
    pathJson = getExeDirectory() + "Resources\\blocks.json";
    pathTextures = getExeDirectory() + "Resources\\Textures";
    loadData();
}
void textureManager::buildAtlas(const BlockPalette& palette) {
    sf::Image atlas({ ATLAS_W, ATLAS_H }, sf::Color::Transparent);

    sf::Image missing({ 16, 16 }, sf::Color::Magenta);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
            if (((x / 8) ^ (y / 8)) & 1)
                missing.setPixel({ x, y }, sf::Color::Black);
    atlas.copy(missing, { TILE, 0 }, sf::IntRect({ 0, 0 }, { 16, 16 }), true);

    int maxId = 0;
    for (auto& [id, _] : palette.nameById) maxId = std::max(maxId, id);
    idToSlot.assign(maxId + 1, 1);

    uint16_t next = 2;
    for (auto& [id, name] : palette.nameById) {
        std::string stripped = stripBlockStates(name);

        if (stripped == "minecraft:air" || stripped == "air" ||
            stripped == "minecraft:__reserved__" || stripped == "__reserved__") {
            idToSlot[id] = 0;
            continue;
        }
        if (next >= COLS * ROWS) break;

        auto it = images.find(stripped);
        if (it == images.end()) {
            idToSlot[id] = 1;
            continue;
        }

        const unsigned col = next % COLS;
        const unsigned row = next / COLS;
        atlas.copy(it->second,
            { col * TILE, row * TILE },
            sf::IntRect({ 0, 0 }, { 16, 16 }), true);

        idToSlot[id] = next;
        ++next;
    }

    atlasTexture.loadFromImage(atlas);
    atlasTexture.setSmooth(false);
    atlasTexture.setRepeated(false);
}

bool textureManager::loadData() {
    std::ifstream file(pathJson);
    if (!file.is_open()) return false;
    json data;
    try { file >> data; }
    catch (const json::parse_error&) { return false; }

    for (auto& [blockName, obj] : data.items()) {
        sf::Color color = sf::Color::Magenta;
        bool hasColor = false;
        if (obj.contains("color") && obj["color"].is_string()) {
            std::string colorStr = obj["color"].get<std::string>();
            if (colorStr.size() == 7 && colorStr[0] == '#') {
                int r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
                int g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
                int b = std::stoi(colorStr.substr(5, 2), nullptr, 16);
                color = sf::Color(r, g, b);
                hasColor = true;
            }
        }

        sf::Texture texture;
        bool loaded = false;

        if (obj.contains("texture") && obj["texture"].is_string()) {
            std::string texRelPath = obj["texture"].get<std::string>();
            std::string fullPath = pathTextures + "/" + texRelPath;
            if (std::filesystem::exists(fullPath)) {
                loaded = texture.loadFromFile(fullPath);
            }
        }
        if (!loaded) {
            sf::Image img({ 16, 16 }, color);
            loaded = texture.loadFromImage(img);
        }
        if (loaded && !hasColor) {
            sf::Image img = texture.copyToImage();
            sf::Vector2u size = img.getSize();
            uint64_t r = 0, g = 0, b = 0, n = 0;
            for (unsigned y = 0; y < size.y; ++y) {
                for (unsigned x = 0; x < size.x; ++x) {
                    sf::Color px = img.getPixel({ x, y });
                    if (px.a < 128) continue;
                    r += px.r; g += px.g; b += px.b; ++n;
                }
            }
            if (n > 0) {
                color = sf::Color(uint8_t(r / n), uint8_t(g / n), uint8_t(b / n));
            }
        }

        colors[blockName] = color;

        if (loaded) {
            textures.emplace(blockName, std::move(texture));
        }
        auto it = textures.find(blockName);
        if (it != textures.end()) {
            images[blockName] = it->second.copyToImage();
        }
        else {
            images[blockName] = sf::Image({ 16, 16 }, colors[blockName]);
        }

        if (obj.contains("type") && obj["type"].is_string()) {
            blockTypes[blockName] = obj["type"].get<std::string>();
        }
        else {
            blockTypes[blockName] = "cube";
        }
    }
    return true;
}

uint16_t textureManager::getAtlasSlot(int blockId) const {
    if (blockId < 0 || blockId >= (int)idToSlot.size()) return 1;
    return idToSlot[blockId];
}
std::string textureManager::getBlockType(const std::string& blockName) const {
    auto it = blockTypes.find(blockName);
    return it == blockTypes.end() ? "cube" : it->second;
}
const sf::Texture* textureManager::getTexture(const std::string& blockName) const {
    auto it = textures.find(blockName);
    if (it == textures.end())
        return nullptr;
    return &it->second;
}
const sf::Image* textureManager::getImage(const std::string& blockName) const {
    auto it = images.find(blockName);
    if (it == images.end())
        return nullptr;
    return &it->second;
}
sf::Color textureManager::getColor(const std::string& blockName) const {
    auto it = colors.find(blockName);
    if (it == colors.end())
        return sf::Color::Magenta;
    return it->second;
}