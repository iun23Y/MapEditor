#pragma once

#include "schematic.h"

#include <SFML/Graphics.hpp>
#include <memory>

class SchematicViewer {
public:
    SchematicViewer(const SchematicMap& sm, int w = 800, int h = 600);
    void run();

private:
    void loadTextures();
    sf::Image* getBlockTexture(int blockId);
    void buildTextures();
    void updateSprites();
    void handleEvents();
    void draw();

    const SchematicMap& schem;
    int windowWidth;
    int windowHeight;

    sf::Texture blockColorTexture;
    sf::Texture heightTexture;
    std::unique_ptr<sf::Sprite> renderSprite;
    std::unique_ptr<sf::Shader> shadowShader;

    float maxHeight = 1.0f;
    float zoom = 2.0f;
    sf::Vector2f viewOffset;
    bool dragging = false;
    sf::Vector2f dragStart;
    sf::Vector2f dragViewStart;

    sf::RenderWindow window;
    sf::Font font;

    static constexpr int BLOCK_PIXEL_SIZE = 16;

    std::unordered_map<std::string, sf::Image> textureImages;
    std::unique_ptr<sf::Text> infoText;
    std::unique_ptr<sf::Text> coordText;
};
