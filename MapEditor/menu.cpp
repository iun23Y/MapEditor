#include "Menu.h"
#include "schematic.h"
#include "redactor.h"
#include "helper.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

Menu::Menu(int width, int height)
    : window(sf::VideoMode({ static_cast<unsigned int>(width), static_cast<unsigned int>(height) }),
        L"Schematic Viewer - Menu",
        sf::Style::Close) {
    window.setFramerateLimit(60);
    if (imageIcon.loadFromFile(getExeDirectory() + "Resources\\Image\\Icon\\icon.png"))
    window.setIcon(imageIcon);
    loadSettings();

    float centerX = static_cast<float>(width) / 2.f;
    float yStart = 100.f;
    float buttonWidth = 250.f;
    float buttonHeight = 50.f;
    float spacing = 20.f;

    backGround.loadFromFile(getExeDirectory() + "Resources\\Image\\Background\\sakhalinskaya-obl_mayak-aniva_3.png");

    ui = GuiManager();
    if (!ui.loadFont(getExeDirectory() + "Resources\\Fonts\\Deledda Closed Semibold.ttf")) {
        ui.loadFont("C:/Windows/Fonts/arial.ttf");
    }

    Tile mainTile(sf::FloatRect({ 0, 0 }, static_cast<sf::Vector2f>(window.getSize())));
    mainTile.setBackground({10, 20, 0, 50});
    mainTile.setOutline(0.f, GuiStyle::PanelBorder);

    Label mainLabel({ static_cast<float>(window.getSize().x) / 2.f - 90.f / 2.f, 20.f }, L"МАПЪ", ui.getFont(), 35);
    mainTile.addLabel(mainLabel);

    Label versionLabel({ 10, static_cast<float>(window.getSize().y) - 20 }, L"Version: " + PROGRAM_VERSION + L" | Minecraft : " + MC_VERSION, ui.getFont(), 10);
    mainTile.addLabel(versionLabel);

    Button loadButton(sf::FloatRect({ centerX - buttonWidth / 2, 100.f }, { buttonWidth, buttonHeight }), L"загрузить", ui.getFont());
    loadButton.setAction([this]() { showLoadDialog(); });
    loadButton.setColors(GuiStyle::ButtonNormal, sf::Color({ 140, 168, 184, 120 }));
    mainTile.addButton(loadButton);

    Button aboutButton(sf::FloatRect({ centerX - buttonWidth / 2, 170.f }, { buttonWidth, buttonHeight }), L"о программе", ui.getFont());
    aboutButton.setAction([this]() { showInfoDialog(); });
    aboutButton.setColors(GuiStyle::ButtonNormal, sf::Color({ 140, 168, 184, 120}));
    mainTile.addButton(aboutButton);

    Button exitButton(sf::FloatRect({ centerX - buttonWidth / 2, 240.f }, { buttonWidth, buttonHeight }), L"выйти", ui.getFont());
    exitButton.setAction([this]() { window.close(); });
    exitButton.setColors(GuiStyle::ButtonNormal, sf::Color({ 140, 168, 184, 120 }));
    mainTile.addButton(exitButton);

    ui.addTile(mainTile);
    ui.setActiveTile(0, true);
}

void Menu::run() {
    while (window.isOpen()) {
        handleEvents();
        update();
        draw();
    }
}

void Menu::handleEvents() {
    bool mouseClicked = false;
    while (const auto event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>())
            window.close();
        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                mouseClicked = true;
            }
        }
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            // ��������� ������ ���� � ���
            window.setView(sf::View(sf::FloatRect({ 0,0 }, { static_cast<float>(resized->size.x),
                                                          static_cast<float>(resized->size.y) })));
        }
    }
    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
    sf::Vector2f mousePixel = sf::Vector2f(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));

    ui.update(mousePixel, mouseClicked);

    mouseClicked = false;
}

void Menu::update() {

}

void Menu::draw() {
    window.clear(settings.backgroundColor);

    sf::Sprite backgroundSprite(backGround);
    backgroundSprite.setScale({ float(window.getSize().x) / float(backGround.getSize().x), float(window.getSize().y) / float(backGround.getSize().y) });
    window.draw(backgroundSprite);

    ui.draw(window);

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
        startLoad(path);

        while (window.isOpen() && isLoading) {
            handleEvents();
            window.clear(settings.backgroundColor);

            sf::Text status(ui.getFont(), L"Loading schematic...", 24);
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
    sf::RenderWindow infoWindow(sf::VideoMode({ 400, 200 }), L"About", sf::Style::Titlebar|sf::Style::Close);
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
    std::string exeDir = getExeDirectory();
    loadFuture = std::async(std::launch::async, [path, exeDir]() {
        return std::make_unique<SchematicMap>(path, exeDir + "world");
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