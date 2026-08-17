#include "Menu.h"
#include <iostream>
#include "schematic.h"
#include <filesystem>
#include <windows.h>
#include "BTEGeoConventor.h"

class Redactor {
private:
    sf::View redactorView;
    sf::View uiView;

	sf::RenderWindow window;
	SchematicMap map;
public:
    void updateUI() {
		window.setView(uiView);



		window.setView(redactorView);
    }
};


int runApplication() {
    try {
        Menu menu(800, 600);
        menu.run();
        // After exiting the UI, remove runtime world directory placed next to the exe
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string path(buffer);
        size_t last = path.find_last_of("\\/");
        if (last != std::string::npos) path = path.substr(0, last + 1);
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(path) / "world", ec);
        if (ec) std::cerr << "Failed to remove world directory: " << ec.message() << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    return runApplication();

}