#include "redactor.h"
#include "schematic.h"
#include "GuiManager.h"   // <-- новый заголовок
#include "helper.h"     // <-- для getExeDirectory

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
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
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

static std::vector<sf::Vector2i> bresenhamLine(int x0, int z0, int x1, int z1) {
    std::vector<sf::Vector2i> points;
    int dx = std::abs(x1 - x0);
    int dz = std::abs(z1 - z0);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (z0 < z1) ? 1 : -1;
    int err = dx - dz;

    while (true) {
        points.emplace_back(x0, z0);
        if (x0 == x1 && z0 == z1) break;
        int e2 = 2 * err;
        if (e2 > -dz) { err -= dz; x0 += sx; }
        if (e2 < dx) { err += dx; z0 += sz; }
    }
    return points;
}

// Определение статической константы (только фон карты)
const sf::Color Redactor::MAP_BACKGROUND_COLOR = sf::Color(0, 0, 0);

void Redactor::addRectPoint(const sf::Vector2f& point) {
    switch (rectStage) {
    case RectStage::Idle:
        rectP1 = point;
        rectStage = RectStage::FirstPoint;
        setStatusText(L"Первая точка. Кликните вторую.");
        break;
    case RectStage::FirstPoint:
        rectP2 = point;
        rectStage = RectStage::SecondPoint;
        setStatusText(L"Вторая точка. Кликните третью или нажмите Enter.");
        break;
    case RectStage::SecondPoint:
        buildRectangle(rectP1, rectP2, point);
        rectStage = RectStage::Idle;
        setStatusText(L"Прямоугольник построен.");
        break;
    default:
        break;
    }
}

void Redactor::buildRectangle(const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Vector2f& p3) {
    // 1. Направления и длины (в пикселях / метрах)
    sf::Vector2f dir = p2 - p1;
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1e-6f) {
        setStatusText(L"Нулевая длина линии");
        return;
    }
    dir /= len;
    sf::Vector2f perp(-dir.y, dir.x);

    // Ширина (проекция p3-p2 на перпендикуляр)
    sf::Vector2f toP3 = p3 - p2;
    float w = toP3.x * perp.x + toP3.y * perp.y;
    if (w < 0) {
        perp = -perp;
        w = -w;
    }

    // 2. Округляем длины до целого числа блоков
    int lenBlocks = static_cast<int>(std::round(len));
    int widthBlocks = static_cast<int>(std::round(w));

    if (lenBlocks == 0 || widthBlocks == 0) {
        setStatusText(L"Слишком маленький размер");
        return;
    }

    // 3. Выбираем начальную точку (округляем p1 до целых координат)
    int startX = static_cast<int>(std::floor(p1.x));
    int startZ = static_cast<int>(std::floor(p1.y));

    // 4. Вычисляем координаты углов с целочисленными смещениями
    //    Используем round для приведения направлений к целым шагам
    int c1x = startX, c1z = startZ;
    int c2x = startX + static_cast<int>(std::round(dir.x * lenBlocks));
    int c2z = startZ + static_cast<int>(std::round(dir.y * lenBlocks));
    int c3x = c2x + static_cast<int>(std::round(perp.x * widthBlocks));
    int c3z = c2z + static_cast<int>(std::round(perp.y * widthBlocks));
    int c4x = startX + static_cast<int>(std::round(perp.x * widthBlocks));
    int c4z = startZ + static_cast<int>(std::round(perp.y * widthBlocks));

    // 5. Генерация контура (алгоритм Брезенхема)
    std::vector<sf::Vector2i> borderBlocks;
    auto addLine = [&](int ax, int az, int bx, int bz) {
        auto line = bresenhamLine(ax, az, bx, bz);
        borderBlocks.insert(borderBlocks.end(), line.begin(), line.end());
        };

    addLine(c1x, c1z, c2x, c2z);
    addLine(c2x, c2z, c3x, c3z);
    addLine(c3x, c3z, c4x, c4z);
    addLine(c4x, c4z, c1x, c1z);

    // Удаление дубликатов
    std::sort(borderBlocks.begin(), borderBlocks.end(),
        [](const sf::Vector2i& a, const sf::Vector2i& b) {
            return (a.x < b.x) || (a.x == b.x && a.y < b.y);
        });
    borderBlocks.erase(std::unique(borderBlocks.begin(), borderBlocks.end(),
        [](const sf::Vector2i& a, const sf::Vector2i& b) {
            return a.x == b.x && a.y == b.y;
        }),
        borderBlocks.end());

    // 6. Определение высот (без изменений)
    sf::Vector3i pos1 = schem->getPos1();
    sf::Vector3i pos2 = schem->getPos2();
    int offsetX = pos1.x;
    int offsetZ = pos1.z;
    int yMinGlobal = pos1.y;
    int yMaxGlobal = pos2.y - 1;

    int xMin = std::min({ c1x, c2x, c3x, c4x });
    int xMax = std::max({ c1x, c2x, c3x, c4x });
    int zMin = std::min({ c1z, c2z, c3z, c4z });
    int zMax = std::max({ c1z, c2z, c3z, c4z });

    auto blocks = schem->getBlocksInArea(xMin + offsetX, yMinGlobal, zMin + offsetZ,
        xMax + offsetX, yMaxGlobal, zMax + offsetZ);

    int minHeight = 0, maxHeight = 0;
    if (!blocks.empty()) {
        minHeight = blocks[0].y;
        maxHeight = blocks[0].y;
        for (const auto& b : blocks) {
            minHeight = std::min(minHeight, b.y);
            maxHeight = std::max(maxHeight, b.y);
        }
    }

    int startY = minHeight - 1;
    int endY = maxHeight + 1;

    // 7. Блок (красная шерсть)
    auto& palette = schem->getPalette();
    int blockId = palette.getId("minecraft:red_wool");
    if (blockId < 0) {
        int maxId = -1;
        for (const auto& [id, name] : palette.nameById) {
            if (id > maxId) maxId = id;
        }
        int newId = maxId + 1;
        palette.addBlock(newId, "minecraft:red_wool");
        blockId = newId;
    }

    // 8. Пакетная запись
    std::vector<std::tuple<int, int, int, int>> blocksToSet;
    blocksToSet.reserve(borderBlocks.size() * (endY - startY + 1));

    for (const auto& b : borderBlocks) {
        int absX = b.x + offsetX;
        int absZ = b.y + offsetZ;
        for (int y = startY; y <= endY; ++y) {
            blocksToSet.emplace_back(absX, y, absZ, blockId);
        }
    }

    if (!blocksToSet.empty()) {
        schem->setBlocks(blocksToSet);
        setStatusText(L"Контур построен (" + std::to_wstring(blocksToSet.size()) + L" блоков)");
    }
    else {
        setStatusText(L"Нет блоков для размещения");
    }
}

void Redactor::drawRectPreview(sf::RenderWindow& window, const sf::View& view) {
    if (currentMode != Modes::AddRectCounters) return;
    window.setView(view);

    if (rectStage == RectStage::FirstPoint) {
        sf::Vertex line[] = {
            sf::Vertex(rectP1, sf::Color(255, 255, 0, 128)),
            sf::Vertex(tempMousePos, sf::Color(255, 255, 0, 128))
        };
        window.draw(line, 2, sf::PrimitiveType::Lines);
    }
    else if (rectStage == RectStage::SecondPoint) {
        // Основная линия (красная)
        sf::Vertex line1[] = {
            sf::Vertex(rectP1, sf::Color::Red),
            sf::Vertex(rectP2, sf::Color::Red)
        };
        window.draw(line1, 2, sf::PrimitiveType::Lines);

        // Перпендикуляр от rectP2 к мыши (жёлтый)
        sf::Vector2f dir = rectP2 - rectP1;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1e-6f) {
            dir /= len;
            sf::Vector2f perp(-dir.y, dir.x);
            sf::Vector2f toMouse = tempMousePos - rectP2;
            float w = toMouse.x * perp.x + toMouse.y * perp.y;
            sf::Vector2f projected = rectP2 + perp * w;
            sf::Vertex line2[] = {
                sf::Vertex(rectP2, sf::Color(255, 255, 0, 128)),
                sf::Vertex(projected, sf::Color(255, 255, 0, 128))
            };
            window.draw(line2, 2, sf::PrimitiveType::Lines);
        }
    }
}

// ----------------------- Конструктор -----------------------
Redactor::Redactor(std::unique_ptr<SchematicMap> schematic, int width, int height)
    : schem(std::move(schematic)), windowWidth(width), windowHeight(height),
    window(sf::VideoMode({ static_cast<unsigned int>(width), static_cast<unsigned int>(height) }),
        L"Schematic Redactor", sf::Style::Default) {
    window.setFramerateLimit(60);
    if (!font.openFromFile(getExeDirectory() + "Benbow Regular.ttf")) {
        (void)font.openFromFile("C:/Windows/Fonts/arial.ttf");
    }

    // Initialize tile map for background
    try {
        tileMap.emplace(17, sf::Vector2f{ float(schem->getPos1().x), float(schem->getPos1().z) });
        tileMap->setCustomTileSource("https://tile.buildtheearth.ru/YandexAero/{x}/{y}/{z}");
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize tile map: " << e.what() << std::endl;
        // Continue without tile map background
    }

    redactorView = sf::View(sf::FloatRect({ 0.f, 0.f }, { static_cast<float>(width), static_cast<float>(height) }));
    uiView = sf::View(sf::FloatRect({ 0.f, 0.f }, { static_cast<float>(width), static_cast<float>(height) }));
    viewCenter = { 0.f, 0.f };
    zoom = 1.0f;

    // Initialize mouse interaction state
    leftMousePressed = false;
    isDragging = false;

    startBuildTextures();
    initUI();   // <-- инициализируем новый интерфейс

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
    shadowShader = std::make_unique<sf::Shader>();
    if (!shadowShader->loadFromMemory(shaderCode, sf::Shader::Type::Fragment)) {
        std::cerr << "Shader compilation error!\n";
    }

    // Текстовые поля
    statusText = std::make_unique<sf::Text>(font, L"Loading map...", 14);
    statusText->setFillColor(GuiStyle::TextColor);
    infoText = std::make_unique<sf::Text>(font, L"", 14);
    infoText->setFillColor(GuiStyle::TextColor);
}

// ----------------------- Инициализация UI -----------------------
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

    Button toolCircle(sf::FloatRect({ windowWidth - RIGHT_PANEL_WIDTH + UI_PADDING, 105.f }, { RIGHT_PANEL_WIDTH - UI_PADDING * 2, 25.f }),
        L"UpdateTextures", ui.getFont());
    toolCircle.setAction([this]() { startBuildTextures(); });
    toolsTile.addButton(toolCircle);

    ui.addTile(toolsTile);

    // По умолчанию активна вкладка Tools (индекс 1)
	ui.setActiveTile(0, true);
    ui.setActiveTile(2, true);
}

// ----------------------- Установка режима -----------------------
void Redactor::setMode(Modes mode) {
    currentMode = mode;
    const wchar_t* modeName = L"None";
    switch (mode) {
    case Modes::None:             modeName = L"Select"; break;
    case Modes::AddRectCounters:  modeName = L"Add Rect Counters"; break;
    case Modes::AddCircleCounters:modeName = L"Add Circle Counters"; break;
    }
    setStatusText(std::wstring(L"Mode: ") + modeName);
}

// ----------------------- Главный цикл -----------------------
void Redactor::run() {
    while (window.isOpen()) {
        handleEvents();
        draw();
    }
}

// ----------------------- Обработка событий -----------------------
void Redactor::handleEvents() {
    sf::Vector2i mousePos;
    sf::Vector2f mousePixel;
    
    // Обработка событий окна
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
            initUI(); // пересоздаём UI с новыми размерами
        }
        // можно обработать колёсико мыши здесь
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            float factor = (wheel->delta > 0) ? 1.2f : 1.0f / 1.2f;
            zoom = std::clamp(zoom * factor, 0.0f, 16.0f);
            updateView();
        }
        // Обработка нажатия и释放 мыши
        if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouseButton->button == sf::Mouse::Button::Left) {
                leftMousePressed = true;
                pressStartTime = clickClock.getElapsedTime();
                mousePos = sf::Mouse::getPosition(window);
                mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
                pressPosition = mousePixel;
                
                // Сбрасываем состояние перетаскивания при новом нажатии
                isDragging = false;
            }
        }
        if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouseButton->button == sf::Mouse::Button::Left) {
                if (leftMousePressed) {
                    leftMousePressed = false;
                    sf::Time releaseTime = clickClock.getElapsedTime();
                    sf::Time pressDuration = releaseTime - pressStartTime;
                    
                    mousePos = sf::Mouse::getPosition(window);
                    mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
                    bool uiContains = ui.contains(mousePixel);
                    
                    // Если это короткое нажатие и не по UI
                    if (pressDuration < DRAG_THRESHOLD && !uiContains && !isDragging) {
                        // Выполняем действие в соответствии с текущим режимом
                        handleMapClick(mousePixel);
                    }
                    // Если мы были в состоянии перетаскивания, завершаем его
                    else if (isDragging) {
                        isDragging = false;
                    }
                }
            }
        }

        if (const auto* moved = event->getIf<sf::Event::MouseMoved>()) {
            sf::Vector2f mouseWorld = window.mapPixelToCoords(
                sf::Vector2i(moved->position.x, moved->position.y), redactorView);
            tempMousePos = mouseWorld;
        }
    }

    // Обновляем состояние мыши для UI
    mousePos = sf::Mouse::getPosition(window);
    mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
    bool mouseClicked = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    
    // Обновляем UI
    ui.update(mousePixel, mouseClicked);

    // Обработка перетаскивания карты (после длительного нажатия)
    if (leftMousePressed && !ui.contains(mousePixel)) {
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

// ----------------------- Обработка клика по карте -----------------------
void Redactor::handleMapClick(const sf::Vector2f& windowPixel) {
    // Преобразуем координаты окна в координаты вида
    sf::Vector2f worldPos = window.mapPixelToCoords(
        sf::Mouse::getPosition(window), redactorView);
    
    // Преобразуем мировые координаты в координаты схемы
    // Предполагаем, что каждый блок составляет 1 единицу, и схема начинается от (0,0,0)
    // Для простоты используем приближенное преобразование
    int schematicX = static_cast<int>(std::floor(worldPos.x));
    int schematicY = static_cast<int>(std::floor(worldPos.y));
    int schematicZ = 0; // Упрощение - работаем только с одним Y-уровнем
    
    // Получаем размеры схемы для проверки границ
    sf::Vector3i pos1 = { 0, 0, 0 };
    sf::Vector3i pos2 = schem->getPos2() - schem->getPos1();
    
    // Проверяем, что клик находится внутри границ схемы
    if (schematicX < pos1.x || schematicX >= pos2.x ||
        schematicY < pos1.z || schematicY >= pos2.z) {
        return; // Клик вне схемы
    }
    
    // В зависимости от режима размещаем соответствующий объект
    switch (currentMode) {
    case Modes::AddRectCounters: {
        sf::Vector2f worldPos = window.mapPixelToCoords(
            sf::Mouse::getPosition(window), redactorView);
        addRectPoint(worldPos);
        break;
    }
    case Modes::AddCircleCounters: {
        // Для простоты размещаем один блок (в реальности круг)
        schem->setBlock(schematicX, schematicY, schematicZ, 41);
        setStatusText(L"Placed circular counter at (" + 
                      std::to_wstring(schematicX) + L", " + 
                      std::to_wstring(schematicY) + L")");
        break;
    }
    default:
        // Для остальных режимов ничего не делаем при клике
        break;
    }
}

// ----------------------- Обновление вида -----------------------
void Redactor::updateView() {
    float halfWidth = static_cast<float>(windowWidth) / 2.f / zoom;
    float halfHeight = static_cast<float>(windowHeight) / 2.f / zoom;
    redactorView = sf::View(sf::FloatRect(
        { viewCenter.x - halfWidth, viewCenter.y - halfHeight },
        { halfWidth * 2.f, halfHeight * 2.f }
    ));
}

// ----------------------- Отрисовка -----------------------
void Redactor::draw() {
    tileMap->update(redactorView);
    pollBuildTextures();
    window.clear(MAP_BACKGROUND_COLOR);

    // Рисуем карту
    window.setView(redactorView);
    if (buildReady && renderSprite) {
        if (shadowShader && sf::Shader::isAvailable()) {
            shadowShader->setUniform("blockTexture", blockColorTexture);
            shadowShader->setUniform("heightTexture", heightTexture);
            shadowShader->setUniform("maxHeight", maxHeight);
            shadowShader->setUniform("shadowStrength", shadowStrength);
            shadowShader->setUniform("blockPixelSize", blockPixelSize);
            shadowShader->setUniform("maxDiff", maxDiff);
            shadowShader->setUniform("zoom", zoom);
            shadowShader->setUniform("textureThreshold", textureThreshold);
            window.draw(*renderSprite, sf::RenderStates(shadowShader.get()));
        }
        else {
            window.draw(*renderSprite);
        }
    }
    tileMap->draw(window, sf::RenderStates::Default);

    drawRectPreview(window, redactorView);

    // Рисуем UI поверх карты
    window.setView(uiView);
    ui.draw(window);

    // Статусная строка
    statusText->setString(statusMessage);
    statusText->setPosition(sf::Vector2f(UI_PADDING, static_cast<float>(windowHeight) - 28.f));
    window.draw(*statusText);

    window.display();
}

// ----------------------- Остальные методы (без изменений) -----------------------
void Redactor::loadTextures() {
    std::string exeDir = getExeDirectory();
    std::string texturesPath = exeDir + "textures";
    if (!std::filesystem::exists(texturesPath)) return;
    for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
        if (entry.path().extension() == ".png") {
            sf::Image img;
            if (img.loadFromFile(entry.path().string())) {
                textureImages[entry.path().stem().string()] = std::move(img);
            }
        }
    }
}

sf::Image* Redactor::getBlockTexture(int blockId) {
    if (blockId < 0) return nullptr;
    if (!schem->getPalette().hasBlock(blockId)) return nullptr;
    std::string fullName = schem->getPalette().getName(blockId);
    std::replace(fullName.begin(), fullName.end(), ':', '_');
    auto it = textureImages.find(fullName);
    if (it != textureImages.end()) return &it->second;
    const std::string prefix = "minecraft_";
    if (fullName.rfind(prefix, 0) == 0) {
        std::string shortName = fullName.substr(prefix.size());
        it = textureImages.find(shortName);
        if (it != textureImages.end()) return &it->second;
    }
    return nullptr;
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
            sf::Image* texImg = getBlockTexture(bb->second);
            for (int py = 0; py < 1; ++py) {
                for (int px = 0; px < 1; ++px) {
                    sf::Color pixel = texImg ? texImg->getPixel({ static_cast<unsigned int>(px), static_cast<unsigned int>(py) }) : schem->getBlockColor(bb->second);
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

void Redactor::applyBuildResult(BuildResult&& result) {
    heightTexture.loadFromImage(result.heightImage);
    blockColorTexture.loadFromImage(result.blockImage);
    buildWidth = result.width;
    buildLength = result.length;
    maxHeight = std::max(result.maxHeight, 1.0f);
    topBlocks = std::move(result.topBlocks);
    topHeights = std::move(result.topHeights);
    renderSprite = std::optional<sf::Sprite>(blockColorTexture);
    buildReady = true;
    statusMessage = L"Map ready";
}

void Redactor::startBuildTextures() {
    loadTextures();
    buildStarted = true;
    buildReady = false;
    buildFailed = false;
    statusMessage = L"Building texture...";
    buildFuture = std::async(std::launch::async, [this]() {
        return buildTexturesImages();
        });
}

void Redactor::pollBuildTextures() {
    if (!buildStarted || buildReady || buildFailed) return;
    if (!buildFuture.valid()) return;
    if (buildFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try {
            applyBuildResult(buildFuture.get());
        }
        catch (const std::exception& e) {
            buildFailed = true;
            statusMessage = L"Error: " + std::wstring(e.what(), e.what() + strlen(e.what()));
        }
    }
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
            applyBuildResult(buildTexturesImages());
        }
        catch (const std::exception& e) {
            buildFailed = true;
            statusMessage = L"Error building textures: " + std::wstring(e.what(), e.what() + strlen(e.what()));
        }
    }
#endif
}

void Redactor::showSaveDialog() {
    // TODO
}

void Redactor::setStatusText(const std::wstring& text) {
    statusMessage = text;
}