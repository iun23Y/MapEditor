#pragma once

#include "schematic.h"
#include "tileMap.h"
#include "GuiManager.h"   // <-- ����� ���������

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
    AddRectCounters,
    AddCircleCounters
};

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

class Redactor {
public:
    explicit Redactor(std::unique_ptr<SchematicMap> schematic, int width, int height);
    void run();
    void setMode(Modes mode);

private:
    enum class RectStage { Idle, FirstPoint, SecondPoint, ThirdPoint };
    RectStage rectStage = RectStage::Idle;
    sf::Vector2f rectP1, rectP2;
    sf::Vector2f tempMousePos;   // для предпросмотра

    void addRectPoint(const sf::Vector2f& point);
    void buildRectangle(const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Vector2f& p3);
    void drawRectPreview(sf::RenderWindow& window, const sf::View& view);

    struct BuildResult {
        sf::Image blockImage;
        sf::Image heightImage;
        float maxHeight = -128.0f;
        int width = 0;
        int length = 0;
        std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topBlocks;
        std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topHeights;
    };

    // Shader parameters for shadow effect
    float shadowStrength = 0.35f;
    float blockPixelSize = 1.f;
    float maxDiff = 3.0f;
    float textureThreshold = 0.6f;

    // Mouse interaction state for click/drag distinction
    bool leftMousePressed = false;
    sf::Time pressStartTime;
    sf::Vector2f pressPosition;
    bool isDragging = false;
    static constexpr sf::Time DRAG_THRESHOLD = sf::milliseconds(300);
    sf::Clock clickClock;

    void loadTextures();
    void handleMapClick(const sf::Vector2f& windowPixel);
    sf::Image* getBlockTexture(int blockId);
    BuildResult buildTexturesImages();
    void applyBuildResult(BuildResult&& result);
    void startBuildTextures();
    void pollBuildTextures();
    void updateView();
    void initUI();
    void handleEvents();          // ������ handleEvent
    void draw();
    void showLoadDialog();
    void showSaveDialog();
    void setStatusText(const std::wstring& text);

    // ---------- ����� ----------
    Modes currentMode = Modes::None;

    std::unique_ptr<SchematicMap> schem;
    sf::Texture blockColorTexture;
    sf::Texture heightTexture;
    std::optional<sf::Sprite> renderSprite;
    std::unique_ptr<sf::Shader> shadowShader;
    sf::RenderWindow window;
    sf::View redactorView;
    sf::View uiView;
    int windowWidth;
    int windowHeight;
    int buildWidth = 0;
    int buildLength = 0;
    float maxHeight = -128.0f;
    float zoom = 1.0f;
    sf::Vector2f viewCenter{ 0.f, 0.f };
    sf::Vector2f dragStart;
    sf::Vector2f dragCenter;
    bool dragging = false;
    bool buildStarted = false;
    bool buildReady = false;
    bool buildFailed = false;
    std::unordered_map<std::string, sf::Image> textureImages;
    std::future<BuildResult> buildFuture;
    std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topBlocks;
    std::unordered_map<sf::Vector2i, int, Vector2iHash, Vector2iEqual> topHeights;
    std::unique_ptr<sf::Text> statusText;
    std::unique_ptr<sf::Text> infoText;
    sf::Font font;
    std::wstring statusMessage;
    std::optional<TileMap> tileMap;

    // ����� �������� ����������
    GuiManager ui;   // <-- ������ ������ UIButton � �.�.

    static const sf::Color MAP_BACKGROUND_COLOR;   // ������ ��� ���� �����

    // �������
    static constexpr float TOP_BAR_HEIGHT = 31.f;
    static constexpr float RIGHT_PANEL_WIDTH = 200.f;
    static constexpr float UI_PADDING = 12.f;
    static constexpr float BUTTON_HEIGHT = 21.f;

    // �������: topButtons, fileMenuButtons, rightButtons, fileMenuOpen � ��� ������ UI-���������
};