#pragma once

#include "GuiManager.h"

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <functional>
#include <future>
#include <memory>

class SchematicMap;

class Menu {
public:
    Menu(int width, int height);
    void run();

private:

    void handleEvents();
    void update();
    void draw();
    void loadSettings();
    void showLoadDialog();
    void showInfoDialog();
    void startLoad(const std::string& path);
    void processLoad();

    sf::RenderWindow window;
    GuiManager ui;

	sf::Image imageIcon;
	sf::Texture iconTexture;
    sf::Texture backGround;

    // Settings
    struct Settings {
        std::string lastSchematicPath = "";
        float shadowStrength = 0.35f;
        float initialZoom = 2.0f;
        sf::Color backgroundColor = sf::Color(30, 30, 30);
    } settings;

    // Version constants
    const std::wstring PROGRAM_VERSION = L"2.0.0";
    const std::wstring MC_VERSION = L"1.12.2 - 1.21.4";

    bool isLoading = false;
    std::future<std::unique_ptr<SchematicMap>> loadFuture;
    std::unique_ptr<SchematicMap> loadedSchematic;
    std::string loadingMessage;
};