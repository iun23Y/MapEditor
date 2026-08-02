#include "Menu.h"
#include <iostream>

int runApplication() {
    try {
        Menu menu(800, 600);
        menu.run();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
}

int WinMain(int nCmdShow) {
    return runApplication();
}