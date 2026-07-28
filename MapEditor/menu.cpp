#include "Menu.h"
#include "schematic.h"
#include "viewer.h"
#include <iostream>
#include <fstream>
#include <sstream>

// Для диалога выбора файла под Windows
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

std::string ansi_to_utf8(const std::string& ansi) {
    if (ansi.empty()) return "";
    // Определяем размер буфера для широких символов
    int wlen = MultiByteToWideChar(1251, 0, ansi.c_str(), -1, nullptr, 0);
    if (wlen == 0) return ansi; // ошибка преобразования
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(1251, 0, ansi.c_str(), -1, wbuf.data(), wlen);
    // Преобразуем широкие символы в UTF-8
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) return ansi;
    std::vector<char> utf8buf(utf8len);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), -1, utf8buf.data(), utf8len, nullptr, nullptr);
    return std::string(utf8buf.data(), utf8len - 1); // удаляем завершающий ноль
}

Menu::Menu(int width, int height)
    : window(sf::VideoMode({ static_cast<unsigned int>(width), static_cast<unsigned int>(height) }),
        L"Schematic Viewer – Меню",
        sf::Style::Default) {
    window.setFramerateLimit(60);
    loadSettings();

    // Загружаем шрифт (можно использовать системный или встроенный)
    if (!font.openFromFile("Benbow Regular.ttf")) {
        if (!font.openFromFile("C:/Windows/Fonts/consola.ttf")) {
            // Если шрифт не найден, создаём заглушку
            font = sf::Font(); // будет использован стандартный (если есть)
        }
    }

    // Определяем кнопки
    float centerX = static_cast<float>(width) / 2.f;
    float yStart = 100.f;
    float buttonWidth = 250.f;
    float buttonHeight = 50.f;
    float spacing = 20.f;

    auto addButton = [&](const std::wstring& label, std::function<void()> action) {
        sf::FloatRect rect({ centerX - buttonWidth / 2, yStart }, { buttonWidth, buttonHeight });
        buttons.push_back({ rect, label, action, sf::Color(70, 70, 120), sf::Color(100, 100, 180) });
        yStart += buttonHeight + spacing;
        };

    addButton(L"Загрузить схему", [this]() { showLoadDialog(); });
    addButton(L"Настройки", [this]() {
        // Здесь можно открыть отдельное окно настроек (упрощённо – меняем параметры в консоли)
        std::cout << "Настройки (пока изменяются через консоль):\n";
        std::cout << "Введите новую силу теней (0.0-1.0): ";
        float val;
        std::cin >> val;
        if (val >= 0 && val <= 1) {
            settings.shadowStrength = val;
            saveSettings();
        }
        });
    addButton(L"О программе", [this]() { showInfoDialog(); });
    addButton(L"Выход", [this]() { window.close(); });
}

void Menu::run() {
    while (window.isOpen()) {
        handleEvents();
        update();
        draw();
    }
}

void Menu::handleEvents() {
    while (const auto event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>())
            window.close();

        if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
            sf::Vector2f mouse(static_cast<float>(mouseMoved->position.x),
                static_cast<float>(mouseMoved->position.y));
            for (auto& btn : buttons) {
                btn.hovered = btn.rect.contains(mouse);
            }
        }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                sf::Vector2f mouse(static_cast<float>(mousePressed->position.x),
                    static_cast<float>(mousePressed->position.y));
                for (auto& btn : buttons) {
                    if (btn.rect.contains(mouse) && btn.action) {
                        btn.action();
                        break;
                    }
                }
            }
        }

        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            // Обновляем размер окна и вид
            window.setView(sf::View(sf::FloatRect({ 0,0 }, { static_cast<float>(resized->size.x),
                                                          static_cast<float>(resized->size.y) })));
        }
    }
}

void Menu::update() {
    // Обновление состояния (пока пусто)
}

void Menu::draw() {
    window.clear(settings.backgroundColor);

    // Заголовок
    sf::Text title(font, "Schematic Viewer", 40);
    title.setFillColor(sf::Color::White);
    title.setPosition({ static_cast<float>(window.getSize().x) / 2.f - title.getLocalBounds().size.x / 2.f, 20.f });
    window.draw(title);

    // Кнопки
    for (auto& btn : buttons) {
        sf::RectangleShape shape(btn.rect.size);
        shape.setPosition(btn.rect.position);
        shape.setFillColor(btn.hovered ? btn.hoverColor : btn.color);
        window.draw(shape);

        sf::Text text(font, btn.label, 24);
        text.setFillColor(sf::Color::White);
        auto bounds = text.getLocalBounds();
        text.setPosition({ btn.rect.position.x + btn.rect.size.x / 2.f - bounds.size.x / 2.f,
                          btn.rect.position.y + btn.rect.size.y / 2.f - bounds.size.y / 2.f });
        window.draw(text);
    }

    // Версия внизу
    sf::Text version(font, L"Версия программы: " + PROGRAM_VERSION + L"  |  Версия игры: " + MC_VERSION, 14);
    version.setFillColor(sf::Color(180, 180, 180));
    version.setPosition({ 10, static_cast<float>(window.getSize().y) - 30 });
    window.draw(version);

    window.display();
}

void Menu::showLoadDialog() {
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
        std::string path(fileName);
        settings.lastSchematicPath = path;
        saveSettings();
        try {
            SchematicMap schem = loadSchematic(path);
            SchematicViewer viewer(schem, window.getSize().x, window.getSize().y);
            viewer.run();
        }
        catch (const std::exception& e) {
            std::cerr << "Ошибка загрузки схемы: " << e.what() << std::endl;
            // Можно показать сообщение в окне, но оставим консоль
        }
    }
#else
    // Для Linux/macOS можно использовать zenity или ввод в консоли
    std::cout << "Введите полный путь к схеме: ";
    std::string path;
    std::cin >> path;
    if (!path.empty()) {
        try {
            SchematicMap schem = loadSchematic(path);
            SchematicViewer viewer(schem, window.getSize().x, window.getSize().y);
            viewer.run();
        }
        catch (const std::exception& e) {
            std::cerr << "Ошибка: " << e.what() << std::endl;
        }
    }
#endif
}

void Menu::showInfoDialog() {
    // Создаём временное окно с информацией
    sf::RenderWindow infoWindow(sf::VideoMode({ 400, 200 }), L"О программе");
    sf::Font fnt;
    fnt.openFromFile("Benbow Regular.ttf"); // или тот же шрифт
    sf::Text info(fnt,
        L"Программа: Schematic Viewer\n"
        L"Версия: " + PROGRAM_VERSION + "\n"
        L"Minecraft версия: " + MC_VERSION + "\n"
        L"Автор: Ilia31050211\n\n"
        L"Нажмите ESC для закрытия", 18);
    info.setFillColor(sf::Color::White);
    info.setPosition({ 20, 20 });

    while (infoWindow.isOpen()) {
        while (const auto ev = infoWindow.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) infoWindow.close();
            if (const auto* key = ev->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Escape) infoWindow.close();
            }
        }
        infoWindow.clear(sf::Color(40, 40, 40));
        infoWindow.draw(info);
        infoWindow.display();
    }
}

void Menu::loadSettings() {
    std::ifstream file("settings.cfg");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, '=') && std::getline(iss, value)) {
                if (key == "lastPath") settings.lastSchematicPath = value;
                else if (key == "shadowStrength") settings.shadowStrength = std::stof(value);
                else if (key == "initialZoom") settings.initialZoom = std::stof(value);
                // можно добавить цвет
            }
        }
        file.close();
    }
}

void Menu::saveSettings() const {
    std::ofstream file("settings.cfg");
    if (file.is_open()) {
        file << "lastPath=" << settings.lastSchematicPath << "\n";
        file << "shadowStrength=" << settings.shadowStrength << "\n";
        file << "initialZoom=" << settings.initialZoom << "\n";
        file.close();
    }
}