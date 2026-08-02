#include "viewer.h"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <windows.h> // для GetModuleFileNameA
#include <map>

std::string getExeDirectory()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t last = path.find_last_of("\\/");
    if (last != std::string::npos) path = path.substr(0, last + 1);
    return path;
}

SchematicViewer::SchematicViewer(const SchematicMap& sm, int w, int h)
    : schem(sm), windowWidth(w), windowHeight(h) {
    loadTextures();
    buildTextures();

    sf::ContextSettings settings;
    settings.antiAliasingLevel = 16;
    window.create(sf::VideoMode({ static_cast<unsigned int>(windowWidth),
                                 static_cast<unsigned int>(windowHeight) }),
        L"Schematic Viewer",
        sf::State::Windowed,
        settings);
    window.setFramerateLimit(60);

    if (!font.openFromFile("Benbow Regular.ttf"))
        font.openFromFile("C:/Windows/Fonts/arial.ttf");

    renderSprite = std::make_unique<sf::Sprite>(blockColorTexture);
    infoText = std::make_unique<sf::Text>(font);
    coordText = std::make_unique<sf::Text>(font);

    infoText->setCharacterSize(14);
    infoText->setFillColor(sf::Color::White);
    coordText->setCharacterSize(14);
    coordText->setFillColor(sf::Color::White);

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
	gl_FragColor = blockColor * brightness;
}
)";

    shadowShader = std::make_unique<sf::Shader>();
    if (!shadowShader->loadFromMemory(shaderCode, sf::Shader::Type::Fragment)) {
        std::cerr << "Ошибка компиляции шейдера!\n";
    }

    viewOffset.x = (static_cast<float>(blockColorTexture.getSize().x) - windowWidth / zoom) / 2.0f;
    viewOffset.y = (static_cast<float>(blockColorTexture.getSize().y) - windowHeight / zoom) / 2.0f;
    updateSprites();
}

void SchematicViewer::loadTextures()
{
    std::string exeDir = getExeDirectory();
    std::string texturesPath = exeDir + "textures";

    std::cout << "Ищем текстуры в: " << texturesPath << std::endl;

    try {
        if (!std::filesystem::exists(texturesPath)) {
            std::cerr << "Папка textures не найдена по пути: " << texturesPath << std::endl;
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
            if (entry.path().extension() == ".png") {
                std::string name = entry.path().stem().string();
                sf::Image img;
                if (img.loadFromFile(entry.path().string())) {
                    textureImages[std::move(name)] = std::move(img);
                    std::cout << "Загружена текстура: " << entry.path().filename().string() << std::endl;
                }
                else {
                    std::cerr << "Не удалось загрузить: " << entry.path().filename().string() << std::endl;
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Ошибка доступа к папке textures: " << e.what() << std::endl;
    }
}

sf::Image* SchematicViewer::getBlockTexture(int blockId)
{
    if (blockId < 0) return nullptr;
    auto itPal = schem.palette.find(blockId);
    if (itPal == schem.palette.end()) return nullptr;

    std::string fullName = itPal->second;
    std::replace(fullName.begin(), fullName.end(), ':', '_');

    auto itTex = textureImages.find(fullName);
    if (itTex != textureImages.end()) return &itTex->second;

    const std::string prefix = "minecraft_";
    if (fullName.compare(0, prefix.size(), prefix) == 0) {
        std::string shortName = fullName.substr(prefix.size());
        itTex = textureImages.find(shortName);
        if (itTex != textureImages.end()) return &itTex->second;
    }
    return nullptr;
}

void SchematicViewer::buildTextures()
{
    int w = schem.width;
    int l = schem.length;
    int h = schem.height;

    sf::Image blockImage({ static_cast<unsigned int>(w * BLOCK_PIXEL_SIZE),
                           static_cast<unsigned int>(l * BLOCK_PIXEL_SIZE) },
        sf::Color(100, 100, 100));
    sf::Image heightImage({ static_cast<unsigned int>(w * BLOCK_PIXEL_SIZE),
                            static_cast<unsigned int>(l * BLOCK_PIXEL_SIZE) },
        sf::Color::Black);

    topBlocks.clear();
    topHeights.clear();

    for (int x = 0; x < w; ++x) {
        for (int z = 0; z < l; ++z) {
            int topHeight = -1;
            int blockId = -1;

            for (int y = 0; y < h; ++y) {
                const sf::Vector3i key{ x + schem.offsetX, y + schem.offsetY, z + schem.offsetZ };
                const auto it = schem.blocks.find(key);
                if (it != schem.blocks.end()) {
                    topHeight = y;
                    blockId = it->second;
                }
            }

            if (topHeight < 0 || blockId < 0) {
                continue;
            }

            topBlocks[{x, z}] = blockId;
            topHeights[{x, z}] = topHeight;
        }
    }

    maxHeight = 0.0f;
    for (int x = 0; x < w; ++x) {
        for (int z = 0; z < l; ++z) {
            const auto itHeight = topHeights.find({x, z});
            if (itHeight != topHeights.end() && itHeight->second > maxHeight) {
                maxHeight = static_cast<float>(itHeight->second);
            }
        }
    }
    if (maxHeight < 1.0f) maxHeight = 1.0f;

    for (int x = 0; x < w; ++x) {
        for (int z = 0; z < l; ++z) {
            const auto itHeight = topHeights.find({x, z});
            const auto itBlock = topBlocks.find({x, z});
            if (itHeight == topHeights.end() || itBlock == topBlocks.end()) {
                continue;
            }

            int h = itHeight->second;
            if (h < 0) continue;

            sf::Image* texImg = getBlockTexture(itBlock->second);

            for (int py = 0; py < BLOCK_PIXEL_SIZE; ++py) {
                for (int px = 0; px < BLOCK_PIXEL_SIZE; ++px) {
                    sf::Color pixelCol;
                    if (texImg) {
                        pixelCol = texImg->getPixel({ static_cast<unsigned int>(px),
                                                    static_cast<unsigned int>(py) });
                    }
                    else {
                        pixelCol = getBlockColor(itBlock->second, schem.palette);
                    }

                    unsigned int imgX = static_cast<unsigned int>(x * BLOCK_PIXEL_SIZE + px);
                    unsigned int imgY = static_cast<unsigned int>(z * BLOCK_PIXEL_SIZE + py);

                    blockImage.setPixel({ imgX, imgY }, pixelCol);

                    float normH = static_cast<float>(h) / std::max(1.0f, float(maxHeight));
                    uint8_t heightValue = static_cast<uint8_t>(std::clamp(normH * 255.0f, 0.0f, 255.0f));
                    heightImage.setPixel({ imgX, imgY }, sf::Color(heightValue, 255, 0));
                }
            }
        }
    }

    blockColorTexture.loadFromImage(blockImage);
    heightTexture.loadFromImage(heightImage);
    heightImage.saveToFile("height_debug.png");
    if (zoom < 0.75f) {
        blockColorTexture.setSmooth(true);
    }
}

void SchematicViewer::updateSprites()
{
    renderSprite->setTexture(blockColorTexture);
    renderSprite->setScale({ zoom, zoom });
    renderSprite->setPosition({ -viewOffset.x * zoom, -viewOffset.y * zoom });
}

void SchematicViewer::handleEvent()
{
    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) window.close();
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            windowWidth = resized->size.x;
            windowHeight = resized->size.y;
            window.setView(sf::View(sf::FloatRect({ 0, 0 },
                { static_cast<float>(windowWidth),
                  static_cast<float>(windowHeight) })));
        }
        if (const auto* mouseWheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            float delta = mouseWheel->delta;
            sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
            float mapX = viewOffset.x + mousePos.x / zoom;
            float mapY = viewOffset.y + mousePos.y / zoom;
            float factor = (delta > 0) ? 1.2f : 1.0f / 1.2f;
            float newZoom = std::max(0.001f, zoom * factor);
            viewOffset.x = mapX - mousePos.x / newZoom;
            viewOffset.y = mapY - mousePos.y / newZoom;
            zoom = newZoom;
            updateSprites();
        }
        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                dragging = true;
                dragStart = sf::Vector2f(static_cast<float>(mousePressed->position.x),
                    static_cast<float>(mousePressed->position.y));
                dragViewStart = viewOffset;
            }
        }
        if (const auto* mouseReleased = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouseReleased->button == sf::Mouse::Button::Left) dragging = false;
        }
        if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
            if (dragging) {
                sf::Vector2f mousePos(static_cast<float>(mouseMoved->position.x),
                    static_cast<float>(mouseMoved->position.y));
                sf::Vector2f delta = mousePos - dragStart;
                viewOffset.x = dragViewStart.x - delta.x / zoom;
                viewOffset.y = dragViewStart.y - delta.y / zoom;
                updateSprites();
            }
        }
    }
}

void SchematicViewer::draw()
{
    window.clear(sf::Color(30, 30, 30));

    if (shadowShader && shadowShader->isAvailable()) {
        shadowShader->setUniform("blockTexture", blockColorTexture);
        shadowShader->setUniform("heightTexture", heightTexture);
        shadowShader->setUniform("maxHeight", static_cast<float>(maxHeight));
        shadowShader->setUniform("shadowStrength", 0.35f);
        shadowShader->setUniform("blockPixelSize", static_cast<float>(BLOCK_PIXEL_SIZE));
        shadowShader->setUniform("maxDiff", 3.0f);
        shadowShader->setUniform("zoom", zoom);
        shadowShader->setUniform("textureThreshold", 0.6f);

        window.draw(*renderSprite, sf::RenderStates(shadowShader.get()));
    }
    else {
        window.draw(*renderSprite);
    }

    infoText->setString("Zoom: " + std::to_string(zoom) + "x  |  Drag: LMB | Zoom: Scroll");
    infoText->setPosition({ 10, static_cast<float>(windowHeight - 30) });
    window.draw(*infoText);

    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
    float mapX = viewOffset.x + static_cast<float>(mousePos.x) / zoom;
    float mapZ = viewOffset.y + static_cast<float>(mousePos.y) / zoom;
    int ix = static_cast<int>(std::floor(mapX / BLOCK_PIXEL_SIZE));
    int iz = static_cast<int>(std::floor(mapZ / BLOCK_PIXEL_SIZE));

    if (ix >= 0 && ix < schem.width && iz >= 0 && iz < schem.length) {
        const auto blockIt = topBlocks.find({ix, iz});
        const auto heightIt = topHeights.find({ix, iz});
        if (blockIt != topBlocks.end() && heightIt != topHeights.end()) {
            int bid = blockIt->second;
            int height = heightIt->second;
            std::string name = "";
            if (bid >= 0) {
                auto it = schem.palette.find(bid);
                if (it != schem.palette.end()) name = it->second;
            }
            coordText->setString("X:" + std::to_string(ix + schem.offsetX) + " Z:" + std::to_string(iz + schem.offsetZ) +
                "  Y:" + std::to_string(height + schem.offsetY) + "  Block: " + name);
            coordText->setPosition({ 10, 10 });
            window.draw(*coordText);
        }
    }

    window.display();
}

void SchematicViewer::run()
{
    while (window.isOpen()) {
        handleEvent();
        draw();
    }
}