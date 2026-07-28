#include "Menu.h"
#include <iostream>

int WinMain() {
    try {
        Menu menu(800, 600);
        menu.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}