#pragma once

#include "schematic.h"

#include <SFML/Graphics.hpp>
#include <memory>

namespace {
    struct Vector2iHash {
        std::size_t operator()(const sf::Vector2i& v) const noexcept {
            std::size_t seed = 0;
            auto hash_combine = [&seed](int value) {
                seed ^= std::hash<int>{}(value)+0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
            hash_combine(v.x);
            hash_combine(v.y);
            return seed;
        }
    };

    struct Vector2iEqual {
        bool operator()(const sf::Vector2i& lhs, const sf::Vector2i& rhs) const noexcept {
            return lhs.x == rhs.x && lhs.y == rhs.y;
        }
    };
}


class SchematicViewer {
public:
    SchematicViewer(const SchematicMap& sm,
                    int w, int h);
    void handleEvent();
    void draw();
    void run();

private:
    void loadTextures();
    sf::Image* getBlockTexture(int blockId);
    void buildTextures();
    void updateSprites();
    void drawInfoAndCoords();

    const SchematicMap& schem;

    sf::Texture blockColorTexture;
    sf::Texture heightTexture;
    std::unique_ptr<sf::Sprite> renderSprite;
    std::unique_ptr<sf::Shader> shadowShader;

    int windowWidth;
    int windowHeight;
    sf::RenderWindow window;
    int maxHeight;

    float zoom = 1.0f;
    sf::Vector2f viewOffset{0.f, 0.f};
    bool dragging = false;
    sf::Vector2f dragStart;
    sf::Vector2f dragViewStart;

    std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topBlocks;
    std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topHeights;

    sf::Font font;

    static constexpr int BLOCK_PIXEL_SIZE = 16;

    std::unordered_map<std::string, sf::Image> textureImages;
    std::unique_ptr<sf::Text> infoText;
    std::unique_ptr<sf::Text> coordText;
};
