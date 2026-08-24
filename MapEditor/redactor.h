#pragma once

#include "schematic.h"
#include "tileMap.h"
#include "GuiManager.h"
#include "helper.h"
#include "schematicTexture.h"

#include <SFML/Graphics.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

enum class Modes {
    None,
    Select,
    AddRectCounters,
    AddPolygon,
    AddCircleCounters
};

class Redactor {
private:
    static constexpr sf::Time DRAG_THRESHOLD = sf::milliseconds(300);
    static const sf::Color MAP_BACKGROUND_COLOR;
    static constexpr float TOP_BAR_HEIGHT = 31.f;
    static constexpr float RIGHT_PANEL_WIDTH = 200.f;
    static constexpr float UI_PADDING = 12.f;
    static constexpr float BUTTON_HEIGHT = 21.f;

    std::optional<schematicTexture> schematicTexture;
    textureManager textureManager;
    std::unique_ptr<SchematicMap> schem;
    std::optional<TileMap> tileMap;
    sf::RenderWindow window;
    sf::View redactorView;
    sf::View uiView;
    GuiManager ui;
    int windowWidth;
    int windowHeight;
    float zoom = 1.0f;

    Modes currentMode = Modes::None;

    void initUI();
    void showLoadDialog();

    void addRectPoint(const sf::Vector2f& point);
    void buildRectangle(const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Vector2f& p3);
    void drawRectPreview(sf::RenderWindow& window, const sf::View& view);

    void handleEvents();
    void handleModeClick(const sf::Vector2f& windowPixel);
    void setStatusText(const std::wstring& text);

    void draw();
    void updateView();






    enum class RectStage { Idle, FirstPoint, SecondPoint, ThirdPoint };
    RectStage rectStage = RectStage::Idle;
    sf::Vector2f rectP1, rectP2;
    sf::Vector2f tempMousePos;

    bool leftMousePressed = false;
    sf::Time pressStartTime;
    sf::Vector2f pressPosition;
    bool isDragging = false;

    sf::Clock clickClock;

    sf::Vector2f viewCenter{ 0.f, 0.f };
    sf::Vector2f dragStart;
    sf::Vector2f dragCenter;
    bool dragging = false;
    std::unique_ptr<sf::Text> statusText;
    sf::Font font;
    std::wstring statusMessage;

public:
    explicit Redactor(std::unique_ptr<SchematicMap> schematic, int width, int height);
    void run();
    void setMode(Modes mode);
};