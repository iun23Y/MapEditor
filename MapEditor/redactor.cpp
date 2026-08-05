#include "redactor.h"
#include "schematic.h"

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

const sf::Color Redactor::UI_PANEL_BACKGROUND = sf::Color(40, 40, 40, 220);
const sf::Color Redactor::UI_PANEL_BORDER = sf::Color(220, 220, 220, 180);
const sf::Color Redactor::UI_BUTTON_BACKGROUND = sf::Color(70, 70, 90, 0);
const sf::Color Redactor::UI_BUTTON_HOVER = sf::Color(30, 110, 200, 255);
const sf::Color Redactor::UI_TEXT_COLOR = sf::Color(240, 240, 240);
const sf::Color Redactor::UI_OVERLAY_COLOR = sf::Color(255, 255, 255, 32);
const sf::Color Redactor::MAP_BACKGROUND_COLOR = sf::Color(0, 0, 0);

static std::string getExeDirectory() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t last = path.find_last_of("\\/");
    if (last != std::string::npos) path = path.substr(0, last + 1);
    return path;
}

Redactor::Redactor(std::unique_ptr<SchematicMap> schematic, int width, int height)
    : schem(std::move(schematic)), windowWidth(width), windowHeight(height), window(sf::VideoMode({ static_cast<unsigned int>(width), static_cast<unsigned int>(height) }), L"Schematic Redactor", sf::Style::Default) {
    window.setFramerateLimit(60);
    if (!font.openFromFile(getExeDirectory() + "Benbow Regular.ttf")) {
        (void)font.openFromFile("C:/Windows/Fonts/arial.ttf");
    }

    redactorView = sf::View(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(static_cast<float>(width), static_cast<float>(height))));
    uiView = sf::View(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(static_cast<float>(width), static_cast<float>(height))));
    viewCenter = { 0.f, 0.f };
    zoom = 1.0f;
    startBuildTextures();
    initUI();

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

    statusText = std::make_unique<sf::Text>(font, L"Loading map...", 14);
    statusText->setFillColor(UI_TEXT_COLOR);
    infoText = std::make_unique<sf::Text>(font, L"", 14);
    infoText->setFillColor(UI_TEXT_COLOR);
}

void Redactor::initUI() {

    //topbar

    topButtons.clear();
    fileMenuButtons.clear();
    rightButtons.clear();

    float x = UI_PADDING;
    float y = 4.f;
    float buttonWidth = 50.f;

    auto addTop = [&](const std::wstring& label, std::function<void()> action) {
        topButtons.push_back({ sf::FloatRect(sf::Vector2f(x, y), sf::Vector2f(buttonWidth, BUTTON_HEIGHT)), label, action, UI_BUTTON_BACKGROUND, UI_BUTTON_HOVER, false });
        x += buttonWidth + UI_PADDING;
    };
    addTop(L"File", [this]() { fileMenuOpen = !fileMenuOpen; });
    addTop(L"Tools", []() {});
    addTop(L"View", []() {});

	//File-menu

    float menuX = UI_PADDING * 2;
    float menuY = TOP_BAR_HEIGHT + UI_PADDING / 2;
    float menuW = 140.f;

    fileMenuButtons.push_back({ sf::FloatRect(sf::Vector2f(menuX, menuY), sf::Vector2f(menuW, BUTTON_HEIGHT)), L"Load", [this]() { showLoadDialog(); fileMenuOpen = false; }, UI_BUTTON_BACKGROUND, UI_BUTTON_HOVER, false });
    fileMenuButtons.push_back({ sf::FloatRect(sf::Vector2f(menuX, menuY + BUTTON_HEIGHT + 4.f), sf::Vector2f(menuW, BUTTON_HEIGHT)), L"Save", [this]() { showSaveDialog(); fileMenuOpen = false; }, UI_BUTTON_BACKGROUND, UI_BUTTON_HOVER, false });

    float rightX = static_cast<float>(windowWidth) - RIGHT_PANEL_WIDTH;
    float rightY = TOP_BAR_HEIGHT;
    float rightW = RIGHT_PANEL_WIDTH - UI_PADDING * 2;
    rightButtons.push_back({ sf::FloatRect(sf::Vector2f(rightX + UI_PADDING, rightY + BUTTON_HEIGHT + 8.f), sf::Vector2f(rightW, BUTTON_HEIGHT)), L"Tool 1", []() {}, UI_BUTTON_BACKGROUND, UI_BUTTON_HOVER, false });
    rightButtons.push_back({ sf::FloatRect(sf::Vector2f(rightX + UI_PADDING, rightY + 2 * (BUTTON_HEIGHT + 8.f)), sf::Vector2f(rightW, BUTTON_HEIGHT)), L"Tool 2", []() {}, UI_BUTTON_BACKGROUND, UI_BUTTON_HOVER, false });
}

void Redactor::run() {
    while (window.isOpen()) {
        handleEvent();
        draw();
    }
}

void Redactor::handleEvent() {
    while (const auto event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
            return;
        }

        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            windowWidth = resized->size.x;
            windowHeight = resized->size.y;
            uiView = sf::View(sf::FloatRect(sf::Vector2f(0.f, 0.f), sf::Vector2f(static_cast<float>(windowWidth), static_cast<float>(windowHeight))));
            redactorView = sf::View(sf::FloatRect(sf::Vector2f(viewCenter.x - windowWidth / 2.f / zoom, viewCenter.y - windowHeight / 2.f / zoom), sf::Vector2f(static_cast<float>(windowWidth) / zoom, static_cast<float>(windowHeight) / zoom)));
            initUI();
        }

        if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
            sf::Vector2f mouse(static_cast<float>(mouseMoved->position.x), static_cast<float>(mouseMoved->position.y));
            handleUIEvent(mouse);
            if (dragging) {
                sf::Vector2f current(mouse);
                sf::Vector2f delta = dragStart - current;
                viewCenter = dragCenter + delta / zoom;
                updateView();
            }
            return;
        }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            sf::Vector2f mouse(static_cast<float>(mousePressed->position.x), static_cast<float>(mousePressed->position.y));
            if (mousePressed->button == sf::Mouse::Button::Left) {
                handleUIEvent(mouse);
                if (!fileMenuOpen) {
                    dragging = true;
                    dragStart = mouse;
                    dragCenter = viewCenter;
                }
            }
            return;
        }

        if (const auto* mouseReleased = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouseReleased->button == sf::Mouse::Button::Left) {
                dragging = false;
            }
            return;
        }

        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            float factor = (wheel->delta > 0) ? 1.2f : 1.0f / 1.2f;
            zoom = std::clamp(zoom * factor, 0.25f, 16.f);
            updateView();
            return;
        }
    }
}

void Redactor::handleUIEvent(const sf::Vector2f& mouse) {
    for (auto& btn : topButtons) {
        btn.hovered = btn.rect.contains(mouse);
    }
    for (auto& btn : fileMenuButtons) {
        btn.hovered = btn.rect.contains(mouse);
    }
    for (auto& btn : rightButtons) {
        btn.hovered = btn.rect.contains(mouse);
    }

    if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
        for (auto& btn : topButtons) {
            if (btn.hovered && btn.action) btn.action();
        }
        if (fileMenuOpen) {
            for (auto& btn : fileMenuButtons) {
                if (btn.hovered && btn.action) btn.action();
            }
        }
        for (auto& btn : rightButtons) {
            if (btn.hovered && btn.action) btn.action();
        }
    }
}

void Redactor::updateView() {
    float halfWidth = static_cast<float>(windowWidth) / 2.f / zoom;
    float halfHeight = static_cast<float>(windowHeight) / 2.f / zoom;
    redactorView = sf::View(sf::FloatRect({ viewCenter.x - halfWidth, viewCenter.y - halfHeight }, { halfWidth * 2.f, halfHeight * 2.f }));
}

void Redactor::drawButton(const UIButton& button) {
    sf::RectangleShape shape(sf::Vector2f(button.rect.size.x, button.rect.size.y));
    shape.setPosition(sf::Vector2f(button.rect.position.x, button.rect.position.y));
    shape.setFillColor(button.hovered ? button.hoverColor : button.color);
    window.draw(shape);
    sf::Text text(font, button.label, 14);
    text.setFillColor(UI_TEXT_COLOR);
    auto bounds = text.getLocalBounds();
    text.setPosition(sf::Vector2f(button.rect.position.x + (button.rect.size.x - bounds.size.x) / 2.f - bounds.position.x,
                     button.rect.position.y + (button.rect.size.y - bounds.size.y) / 2.f - bounds.position.y));
    window.draw(text);
}

void Redactor::draw() {
    pollBuildTextures();
    window.clear(MAP_BACKGROUND_COLOR);

    window.setView(redactorView);
    if (renderSprite) {
        if (shadowShader && sf::Shader::isAvailable()) {
            shadowShader->setUniform("blockTexture", blockColorTexture);
            shadowShader->setUniform("heightTexture", heightTexture);
            shadowShader->setUniform("maxHeight", maxHeight);
            shadowShader->setUniform("shadowStrength", 0.35f);
            shadowShader->setUniform("blockPixelSize", 16.f);
            shadowShader->setUniform("maxDiff", 3.0f);
            shadowShader->setUniform("zoom", zoom);
            shadowShader->setUniform("textureThreshold", 0.6f);
            window.draw(*renderSprite, sf::RenderStates(shadowShader.get()));
        }
        else {
            window.draw(*renderSprite);
        }
    }

    window.setView(uiView);

    sf::RectangleShape topBar(sf::Vector2f(static_cast<float>(windowWidth), TOP_BAR_HEIGHT));
    topBar.setPosition(sf::Vector2f(0.f, 0.f));
    topBar.setFillColor(UI_PANEL_BACKGROUND);
    topBar.setOutlineColor(UI_PANEL_BORDER);
    topBar.setOutlineThickness(1.f);
    window.draw(topBar);

    for (const auto& btn : topButtons) drawButton(btn);
    if (fileMenuOpen) {
        sf::RectangleShape menuBg(sf::Vector2f(160.f, 2.f + fileMenuButtons.size() * (BUTTON_HEIGHT) + UI_PADDING));
        menuBg.setPosition(sf::Vector2f(UI_PADDING, TOP_BAR_HEIGHT+2));
        menuBg.setFillColor(UI_PANEL_BACKGROUND);
        menuBg.setOutlineColor(UI_PANEL_BORDER);
        menuBg.setOutlineThickness(1.f);
        window.draw(menuBg);
        for (const auto& btn : fileMenuButtons) drawButton(btn);
    }

    sf::RectangleShape rightPanel(sf::Vector2f(RIGHT_PANEL_WIDTH, static_cast<float>(windowHeight) - TOP_BAR_HEIGHT));
    rightPanel.setPosition(sf::Vector2f(static_cast<float>(windowWidth) - RIGHT_PANEL_WIDTH, TOP_BAR_HEIGHT + 2));
    rightPanel.setFillColor(UI_PANEL_BACKGROUND);
    rightPanel.setOutlineColor(UI_PANEL_BORDER);
    rightPanel.setOutlineThickness(1.f);
    window.draw(rightPanel);
    for (const auto& btn : rightButtons) drawButton(btn);

    sf::Text title(font, L"Editor", 16);
    title.setFillColor(UI_TEXT_COLOR);
    title.setPosition(sf::Vector2f(static_cast<float>(windowWidth) - RIGHT_PANEL_WIDTH/2 - 19, TOP_BAR_HEIGHT + 10.f));
    window.draw(title);

    statusText->setString(statusMessage);
    statusText->setPosition(sf::Vector2f(UI_PADDING, static_cast<float>(windowHeight) - 28.f));
    window.draw(*statusText);

    window.display();
}

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
    sf::Image blockImage({ static_cast<unsigned int>(w * 16), static_cast<unsigned int>(l * 16) }, sf::Color(0, 0, 0, 0));
    sf::Image heightImage({ static_cast<unsigned int>(w * 16), static_cast<unsigned int>(l * 16) }, sf::Color::Black);
    for (int y = Pos1.y; y < Pos2.y; ++y) {
        for (int rx = (Pos1.x >= 0 ? Pos1.x / 1000 : (Pos1.x - 999) / 1000) * 1000;
             rx <= ((Pos2.x - 1) >= 0 ? (Pos2.x - 1) / 1000 : ((Pos2.x - 1) - 999) / 1000) * 1000;
             rx += 1000) {
            for (int rz = (Pos1.z >= 0 ? Pos1.z / 1000 : (Pos1.z - 999) / 1000) * 1000;
                 rz <= ((Pos2.z - 1) >= 0 ? (Pos2.z - 1) / 1000 : ((Pos2.z - 1) - 999) / 1000) * 1000;
                 rz += 1000) {
                auto regionBlocks = schem->getRegionBlocks(rx, y, rz);
                for (const auto& b : regionBlocks) {
                    if (b.x < Pos1.x || b.x >= Pos2.x || b.z < Pos1.z || b.z >= Pos2.z)
                        continue;
                    int lx = b.x - Pos1.x;
                    int lz = b.z - Pos1.z;
                    result.topBlocks[{lx, lz}] = b.blockId;
                    result.topHeights[{lx, lz}] = b.y;
                }
            }
        }
    }
    float maxHeight = 1.0f;
    for (int lx = 0; lx < w; ++lx) {
        for (int lz = 0; lz < l; ++lz) {
            auto it = result.topHeights.find({ lx, lz });
            if (it != result.topHeights.end() && it->second > maxHeight)
                maxHeight = static_cast<float>(it->second);
        }
    }
    result.maxHeight = std::max(maxHeight, 1.0f);
    for (int lx = 0; lx < w; ++lx) {
        for (int lz = 0; lz < l; ++lz) {
            auto hb = result.topHeights.find({ lx, lz });
            auto bb = result.topBlocks.find({ lx, lz });
            if (hb == result.topHeights.end() || bb == result.topBlocks.end()) continue;
            int h = hb->second;
            sf::Image* texImg = getBlockTexture(bb->second);
            for (int py = 0; py < 16; ++py) {
                for (int px = 0; px < 16; ++px) {
                    sf::Color pixel = texImg ? texImg->getPixel({ static_cast<unsigned int>(px), static_cast<unsigned int>(py) }) : schem->getBlockColor(bb->second);
                    if (!texImg) pixel.a = 255;
                    unsigned int imgX = static_cast<unsigned int>(lx * 16 + px);
                    unsigned int imgY = static_cast<unsigned int>(lz * 16 + py);
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
    buildWidth = result.width;
    buildLength = result.length;
    maxHeight = std::max(result.maxHeight, 1.0f);
    topBlocks = std::move(result.topBlocks);
    topHeights = std::move(result.topHeights);
    if (!blockColorTexture.loadFromImage(result.blockImage))
        throw std::runtime_error("Failed to load block texture");
    if (!heightTexture.loadFromImage(result.heightImage))
        throw std::runtime_error("Failed to load height texture");
    if (!renderSprite) renderSprite.emplace(blockColorTexture);
    else renderSprite->setTexture(blockColorTexture, true);
    updateView();
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
        // TODO: implement loading in redactor later
    }
#endif
}

void Redactor::showSaveDialog() {
    // TODO: add schematic export/save
}

void Redactor::setStatusText(const std::wstring& text) {
    statusMessage = text;
}
