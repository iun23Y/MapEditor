#pragma once

#include <unordered_map>
#include <SFML/Graphics.hpp>

class textureManager {
private:
    std::unordered_map<std::string, sf::Texture> textures;
    std::unordered_map<std::string, sf::Image> images;
    std::unordered_map<std::string, sf::Color> colors;

    std::string pathJson;
    std::string pathTextures;

    bool loadData();
public:
    textureManager();

    const sf::Texture* getTexture(const std::string& blockName) const;
    const sf::Image* getImage(const std::string& blockName) const;
    sf::Color getColor(const std::string& blockName) const;
};