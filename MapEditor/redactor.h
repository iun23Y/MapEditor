#pragma once

#include "schematic.h"
#include "tileMap.h"
#include "GuiManager.h"
#include "helper.h"
#include "textureManager.h"
#include "counters.h"
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
    AddPolygonCounters,
    AddCircleCounters
};

class Redactor {
public:
    explicit Redactor(std::unique_ptr<SchematicMap> schematic, int width, int height);
    void run();
    void setMode(Modes mode);

private:
    sf::Vector2f tempMousePos;
    std::unique_ptr<Counter> currentCounter;
    std::vector<std::unique_ptr<Counter>> counters;

    struct BuildResult {
        sf::Image blockImage;
        sf::Image heightImage;
        float maxHeight = -128.0f;
        int width = 0;
        int length = 0;
        std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topBlocks;
        std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topHeights;
    };

    bool leftMousePressed = false;
    sf::Time pressStartTime;
    sf::Vector2f pressPosition;
    bool isDragging = false;
    static constexpr sf::Time DRAG_THRESHOLD = sf::milliseconds(300);
    sf::Clock clickClock;

    void handleMapClick(const sf::Vector2f& windowPixel);
    BuildResult buildTexturesImages();
    void updateView();
    void initUI();
    void handleEvents();
    void draw();
    void showLoadDialog();
    void setStatusText(const std::wstring& text);

    Modes currentMode = Modes::None;

    std::unique_ptr<SchematicMap> schem;
	textureManager texManager;
	std::optional<schematicTexture> schemTexture;
    sf::RenderWindow window;
    sf::View redactorView;
    sf::View uiView;
    int windowWidth;
    int windowHeight;
    float zoom = 1.0f;
    sf::Vector2f viewCenter{ 0.f, 0.f };
    sf::Vector2f dragStart;
    sf::Vector2f dragCenter;
    bool dragging = false;
    std::unique_ptr<sf::Text> statusText;
    std::unique_ptr<sf::Text> infoText;
    sf::Font font;
    std::wstring statusMessage;
    std::optional<TileMap> tileMap;

    GuiManager ui;

    static const sf::Color MAP_BACKGROUND_COLOR;

    static constexpr float TOP_BAR_HEIGHT = 31.f;
    static constexpr float RIGHT_PANEL_WIDTH = 200.f;
    static constexpr float UI_PADDING = 12.f;
    static constexpr float BUTTON_HEIGHT = 21.f;
};