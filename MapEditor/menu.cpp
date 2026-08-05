#include "Menu.h"
#include "schematic.h"
#include "redactor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>

// ��� ������� ������ ����� ��� Windows
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

Menu::Menu(int width, int height)
    : window(sf::VideoMode({ static_cast<unsigned int>(width), static_cast<unsigned int>(height) }),
        L"Schematic Viewer - Menu",
        sf::Style::Default) {
    window.setFramerateLimit(60);
    loadSettings();

    // Load font or fallback
    if (!font.openFromFile("")) {
        if (!font.openFromFile("C:/Windows/Fonts/consola.ttf")) {
            font = sf::Font();
        }
    }

    // ���������� ������
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

    addButton(L"Load schematic", [this]() { showLoadDialog(); });
    addButton(L"~Settings~", [this]() {
        std::cout << "Settings (enter shadow strength 0.0-1.0):\n";
        std::cout << "Shadow strength: ";
        float val;
        std::cin >> val;
        if (val >= 0 && val <= 1) {
            settings.shadowStrength = val;
            saveSettings();
        }
        });
    addButton(L"About", [this]() { showInfoDialog(); });
    addButton(L"Exit", [this]() { window.close(); });
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
            // ��������� ������ ���� � ���
            window.setView(sf::View(sf::FloatRect({ 0,0 }, { static_cast<float>(resized->size.x),
                                                          static_cast<float>(resized->size.y) })));
        }
    }
}

void Menu::update() {
    // ���������� ��������� (���� �����)
}

void Menu::draw() {
    window.clear(settings.backgroundColor);

    // ���������
    sf::Text title(font, "Schematic Viewer", 40);
    title.setFillColor(sf::Color::White);
    title.setPosition({ static_cast<float>(window.getSize().x) / 2.f - title.getLocalBounds().size.x / 2.f, 20.f });
    window.draw(title);

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

    // ������ �����
    sf::Text version(font, L"Version: " + PROGRAM_VERSION + L"  |  Minecraft: " + MC_VERSION, 14);
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
        startLoad(path);

        while (window.isOpen() && isLoading) {
            handleEvents();
            window.clear(settings.backgroundColor);

            sf::Text status(font, L"Loading schematic...", 24);
            status.setFillColor(sf::Color::White);
            status.setPosition({ 40.f, static_cast<float>(window.getSize().y) / 2.f - 20.f });
            window.draw(status);
            window.display();

            processLoad();
            sf::sleep(sf::milliseconds(16));
        }

        if (!loadedSchematic) {
            if (!loadingMessage.empty())
                std::cerr << "Failed to load schematic: " << loadingMessage << std::endl;
            return;
        }

        int currentWidth = window.getSize().x;
        int currentHeight = window.getSize().y;
        window.close();

        try {
            Redactor redactor(std::move(loadedSchematic), currentWidth, currentHeight);
            redactor.run();
        }
        catch (const std::exception& e) {
            std::cerr << "Editor display error: " << e.what() << std::endl;
        }

        loadedSchematic.reset();
    }
#else
    // ��� Linux/macOS ����� ������������ zenity ��� ���� � �������
    std::cout << "Enter path to schematic file: ";
    std::string path;
    std::cin >> path;
    if (!path.empty()) {
        try {
            SchematicMap schem = loadSchematic(path);
            SchematicViewer viewer(schem, window.getSize().x, window.getSize().y);
            viewer.run();
        }
        catch (const std::exception& e) {
            std::cerr << "������: " << e.what() << std::endl;
        }
    }
#endif
}

void Menu::showInfoDialog() {
    // ������ ��������� ���� � �����������
    sf::RenderWindow infoWindow(sf::VideoMode({ 400, 200 }), L"About");
    sf::Font fnt;
    bool loaded = fnt.openFromFile("Benbow Regular.ttf");
    if (!loaded) {
        loaded = fnt.openFromFile("C:/Windows/Fonts/arial.ttf");
    }
    sf::Text info(fnt,
        L"Name: Schematic Viewer\n"
        L"Version: " + PROGRAM_VERSION + L"\n"
        L"Minecraft version: " + MC_VERSION + L"\n"
        L"Author: Ilia31050211\n\n"
        L"Press ESC to close", 18);
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
                // ����� �������� ����
            }
        }
        file.close();
    }
}

void Menu::startLoad(const std::string& path) {
    isLoading = true;
    loadingMessage.clear();
    loadFuture = std::async(std::launch::async, [path]() {
        return std::make_unique<SchematicMap>(path);
    });
}

void Menu::processLoad() {
    if (!isLoading) return;
    if (loadFuture.valid()) {
        auto status = loadFuture.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            try {
                loadedSchematic = loadFuture.get();
            }
            catch (const std::exception& e) {
                loadingMessage = e.what();
            }
            isLoading = false;
        }
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