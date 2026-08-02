#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <functional>

class Menu {
public:
    Menu(int width, int height);
    void run();

private:
    // Структура кнопки
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
    void showLoadDialog();   // вызов диалога выбора файла (Windows)
    void showInfoDialog();   // окно с версией

    sf::RenderWindow window;
    sf::Font font;
    std::vector<Button> buttons;

    // Настройки
    struct Settings {
        std::string lastSchematicPath = "";
        float shadowStrength = 0.35f;
        float initialZoom = 2.0f;
        sf::Color backgroundColor = sf::Color(30, 30, 30);
    } settings;

    // Константы версий
    const std::wstring PROGRAM_VERSION = L"1.0.1";
    const std::wstring MC_VERSION = L"1.21.4";
};