#include "GuiManager.h"
#include <iostream>

// ------------------- Button -------------------
Button::Button(const sf::FloatRect& rect, const std::wstring& label, const sf::Font& font,
    const sf::Color& normal, const sf::Color& hover)
    : rect(rect), label(label), color(normal), hoverColor(hover) {
    if (font.getInfo().family != "") {
        text.emplace(font);
        text->setString(label);
        text->setCharacterSize(static_cast<unsigned int>(rect.size.y * 0.6f));
        text->setFillColor(GuiStyle::TextColor);
        sf::FloatRect textRect = text->getLocalBounds();
        text->setOrigin({ textRect.position.x + textRect.size.x / 2.f, textRect.position.y + textRect.size.y / 2.f });
        text->setPosition({ rect.position.x + rect.size.x / 2.f, rect.position.y + rect.size.y / 2.f });
    }
    else {
        std::cerr << "Warning: Button created without valid font!\n";
    }
}

void Button::update(const sf::Vector2f& mousePos) {
    hovered = contains(mousePos);
}

bool Button::contains(const sf::Vector2f& point) const {
    return rect.contains(point);
}

void Button::activate() {
    if (hovered && action) action();
}

void Button::draw(sf::RenderTarget& target) const {
    sf::RectangleShape shape(rect.size);
    shape.setPosition(rect.position);
    shape.setFillColor(hovered ? hoverColor : color);
    target.draw(shape);
    if (text.has_value()) target.draw(text.value());
}

// ------------------- Label -------------------
Label::Label(const sf::Vector2f& position, const std::wstring& str, const sf::Font& font,
    unsigned int size, const sf::Color& color) {
    text.emplace(font);
    text->setString(str);
    text->setCharacterSize(size);
    text->setFillColor(color);
    text->setPosition(position);
}

// ------------------- Tile -------------------
Tile::Tile(const sf::FloatRect& rect, const sf::Color& bg, float outline, const sf::Color& outlineCol)
    : rect(rect), backgroundColor(bg), outlineThickness(outline), outlineColor(outlineCol) {
}

void Tile::update(const sf::Vector2f& mousePos) {
    if (rect.contains(mousePos)) {
        for (auto& b : buttons) b.update(mousePos);
    }
    else {
        for (auto& b : buttons) b.resetHover();
    }
}

void Tile::handleClick() {
    for (auto& b : buttons) b.activate();
}

void Tile::draw(sf::RenderTarget& target) const {
    sf::RectangleShape bg(rect.size);
    bg.setPosition(rect.position);
    bg.setFillColor(backgroundColor);
    bg.setOutlineThickness(outlineThickness);
    bg.setOutlineColor(outlineColor);
    target.draw(bg);

    for (const auto& lbl : labels) lbl.draw(target);
    for (const auto& btn : buttons) btn.draw(target);
}

// ------------------- GuiManager -------------------
GuiManager::GuiManager() {
    loadFont("arial.ttf");
}

bool GuiManager::loadFont(const std::string& filename) {
    return font.openFromFile(filename);
}

void GuiManager::update(const sf::Vector2f& mousePos, bool mouseClicked) {
    for (auto& tile : tiles) {
        if (!tile.isActive()) continue;
        tile.update(mousePos);
        if (mouseClicked && tile.contains(mousePos)) {
            tile.handleClick();
        }
    }
}

void GuiManager::draw(sf::RenderTarget& target) const {
    for (const auto& tile : tiles) {
        if (tile.isActive()) tile.draw(target);
    }
}

void GuiManager::setActiveTile(size_t index, bool value) {
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (i == index) {
            tiles[i].setActive(value);
        }
    }
}

bool GuiManager::getActiveTile(size_t index) {
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (i == index) {
            return tiles[i].isActive();
        }
    }
	return false;
}

bool GuiManager::contains(const sf::Vector2f& point) const {
    for (const auto& tile : tiles) {
        if (tile.isActive() && tile.contains(point)) return true;
    }
    return false;
}