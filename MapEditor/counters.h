#pragma once

#include "helper.h"
#include "textureManager.h"

#include <SFML/Graphics.hpp>

class Counter {
private:
    textureManager* textures;

    std::vector<sf::Vector2f> points;
    sf::Image image;
public:
    Counter(textureManager* textureManager) : textures(textureManager) {
        
    }

    void drawPreview(sf::RenderWindow& window) {

    }

    const std::vector<sf::Vector2f>& getPoints() {
        return points;
    }
};

