#include "textureManager.h"
#include "helper.h"
#include <fstream>
#include <nlohmann/json.hpp>
using namespace nlohmann;

textureManager::textureManager() {
    pathJson = getExeDirectory() + "Resources\\blockTextures.json";
    pathTextures = getExeDirectory() + "Resources\\Textures";
    loadData();
}
bool textureManager::loadData() {
    std::ifstream file(pathJson);
    if (!file.is_open()) return false;
    json data;
    try { file >> data; }
    catch (const json::parse_error&) {
        return false;
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
        colors[blockName] = color;

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
        if (loaded) {
            textures.emplace(blockName, std::move(texture));
        }

        auto it = textures.find(blockName);
        if (it != textures.end()) {
            sf::Image img = it->second.copyToImage();
            images[blockName] = std::move(img);
        }
        else {
            sf::Image img({ 16, 16 }, colors[blockName]);
            images[blockName] = std::move(img);
        }
    }
    return true;
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