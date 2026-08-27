#include "redactor.h"
#include "schematic.h"
#include "GuiManager.h"
#include "helper.h"
#include "textureManager.h"
#include "counters.h"
#include "schematicTexture.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <commdlg.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Comdlg32.lib")

const sf::Color Redactor::MAP_BACKGROUND_COLOR = sf::Color(0, 0, 0);

Redactor::Redactor(std::unique_ptr<SchematicMap> schematic, int width, int height)
    : schem(std::move(schematic)), windowWidth(width), windowHeight(height),
    window(sf::VideoMode({ static_cast<unsigned int>(width), static_cast<unsigned int>(height) }),
        L"Schematic Redactor", sf::Style::Default) {
    window.setFramerateLimit(60);
    if (!font.openFromFile(getExeDirectory() + "Benbow Regular.ttf")) {
        (void)font.openFromFile("C:/Windows/Fonts/arial.ttf");
    }

    try {
        tileMap.emplace(19, sf::Vector2f{ float(schem->getPos1().x), float(schem->getPos1().z) });
        tileMap->setCustomTileSource("https://tile.buildtheearth.ru/YandexAero/{x}/{y}/{z}");
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize tile map: " << e.what() << std::endl;
        // Continue without tile map background
    }

    schemTexture.emplace(schem.get(), &texManager);

    redactorView = sf::View(sf::FloatRect({ 0.f, 0.f }, { static_cast<float>(width), static_cast<float>(height) }));
    uiView = sf::View(sf::FloatRect({ 0.f, 0.f }, { static_cast<float>(width), static_cast<float>(height) }));
    viewCenter = { 0.f, 0.f };
    zoom = 1.0f;

    // Initialize mouse interaction state
    leftMousePressed = false;
    isDraggingMap = false;
    isDraggingCounter = false;
    lastMouseWorld = {0.f, 0.f};

    initUI();

    // Компиляция шейдера для теней
    std::string shaderCode = R"(
uniform sampler2D blockTexture;
uniform sampler2D heightTexture;
uniform float maxHeight;
uniform float shadowStrength;
uniform float blockPixelSize;
uniform float maxDiff;
uniform float zoom;
uniform float textureThreshold;

void main() {
    vec2 texCoord = gl_TexCoord[0].xy;
    vec4 heightData = texture2D(heightTexture, texCoord);
    float h = heightData.r * maxHeight;
    float mask = heightData.g;

    if (mask < 128.0 / 255.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec2 texSize = textureSize(heightTexture, 0);
    vec2 pixelPos = texCoord * texSize;
    vec2 blockIndex = floor(pixelPos / blockPixelSize);
    vec2 inside = (pixelPos - blockIndex * blockPixelSize) / blockPixelSize;

    vec4 blockColor = texture2D(blockTexture, texCoord);
    float alpha = blockColor.a;

    float shadow = 0.0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;

            vec2 neighborBlock = blockIndex + vec2(float(dx), float(dy));
            vec2 neighborCoord = (neighborBlock * blockPixelSize + blockPixelSize * 0.5) / texSize;
            vec4 neighborData = texture2D(heightTexture, neighborCoord);
            float neighborH = neighborData.r * maxHeight;
            float neighborMask = neighborData.g;

            if (neighborMask < 128.0 / 255.0) continue;

            if (neighborH > h) {
                float diff = min(neighborH - h, maxDiff);
                float weight = 1.0;

                if (dx == -1)      weight *= (1.0 - inside.x);
                else if (dx == 1)  weight *= inside.x;

                if (dy == -1)      weight *= (1.0 - inside.y);
                else if (dy == 1)  weight *= inside.y;

                shadow += diff * weight;
            }
        }
    }

    shadow = min(1.0, shadow * shadowStrength);
    float brightness = max(0.4, 1.0 - shadow);
    vec3 shadedColor = blockColor.rgb * brightness;
    gl_FragColor = vec4(shadedColor, alpha);
}
)";
    statusText = std::make_unique<sf::Text>(font, L"Loading map...", 14);
    statusText->setFillColor(GuiStyle::TextColor);
}

void Redactor::initUI() {
    // Загружаем шрифт для UI, если ещё не загружен
    if (ui.getFont().getInfo().family.empty()) {
        ui.loadFont(getExeDirectory() + "Benbow Regular.ttf");
    }

    // Очищаем существующие плитки вместо полной пересоздания
    // Для простоты оставляем пересоздание, но в будущем можно оптимизировать
    ui = GuiManager();
    if (!ui.loadFont(getExeDirectory() + "Benbow Regular.ttf")) {
        // Fallback to Arial if Benbow not found
        ui.loadFont("C:/Windows/Fonts/arial.ttf");
    }

    Tile topTile(sf::FloatRect({ 0.f, 0.f }, { float(windowWidth), float(TOP_BAR_HEIGHT) }));
    topTile.setBackground(GuiStyle::PanelBackground);
    topTile.setOutline(1.f, GuiStyle::PanelBorder);
    
	Button fileBtn(sf::FloatRect({ UI_PADDING, 4.f }, { 100.f, 25.f }), L"File", ui.getFont());
    fileBtn.setAction([this]() { ui.setActiveTile(1, !ui.getActiveTile(1)); });
	topTile.addButton(fileBtn);

    // 1. Вкладка "File" (верхняя левая часть)
    Tile fileTile(sf::FloatRect({ UI_PADDING, float(TOP_BAR_HEIGHT) }, { 120.f, 95.f }));
    fileTile.setBackground(GuiStyle::PanelBackground);
    fileTile.setOutline(1.f, GuiStyle::PanelBorder);

    Button loadBtn(sf::FloatRect({ 22.f, 35.f }, { 100.f, 25.f }), L"Load", ui.getFont());
    loadBtn.setAction([this]() { showLoadDialog(); });
    fileTile.addButton(loadBtn);

    Button saveBtn(sf::FloatRect({ 22.f, 65.f }, { 100.f, 25.f }), L"Save", ui.getFont(),
        sf::Color(100, 100, 120), sf::Color(50, 150, 255));
    saveBtn.setAction([this]() { schem->exportToSchematic("Leningradskaya", "C:/MySchematics"); });
    fileTile.addButton(saveBtn);

    Button exitBtn(sf::FloatRect({ 22.f, 95.f }, { 100.f, 25.f }), L"Exit", ui.getFont());
    exitBtn.setAction([this]() { window.close(); });
    fileTile.addButton(exitBtn);

	ui.addTile(topTile);
    ui.addTile(fileTile);

    // 2. Вкладка "Tools" (правая панель)
    float rightX = static_cast<float>(windowWidth) - RIGHT_PANEL_WIDTH;
    Tile toolsTile(sf::FloatRect({ rightX, TOP_BAR_HEIGHT }, { RIGHT_PANEL_WIDTH,
        static_cast<float>(windowHeight) - TOP_BAR_HEIGHT }));
    toolsTile.setBackground(GuiStyle::PanelBackground);
    toolsTile.setOutline(1.f, GuiStyle::PanelBorder);

    Button toolSelect(sf::FloatRect({ windowWidth - RIGHT_PANEL_WIDTH + UI_PADDING, 35.f }, { RIGHT_PANEL_WIDTH - UI_PADDING * 2, 25.f }),
		L"Select", ui.getFont());
	toolSelect.setAction([this]() { setMode(Modes::None); });
	toolsTile.addButton(toolSelect);

    Button toolRect(sf::FloatRect({ windowWidth - RIGHT_PANEL_WIDTH + UI_PADDING, 70.f }, { RIGHT_PANEL_WIDTH - UI_PADDING * 2, 25.f }),
        L"Rect Counters", ui.getFont());
    toolRect.setAction([this]() { setMode(Modes::AddRectCounters); });
    toolsTile.addButton(toolRect);

    Button toolPoly(sf::FloatRect({ windowWidth - RIGHT_PANEL_WIDTH + UI_PADDING, 105.f }, { RIGHT_PANEL_WIDTH - UI_PADDING * 2, 25.f }),
        L"Polygon Counters", ui.getFont());
    toolPoly.setAction([this]() { setMode(Modes::AddPolygonCounters); });
    toolsTile.addButton(toolPoly);

    Button toolCirclet(sf::FloatRect({ windowWidth - RIGHT_PANEL_WIDTH + UI_PADDING, 140.f }, { RIGHT_PANEL_WIDTH - UI_PADDING * 2, 25.f }),
        L"Circle Counters", ui.getFont());
    toolCirclet.setAction([this]() { setMode(Modes::AddCircleCounters); });
    toolsTile.addButton(toolCirclet);

    Button toolCircle(sf::FloatRect({ windowWidth - RIGHT_PANEL_WIDTH + UI_PADDING, 175.f }, { RIGHT_PANEL_WIDTH - UI_PADDING * 2, 25.f }),
        L"UpdateTextures", ui.getFont());
    toolCircle.setAction([this]() { schemTexture.emplace(schem.get(), &texManager); });
    toolsTile.addButton(toolCircle);

    ui.addTile(toolsTile);

	ui.setActiveTile(0, true);
    ui.setActiveTile(2, true);
}

int Redactor::findCounterAt(const sf::Vector2f& worldPos) const {
    const float threshold = 1.5f;
    for (std::size_t i = 0; i < counters.size(); ++i) {
        auto border = counters[i]->buildBorder();
        for (const auto& p : border) {
            float dx = worldPos.x - static_cast<float>(p.x);
            float dy = worldPos.y - static_cast<float>(p.y);
            if (dx * dx + dy * dy <= threshold * threshold) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void Redactor::setMode(Modes mode) {
    currentMode = mode;
    currentCounter.reset(); // сбрасываем старый

    const wchar_t* modeName = L"None";
    switch (mode) {
    case Modes::AddRectCounters:
        currentCounter = std::make_unique<Counter>(schem.get(), &texManager, counterType::rectangle);
        modeName = L"Rect Counters";
        break;
    case Modes::AddCircleCounters:
        currentCounter = std::make_unique<Counter>(schem.get(), &texManager, counterType::circle);
        modeName = L"Circle Counters";
        break;
    case Modes::AddPolygonCounters:
        currentCounter = std::make_unique<Counter>(schem.get(), &texManager, counterType::polygon);
        modeName = L"Polygon";
        break;
    default:
        modeName = L"Select";
        break;
    }
    setStatusText(std::wstring(L"Mode: ") + std::wstring(modeName));
}

void Redactor::run() {
    while (window.isOpen()) {
        handleEvents();
        draw();
    }
}

void Redactor::handleEvents() {
    sf::Vector2i mousePos;
    sf::Vector2f mousePixel;

    while (const auto event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
            return;
        }
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            windowWidth = resized->size.x;
            windowHeight = resized->size.y;
            uiView = sf::View(sf::FloatRect({ 0.f, 0.f },
                { static_cast<float>(windowWidth), static_cast<float>(windowHeight) }));
            redactorView = sf::View(sf::FloatRect(
                { viewCenter.x - windowWidth / 2.f / zoom, viewCenter.y - windowHeight / 2.f / zoom },
                { static_cast<float>(windowWidth) / zoom, static_cast<float>(windowHeight) / zoom }
            ));
            initUI();
        }
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            float factor = (wheel->delta > 0) ? 1.2f : 1.0f / 1.2f;
            zoom = std::clamp(zoom * factor, 0.0f, 16.0f);
            updateView();
        }
        if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouseButton->button == sf::Mouse::Button::Left) {
                leftMousePressed = true;
                pressStartTime = clickClock.getElapsedTime();
                mousePos = sf::Mouse::getPosition(window);
                mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
                pressPosition = mousePixel;
                if (currentMode != Modes::None) {
                    if (selectedCounterIndex < counters.size())
                        counters[selectedCounterIndex]->setSelected(false);
                    selectedCounterIndex = std::numeric_limits<std::size_t>::max();
                    isDraggingCounter = false;
                }
                else {
                    sf::Vector2f worldPos = window.mapPixelToCoords(sf::Vector2i(mousePos), redactorView);
                    int idx = findCounterAt(worldPos);
                    if (idx >= 0) {
                        draggingCounterIndex = static_cast<std::size_t>(idx);
                        isDraggingCounter = true;
                        dragCounterStartWorld = worldPos;
                        dragStartPoints = counters[idx]->getPoints();

                        counters[idx]->removePlacedBlocks(&*schemTexture);

                        for (auto& c : counters) c->setSelected(false);
                        counters[idx]->setSelected(true);
                        setStatusText(L"Перетаскивание контура...");
                    }
                    else {
                        for (auto& c : counters) c->setSelected(false);
                        selectedCounterIndex = std::numeric_limits<std::size_t>::max();
                        isDraggingCounter = false;
                    }
                    isDragging = false;
                }
            }
        }
        if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonReleased>()) {
            mouseClicked = true;
            if (mouseButton->button == sf::Mouse::Button::Left) {
                if (leftMousePressed) {
                    leftMousePressed = false;
                    sf::Time releaseTime = clickClock.getElapsedTime();
                    sf::Time pressDuration = releaseTime - pressStartTime;

                    mousePos = sf::Mouse::getPosition(window);
                    mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
                    bool uiContains = ui.contains(mousePixel);

                    if (isDraggingCounter && draggingCounterIndex < counters.size()) {
                        counters[draggingCounterIndex]->buildCounter(&*schemTexture);
                        counters[draggingCounterIndex]->setSelected(false);
                        setStatusText(L"Контур перемещён.");

                        isDraggingCounter = false;
                        draggingCounterIndex = std::numeric_limits<std::size_t>::max();
                    }
                    else if (pressDuration < DRAG_THRESHOLD && !uiContains && !isDragging) {
                        handleMapClick(mousePixel);
                    }
                    else if (isDragging) {
                        isDragging = false;
                    }
                }
            }
        }
        if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            if (keyPressed->code == sf::Keyboard::Key::Enter) {
                if (currentMode == Modes::AddPolygonCounters && currentCounter && !currentCounter->isCompleted()) {
                    currentCounter->finish(&*schemTexture);
                    counters.push_back(std::move(currentCounter));
                    currentCounter = std::make_unique<Counter>(schem.get(), &texManager, counterType::polygon);
                    setStatusText(L"Полигон завершён и построен.");
                }
            }
            if (keyPressed->code == sf::Keyboard::Key::Escape) {
                if (currentMode == Modes::AddRectCounters ||
                    currentMode == Modes::AddPolygonCounters ||
                    currentMode == Modes::AddCircleCounters)
                {
                    if (currentCounter && !currentCounter->getPoints().empty()) {
                        currentCounter.reset();
                        setStatusText(L"Текущее построение отменено.");
                    }

                    if (!counters.empty()) {
                        counters.back()->removePlacedBlocks(&*schemTexture);
                        counters.pop_back();
                        setStatusText(L"Последний контур удалён.");
                    }
                }
            }
        }
        if (const auto* moved = event->getIf<sf::Event::MouseMoved>()) {
            sf::Vector2f mouseWorld = window.mapPixelToCoords(
                sf::Vector2i(moved->position.x, moved->position.y), redactorView);
            tempMousePos = mouseWorld;

            if (isDraggingCounter && draggingCounterIndex < counters.size()) {
                sf::Vector2f delta = mouseWorld - dragCounterStartWorld;
                if (delta.x != 0.f || delta.y != 0.f) {
                    counters[draggingCounterIndex]->move(delta);
                    dragCounterStartWorld = mouseWorld;
                }
            }
        }
    }
    mousePos = sf::Mouse::getPosition(window);
    mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));

    ui.update(mousePixel, mouseClicked);

    mouseClicked = false;
    if (leftMousePressed && !ui.contains(mousePixel) && !isDraggingCounter) {
        sf::Time currentTime = clickClock.getElapsedTime();
        sf::Time pressDuration = currentTime - pressStartTime;
        
        // Если нажатие длительное и мы еще не в состоянии перетаскивания
        if (pressDuration >= DRAG_THRESHOLD && !isDragging) {
            isDragging = true;
            // Сохраняем центр вида на момент начала перетаскивания
            dragCenter = viewCenter;
        }
        
        // Если мы в состоянии перетаскивания, обновляем вид
        if (isDragging) {
            sf::Vector2f current(mousePixel);
            sf::Vector2f delta = pressPosition - current;
            viewCenter = dragCenter + delta / zoom;
            updateView();
        }
    }
}

void Redactor::handleMapClick(const sf::Vector2f& windowPixel) {
    sf::Vector2f worldPos = window.mapPixelToCoords(
        sf::Mouse::getPosition(window), redactorView);

    sf::Vector3i pos1 = { 0, 0, 0 };
    sf::Vector3i pos2 = schem->getPos2() - schem->getPos1();
    if (worldPos.x < pos1.x || worldPos.x >= pos2.x || worldPos.y < pos1.z || worldPos.y >= pos2.z) return;

    switch (currentMode) {
    case Modes::AddRectCounters:
    case Modes::AddCircleCounters:
    case Modes::AddPolygonCounters: {
        if (!currentCounter) break;
        currentCounter->addPoint(worldPos);
        if (currentCounter->isCompleted()) {
            currentCounter->buildCounter(&*schemTexture);
            counters.push_back(std::move(currentCounter));
            counterType type;
            switch (currentMode) {
            case Modes::AddRectCounters: type = counterType::rectangle; break;
            case Modes::AddCircleCounters: type = counterType::circle; break;
            case Modes::AddPolygonCounters: type = counterType::polygon; break;
            default: type = counterType::rectangle; break;
            }
            currentCounter = std::make_unique<Counter>(schem.get(), &texManager, type);
            setStatusText(L"Фигура построена.");
        }
        break;
    }
    default:
        break;
    }
}

void Redactor::updateView() {
    float halfWidth = static_cast<float>(windowWidth) / 2.f / zoom;
    float halfHeight = static_cast<float>(windowHeight) / 2.f / zoom;
    redactorView = sf::View(sf::FloatRect(
        { viewCenter.x - halfWidth, viewCenter.y - halfHeight },
        { halfWidth * 2.f, halfHeight * 2.f }
    ));
}

void Redactor::draw() {
    if (tileMap) {
        tileMap->update(redactorView);
    }
    window.clear(MAP_BACKGROUND_COLOR);

    window.setView(redactorView);

	schemTexture->draw(window, sf::RenderStates::Default);

    if (tileMap) {
        tileMap->draw(window, sf::RenderStates::Default);
    }

    for (auto& c : counters) {
        c->drawLinePreview(window);
    }
    if (currentCounter) {
        currentCounter->drawLinePreview(window);
    }

    window.setView(uiView);
    ui.draw(window);

    statusText->setString(statusMessage);
    statusText->setPosition(sf::Vector2f(UI_PADDING, static_cast<float>(windowHeight) - 28.f));
    window.draw(*statusText);

    window.display();
}

Redactor::BuildResult Redactor::buildTexturesImages() {
    BuildResult result;
    sf::Vector3i Pos1 = schem->getPos1();
    sf::Vector3i Pos2 = schem->getPos2();
    int w = Pos2.x - Pos1.x;
    int l = Pos2.z - Pos1.z;
    if (w <= 0 || l <= 0)
        throw std::runtime_error("Invalid schematic dimensions");
    result.width = w;
    result.length = l;
    sf::Image blockImage({ static_cast<unsigned int>(w * 1), static_cast<unsigned int>(l * 1) }, sf::Color(0, 0, 0, 0));
    sf::Image heightImage({ static_cast<unsigned int>(w * 1), static_cast<unsigned int>(l * 1) }, sf::Color::Black);
    
    // Get all blocks in the schematic area for more efficient processing
    auto blocks = schem->getBlocksInArea(Pos1.x, Pos1.y, Pos1.z, Pos2.x, Pos2.y, Pos2.z);
    
    for (const auto& b : blocks) {
        int lx = b.x - Pos1.x;
        int ly = b.y - Pos1.y;
        int lz = b.z - Pos1.z;
        
        // Only process blocks within our bounds (should already be true from getBlocksInArea)
        if (lx < 0 || lx >= w || ly < 0 || ly >= (Pos2.y - Pos1.y) || lz < 0 || lz >= l) {
            continue;
        }
        
        // For simplicity, we're only storing the top block at each x,z position
        // In a more complex implementation, we might want to store multiple layers
        auto it = result.topBlocks.find({lx, lz});
        if (it == result.topBlocks.end() || b.y > it->second) {
            result.topBlocks[{lx, lz}] = b.blockId;
            result.topHeights[{lx, lz}] = b.y;
        }
    }
    
    float maxHeight = 1.0f;
    for (int lx = 0; lx < w; ++lx) {
        for (int lz = 0; lz < l; ++lz) {
            auto it = result.topHeights.find({lx, lz});
            if (it != result.topHeights.end() && it->second > maxHeight)
                maxHeight = static_cast<float>(it->second);
        }
    }
    result.maxHeight = std::max(maxHeight, 1.0f);
    
    for (int lx = 0; lx < w; ++lx) {
        for (int lz = 0; lz < l; ++lz) {
            auto hb = result.topHeights.find({lx, lz});
            auto bb = result.topBlocks.find({lx, lz});
            if (hb == result.topHeights.end() || bb == result.topBlocks.end()) continue;
            int h = hb->second;
            const sf::Image* texImg = texManager.getImage(schem->getPalette().getName(bb->second));
            for (int py = 0; py < 1; ++py) {
                for (int px = 0; px < 1; ++px) {
                    sf::Color pixel = texImg ? texImg->getPixel({ static_cast<unsigned int>(px), static_cast<unsigned int>(py) }) : texManager.getColor(schem->getPalette().getName(bb->second));
                    if (!texImg) pixel.a = 255;
                    unsigned int imgX = static_cast<unsigned int>(lx * 1 + px);
                    unsigned int imgY = static_cast<unsigned int>(lz * 1 + py);
                    blockImage.setPixel({ imgX, imgY }, pixel);
                    float normH = static_cast<float>(h) / result.maxHeight;
                    uint8_t heightValue = static_cast<uint8_t>(std::clamp(normH * 255.f, 0.f, 255.f));
                    heightImage.setPixel({ imgX, imgY }, sf::Color(heightValue, 255, 0));
                }
            }
        }
    }
    result.blockImage = std::move(blockImage);
    result.heightImage = std::move(heightImage);
    return result;
}

void Redactor::showLoadDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn = {};
    char fileName[260] = "";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window.getNativeHandle();
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = sizeof(fileName);
    ofn.lpstrFilter = "Schematic files\0*.schem\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        schem->loadFromFile(fileName);
        // Перестраиваем текстуры после загрузки
        try {
            schemTexture.emplace(schem.get(), &texManager);
        }
        catch (const std::exception& e) {

            statusMessage = L"Error building textures: " + std::wstring(e.what(), e.what() + strlen(e.what()));
        }
    }
#endif
}

void Redactor::setStatusText(const std::wstring& text) {
    statusMessage = text;
}