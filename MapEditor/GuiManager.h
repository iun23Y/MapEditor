#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <functional>
#include <optional>

// ------------------- Глобальные стили -------------------
namespace GuiStyle {
    const sf::Color PanelBackground = sf::Color(15, 25, 40, 220);
    const sf::Color PanelBorder = sf::Color(220, 220, 220, 180);
    const sf::Color ButtonNormal = sf::Color(40, 70, 90, 0);
    const sf::Color ButtonHover = sf::Color(80, 180, 230, 50);
    const sf::Color TextColor = sf::Color(240, 240, 240);
    const sf::Color OverlayColor = sf::Color(255, 255, 255, 32);
}

// ------------------- Button -------------------
class Button {
private:
    sf::FloatRect rect;
    bool hovered = false;
    std::wstring label;
    std::optional<sf::Text> text;
    std::function<void()> action;
    sf::Color color;
    sf::Color hoverColor;
public:
    // Основной конструктор с явными цветами
    Button(const sf::FloatRect& rect, const std::wstring& label, const sf::Font& font,
        const sf::Color& normal = GuiStyle::ButtonNormal,
        const sf::Color& hover = GuiStyle::ButtonHover);

    void setAction(std::function<void()> func) { action = func; }
    void setColors(const sf::Color& normal, const sf::Color& hover) { color = normal; hoverColor = hover; }
    void update(const sf::Vector2f& mousePos);
    void resetHover() { hovered = false; }
    bool contains(const sf::Vector2f& point) const;
    void activate();
    void draw(sf::RenderTarget& target) const;
};

// ------------------- Label -------------------
class Label {
private:
    std::optional<sf::Text> text;
public:
    Label(const sf::Vector2f& position, const std::wstring& str, const sf::Font& font,
        unsigned int size = 20, const sf::Color& color = GuiStyle::TextColor);
    void setString(const std::wstring& str) { text->setString(str); }
    void draw(sf::RenderTarget& target) const { target.draw(text.value()); }
};

// ------------------- Tile (вкладка) -------------------
class Tile {
private:
    sf::FloatRect rect;
    bool active = false;
    sf::Color backgroundColor;
    float outlineThickness;
    sf::Color outlineColor;

    std::vector<Label> labels;
    std::vector<Button> buttons;
public:
    Tile(const sf::FloatRect& rect,
        const sf::Color& bg = GuiStyle::PanelBackground,
        float outline = 1.f,
        const sf::Color& outlineCol = GuiStyle::PanelBorder);

    void setBackground(const sf::Color& color) { backgroundColor = color; }
    void setOutline(float thickness, const sf::Color& color) {
        outlineThickness = thickness;
        outlineColor = color;
    }

    void addLabel(const Label& lbl) { labels.push_back(lbl); }
    void addButton(const Button& btn) { buttons.push_back(btn); }

    void update(const sf::Vector2f& mousePos);
    void handleClick();
    bool contains(const sf::Vector2f& point) const { return rect.contains(point); }
    void draw(sf::RenderTarget& target) const;

    void setActive(bool a) { active = a; }
    bool isActive() const { return active; }
    sf::FloatRect getRect() const { return rect; }
};

// ------------------- GuiManager (главный контейнер) -------------------
class GuiManager {
private:
    sf::Font font;
    std::vector<Tile> tiles;
public:
    GuiManager();
    bool loadFont(const std::string& filename);

    void addTile(const Tile& tile) { tiles.push_back(tile); }
    void update(const sf::Vector2f& mousePos, bool mouseClicked);
    void draw(sf::RenderTarget& target) const;

    const sf::Font& getFont() const { return font; }

    void setActiveTile(size_t index, bool value);
    bool getActiveTile(size_t index);
    size_t getTilesCount() const { return tiles.size(); }

    bool contains(const sf::Vector2f& point) const;
};