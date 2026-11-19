#pragma once

#include <imgui.h>
#include <imgui-SFML.h>
#include <malloc.h>
#include <chrono>
#include <future>
#include <filesystem>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <climits>
#include <cmath>
#include "Schematic.h"
#include "Data.h"
#include "DataPersistence.h"
#include "Chunk.h"
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
        if (!Schematic::loadForSchematic(schematicPath, palette, blocks, bounds))
            throw std::runtime_error("Failed to load schematic: " + schematicPath);

        data.setWorldOffset(bounds.min);
        world->data().setBlocks(blocks, glm::ivec3(0), bounds.max - bounds.min);
        std::vector<int>().swap(blocks);

        for (const auto& [cc, chunk] : world->data().getChunks())
            if (!chunk.isEmpty())
                world->allocateChunkId(chunk.getMinPos());

        persistenceRoot = fs::path(getExeDirectory());
        DataPersistence::deleteObject(NO_OBJECT, persistenceRoot);
        world->setId(DataPersistence::nextObjectId(persistenceRoot));
        data.setNextObjectId(world->id() + 1);
        if (!DataPersistence::saveObject(*world, persistenceRoot))
            throw std::runtime_error("Failed to save schematic world");

        ::textureManager = &textureManager;
        ::palette = &palette;
        ::windowSize = toGLM(static_cast<sf::Vector2i>(window.getSize()));
        mainViewer.setViewport({ 0, 0 }, windowSize);
        mainViewer.setSide(Side::Top);

        updateRegions();
        renderData.build(data);
        mainViewer.setData(renderData.getRenderData());
        _heapmin();
        syncCameraFromView();

        window.setFramerateLimit(60);
        ImGui::SFML::Init(window);
        ImGui::GetIO().IniFilename = nullptr;

        if (auto* s = mainViewer.streamed()) {
            m_loadsPerFrame = (int)s->loadsPerFrame();
            m_concurrentLoads = (int)s->maxConcurrentLoads();
            m_cacheTiles = (int)s->maxCacheSize();
        }
        m_sideIdx = (int)mainViewer.getSide();
    }

    ~Redactor() {
        ImGui::SFML::Shutdown();
    }

    void run() {
        while (window.isOpen()) {
            const float dt = m_dtClock.restart().asSeconds();
            ImGui::SFML::Update(window, sf::seconds(dt));
            handleEvents();
            updateCamera(dt);
            draw();
        }
        for (auto r : loadedRegions)
            DataPersistence::updateRegion(r, data, persistenceRoot);
    }

private:
    int m_maxLoadedRegions = 64;
    int m_loadsPerFrame = 4;
    int m_concurrentLoads = 4;
    int m_cacheTiles = 128;
    int m_sideIdx = 0;

    static constexpr float MIN_SIZE = 32.f;
    static constexpr float MAX_SIZE = 20000.f;
    static constexpr float PAN_SPEED_MULT = 1.2f;
    static constexpr float ZOOM_STEP = 0.85f;
    static constexpr float CAMERA_LERP = 14.f;
    static constexpr float PANEL_W = 320.f;

    sf::RenderWindow window{ sf::VideoMode::getFullscreenModes()[0], "Map Editor" };
    Data data;
    RenderData renderData;
    std::future<RenderData> renderBuild;
    BlockPalette palette;
    TextureManager textureManager;
    Viewer2D mainViewer;
    fs::path persistenceRoot;
    std::unordered_set<glm::ivec2> loadedRegions;

    int selectedId = -1;
    bool m_rebuildWasEdit = false;

    sf::Vector2f m_currentCenter{ 0.f, 0.f };
    sf::Vector2f m_targetCenter{ 0.f, 0.f };
    float m_currentSize = 1000.f;
    float m_targetSize = 1000.f;

    bool m_rmbDown = false;
    sf::Vector2i m_lastMousePos{ 0, 0 };
    sf::Clock m_dtClock;

    void syncCameraFromView() {
        m_currentCenter = mainViewer.getCenterF();
        m_currentSize = mainViewer.getSizeF().y;
        m_targetCenter = m_currentCenter;
        m_targetSize = m_currentSize;
    }

    void updateCamera(float dt) {
        if (dt <= 0.f) return;
        if (dt > 0.1f) dt = 0.1f;

        sf::Vector2f panDir(0.f, 0.f);
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) panDir.y -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) panDir.y += 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) panDir.x -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) panDir.x += 1.f;

        if (panDir.x != 0.f || panDir.y != 0.f) {
            const float len = std::sqrt(panDir.x * panDir.x + panDir.y * panDir.y);
            panDir /= len;
            const float speed = m_currentSize * PAN_SPEED_MULT;
            m_targetCenter += panDir * speed * dt;
        }

        const float alpha = 1.f - std::exp(-CAMERA_LERP * dt);
        m_currentCenter += (m_targetCenter - m_currentCenter) * alpha;
        m_currentSize += (m_targetSize - m_currentSize) * alpha;

        const float winW = (float)window.getSize().x;
        const float winH = (float)window.getSize().y;
        if (winW <= 0.f || winH <= 0.f) return;

        const float aspect = winW / winH;
        mainViewer.setCenterF(m_currentCenter);
        mainViewer.setSizeF({ m_currentSize * aspect, m_currentSize });
    }

    void applyRenderSettings() {
        if (auto* s = mainViewer.streamed()) {
            s->setLoadsPerFrame((size_t)m_loadsPerFrame);
            s->setMaxConcurrentLoads((size_t)m_concurrentLoads);
            s->setMaxCacheSize((size_t)m_cacheTiles);
        }
    }

    void rebuildForRegion() {
        if (renderBuild.valid()) return;
        m_rebuildWasEdit = false;
        renderBuild = renderData.buildAsync(data);
    }

    void rebuildForEdit() {
        if (renderBuild.valid()) return;
        m_rebuildWasEdit = true;
        renderBuild = renderData.buildAsync(data);
    }

    void finishRebuild() {
        if (!renderBuild.valid()) return;
        if (renderBuild.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            return;

        renderData = renderBuild.get();
        mainViewer.setData(renderData.getRenderData());

        if (m_rebuildWasEdit) {
            mainViewer.invalidate();
            m_rebuildWasEdit = false;
        }
    }

    bool updateRegions() {
        const glm::ivec4 view = mainViewer.viewRect();

        const glm::ivec3 centerPos{ view.x + view.z / 2, 0, view.y + view.w / 2 };
        const glm::ivec2 centerRegion = DataPersistence::regionOf(centerPos);

        const glm::ivec3 minPos{ view.x, 0, view.y };
        const glm::ivec3 maxPos{
            view.x + std::max(view.z - 1, 0),
            0,
            view.y + std::max(view.w - 1, 0)
        };
        const glm::ivec2 first = DataPersistence::regionOf(minPos);
        const glm::ivec2 last = DataPersistence::regionOf(maxPos);

        const size_t cap = (size_t)std::max(1, m_maxLoadedRegions);
        std::vector<glm::ivec2> candidates;
        candidates.reserve(cap);

        const int maxDistance = std::max(
            std::abs(first.x - centerRegion.x) + std::abs(first.y - centerRegion.y),
            std::max(
                std::abs(first.x - centerRegion.x) + std::abs(last.y - centerRegion.y),
                std::max(
                    std::abs(last.x - centerRegion.x) + std::abs(first.y - centerRegion.y),
                    std::abs(last.x - centerRegion.x) + std::abs(last.y - centerRegion.y))));

        for (int distance = 0; distance <= maxDistance && candidates.size() < cap; ++distance) {
            for (int dx = -distance; dx <= distance && candidates.size() < cap; ++dx) {
                const int dz = distance - std::abs(dx);
                const int x = centerRegion.x + dx;
                if (x < first.x || x > last.x) continue;

                const int z1 = centerRegion.y + dz;
                if (z1 >= first.y && z1 <= last.y)
                    candidates.push_back({ x, z1 });

                if (dz != 0 && candidates.size() < cap) {
                    const int z2 = centerRegion.y - dz;
                    if (z2 >= first.y && z2 <= last.y)
                        candidates.push_back({ x, z2 });
                }
            }
        }

        std::unordered_set<glm::ivec2> wanted;
        wanted.reserve(candidates.size());
        for (auto& c : candidates) wanted.insert(c);

        bool changed = false;

        for (auto it = loadedRegions.begin(); it != loadedRegions.end(); ) {
            if (wanted.contains(*it)) { ++it; continue; }
            DataPersistence::unloadRegion(*it, data, persistenceRoot, wanted);
            it = loadedRegions.erase(it);
            changed = true;
        }

        for (auto& region : candidates) {
            if (loadedRegions.insert(region).second) {
                DataPersistence::loadRegion(region, data, persistenceRoot);
                changed = true;
            }
        }

        return changed;
    }

    void handleEvents() {
        finishRebuild();

        while (const auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>()) {
                window.close();
                return;
            }

            if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                if (ImGui::GetIO().WantCaptureMouse) continue;

                const float factor = (wheel->delta > 0) ? ZOOM_STEP : 1.f / ZOOM_STEP;
                const float newSize = std::clamp(m_targetSize * factor, MIN_SIZE, MAX_SIZE);

                const sf::Vector2f center = mainViewer.getCenterF();
                const sf::Vector2f size = mainViewer.getSizeF();

                const float winW = (float)window.getSize().x;
                const float winH = (float)window.getSize().y;
                if (winW <= 0.f || winH <= 0.f) continue;

                const sf::Vector2i mp = sf::Mouse::getPosition(window);
                const float worldMx = center.x - size.x * 0.5f + (float)mp.x * (size.x / winW);
                const float worldMy = center.y - size.y * 0.5f + (float)mp.y * (size.y / winH);

                const float newW = newSize * (winW / winH);
                const float newX = worldMx - (float)mp.x * (newW / winW);
                const float newY = worldMy - (float)mp.y * (newSize / winH);

                m_targetCenter = sf::Vector2f(newX + newW * 0.5f, newY + newSize * 0.5f);
                m_targetSize = newSize;
            }

            if (const auto* mb = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (ImGui::GetIO().WantCaptureMouse) continue;
                if (mb->button == sf::Mouse::Button::Right) {
                    m_rmbDown = true;
                    m_lastMousePos = sf::Mouse::getPosition(window);
                }
            }

            if (const auto* mb = event->getIf<sf::Event::MouseButtonReleased>()) {
                if (mb->button == sf::Mouse::Button::Right)
                    m_rmbDown = false;

                if (ImGui::GetIO().WantCaptureMouse) continue;
                if (mb->button == sf::Mouse::Button::Left) {
                    sf::Vector2f mousePos((float)mb->position.x, (float)mb->position.y);
                    if (auto hit = mainViewer.pickAtScreen(mousePos, window)) {
                        glm::ivec3 worldPos = std::get<0>(hit.value());
                        if (auto* obj = data.getObjectAt(worldPos))
                            selectedId = obj->id();
                        else
                            selectedId = -1;
                    }
                    else {
                        selectedId = -1;
                    }
                }
            }

            if (const auto* mm = event->getIf<sf::Event::MouseMoved>()) {
                if (!m_rmbDown) continue;
                if (ImGui::GetIO().WantCaptureMouse) {
                    m_lastMousePos = sf::Mouse::getPosition(window);
                    continue;
                }
                const sf::Vector2i cur(mm->position.x, mm->position.y);
                const sf::Vector2i d = m_lastMousePos - cur;
                m_lastMousePos = cur;

                const sf::Vector2f size = mainViewer.getSizeF();
                const float winW = (float)window.getSize().x;
                const float winH = (float)window.getSize().y;
                if (winW > 0.f && winH > 0.f) {
                    m_targetCenter.x += (float)d.x * (size.x / winW);
                    m_targetCenter.y += (float)d.y * (size.y / winH);
                }
            }

            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (ImGui::GetIO().WantCaptureKeyboard) continue;
                auto code = key->code;

                if (code == sf::Keyboard::Key::Escape) {
                    window.close();
                    return;
                }

                if (renderBuild.valid())
                    continue;

                if (selectedId >= 0) {
                    if (auto* obj = data.get(selectedId)) {
                        glm::ivec3 delta{ 0 };
                        if (code == sf::Keyboard::Key::Left)  delta.x = -100;
                        if (code == sf::Keyboard::Key::Right) delta.x = 100;
                        if (code == sf::Keyboard::Key::Up)    delta.z = -100;
                        if (code == sf::Keyboard::Key::Down)  delta.z = 100;

                        if (delta != glm::ivec3(0)) {
                            DataPersistence::moveObject(*obj, delta, persistenceRoot);
                            rebuildForEdit();
                        }
                    }

                    if (code == sf::Keyboard::Key::Delete) {
                        DataPersistence::deleteObject(selectedId, persistenceRoot);
                        data.remove(selectedId);
                        selectedId = -1;
                        rebuildForEdit();
                    }
                }
            }
        }
    }

    void drawUI() {
        ImGuiIO& io = ImGui::GetIO();
        const float winW = (float)window.getSize().x;
        const float winH = (float)window.getSize().y;

        ImGui::SetNextWindowPos(ImVec2(winW - PANEL_W, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(PANEL_W, winH), ImGuiCond_Always);
        ImGui::Begin("Render Settings", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImGui::Text("RENDER");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::SliderInt("Loads / frame", &m_loadsPerFrame, 1, 32))
            applyRenderSettings();
        if (ImGui::SliderInt("Concurrent loads", &m_concurrentLoads, 1, 32))
            applyRenderSettings();
        if (ImGui::SliderInt("Cache tiles", &m_cacheTiles, 16, 512))
            applyRenderSettings();
        ImGui::SliderInt("Regions around", &m_maxLoadedRegions, 1, 256);

        ImGui::Spacing();
        ImGui::Text("VIEW");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Combo("Side", &m_sideIdx, "Top\0Bottom\0Front\0Back\0Left\0Right\0")) {
            mainViewer.setSide((Side)m_sideIdx);
            rebuildForEdit();
        }

        ImGui::Spacing();
        ImGui::Text("STATS");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Loaded regions: %d", (int)loadedRegions.size());
        if (auto* s = mainViewer.streamed()) {
            ImGui::Text("Cache tiles:    %d", (int)s->cacheSize());
            ImGui::Text("Loading:        %d", (int)s->loadingSize());
        }
        ImGui::Text("Selected id:    %d", selectedId);
        ImGui::Text("FPS:            %.0f", io.Framerate);

        ImGui::End();
    }

    void draw() {
        finishRebuild();
        if (!renderBuild.valid() && updateRegions())
            rebuildForRegion();

        window.clear(sf::Color::Black);
        mainViewer.draw(window);
        drawUI();
        ImGui::SFML::Render(window);
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