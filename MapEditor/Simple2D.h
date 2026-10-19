#pragma once

#include <SFML/Graphics.hpp>

class Simple2D
{
public:
    void setData(const BlockData&);
    void markDirty();
    void update();
    void draw(sf::RenderTarget&, const sf::View&);
};

