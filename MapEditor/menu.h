#pragma once

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
    // Button structure
    struct Button {
        sf::FloatRect rect;
        std::wstring label;
        std::function<void()> action;
        sf::Color color;
        sf::Color hoverColor;
        bool hovered = false;
    };

    void handleEvents();
    void update();
    void draw();
    void loadSettings();
    void saveSettings() const;
    void showLoadDialog();   // file open dialog (Windows)
    void showInfoDialog();   // version info window
    void startLoad(const std::string& path);
    void processLoad();

    sf::RenderWindow window;
    sf::Font font;
    std::vector<Button> buttons;

    // Settings
    struct Settings {
        std::string lastSchematicPath = "";
        float shadowStrength = 0.35f;
        float initialZoom = 2.0f;
        sf::Color backgroundColor = sf::Color(30, 30, 30);
    } settings;

    // Version constants
    const std::wstring PROGRAM_VERSION = L"1.1.0";
    const std::wstring MC_VERSION = L"1.12.2 - 1.21.4";

    bool isLoading = false;
    std::future<std::unique_ptr<SchematicMap>> loadFuture;
    std::unique_ptr<SchematicMap> loadedSchematic;
    std::string loadingMessage;
};