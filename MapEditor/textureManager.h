#pragma once

#include <unordered_map>
#include <SFML/Graphics.hpp>
#include "BlockPalette.h"

class textureManager {
private:
    std::unordered_map<std::string, sf::Texture> textures;
    std::unordered_map<std::string, sf::Image> images;
    std::unordered_map<std::string, sf::Color> colors;
    std::unordered_map<std::string, std::string> blockTypes;

    sf::Texture atlasTexture;
    std::vector<uint16_t> idToSlot;

    std::string pathJson;
    std::string pathTextures;

    bool loadData();
public:
    textureManager();
    void buildAtlas(const BlockPalette& palette);
    const sf::Texture& getAtlas() const { return atlasTexture; }
    uint16_t getAtlasSlot(int blockId) const;

    static constexpr int TILE = 16;
    static constexpr int COLS = 64;
    static constexpr int ROWS = 64;
    static constexpr int ATLAS_W = COLS * TILE;
    static constexpr int ATLAS_H = ROWS * TILE;

    std::string getBlockType(const std::string& blockName) const;
    const sf::Texture* getTexture(const std::string& blockName) const;
    const sf::Image* getImage(const std::string& blockName) const;
    sf::Color getColor(const std::string& blockName) const;
};