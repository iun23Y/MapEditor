#pragma once

#include "Schematic.h"
#include "Data.h"
#include "Bounds.h"
#include "RenderData.h"
#include "Viewer2D.h"
#include "BigObject.h"

class Redactor {
public:
    Redactor(const std::string& schematicPath) {
        palette = textureManager.buildPalette();
        textureManager.buildAtlas(palette);
        auto world = std::make_unique<BigObject>();
        std::vector<int> blocks;
        Bounds bounds;
        if (!Schematic::loadForSchematic(schematicPath, palette, blocks, bounds)) {
            throw std::runtime_error("Failed to load schematic: " + schematicPath);
        }
        world->data().setBlocks(blocks, bounds.min, bounds.max);
        data.add(std::move(world), ObjectRenderType::Renderer);
        renderData.build(data);
        ::textureManager = &textureManager;
        ::windowSize = toGLM(static_cast<sf::Vector2i>(window.getSize()));
        mainViewer.setData(renderData.getRenderData());
        mainViewer.setViewport({ 0, 0 }, windowSize);
		mainViewer.setSide(Side::Top);
    }
    void run() {
        while (window.isOpen()) {
            handleEvents();
            draw();
        }
    }
private:
    sf::RenderWindow window{ sf::VideoMode::getFullscreenModes()[0], "Map Editor" };
    Data data;
    RenderData renderData;
    BlockPalette palette;
    TextureManager textureManager;
    Viewer2D mainViewer;

    void handleEvents() {
        while (const auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
                return;
            }
            if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                float factor = (wheel->delta > 0) ? 1.2f : 1.0f / 1.2f;
				mainViewer.zoom(factor);
            }
            if (event->getIf<sf::Event::KeyPressed>()) {
                if (event->getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::Escape) {
                    window.close();
                    return;
				}
                if (event->getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::W) {
                    mainViewer.move({ 0, -10 });
				}
                if (event->getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::S) {
					mainViewer.move({ 0, 10 });
                }
                if (event->getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::A) {
					mainViewer.move({ -10, 0 });
                }
                if (event->getIf<sf::Event::KeyPressed>()->code == sf::Keyboard::Key::D) {
                    mainViewer.move({ 10, 0 });
                }
            }
        }
    }
    void draw() {
        window.clear(sf::Color::Black);
        mainViewer.draw(window);
        window.display();
    }
};

/*#pragma once

#include "schematic.h"
#include "tileMap.h"
#include "GuiManager.h"
#include "helper.h"
#include "TextureManager.h"
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

    bool leftMousePressed = false;
    bool mouseClicked = false;
    sf::Time pressStartTime;
    sf::Vector2f pressPosition;
    bool isDragging = false;
    bool isDraggingMap = false;
    bool isDraggingCounter = false;
    std::size_t draggingCounterIndex = std::numeric_limits<std::size_t>::max();
    sf::Vector2f dragCounterStartWorld;
    std::vector<sf::Vector2f> dragStartPoints;
    sf::Vector2f lastMouseWorld;
    
    static constexpr sf::Time DRAG_THRESHOLD = sf::milliseconds(300);
    sf::Clock clickClock;

    int findCounterAt(const sf::Vector2f& worldPos) const;
    void handleMapClick(const sf::Vector2f& windowPixel);
    void updateView();
    void initUI();
    void handleEvents();
    void draw();
    void showLoadDialog();
    void setStatusText(const std::wstring& text);

    Modes currentMode = Modes::None;

    std::unique_ptr<SchematicMap> schem;
    BlockPalette pallete;
	TextureManager textureManager;
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
    std::size_t selectedCounterIndex = std::numeric_limits<std::size_t>::max();
    std::unique_ptr<sf::Text> statusText;
    std::unique_ptr<sf::Text> infoText;
    std::wstring statusMessage;
    std::optional<TileMap> tileMap;

    GuiManager ui;

    static const sf::Color MAP_BACKGROUND_COLOR;

    static constexpr float TOP_BAR_HEIGHT = 31.f;
    static constexpr float RIGHT_PANEL_WIDTH = 200.f;
    static constexpr float UI_PADDING = 12.f;
    static constexpr float BUTTON_HEIGHT = 21.f;
};
*/