#include "Menu.h"
#include <iostream>
#include "schematic.h"

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
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

int WinMain(int nCmdShow) {
    return runApplication();
}