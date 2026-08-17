#pragma once

#include <string>
#include <windows.h>

static std::string getExeDirectory() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t last = path.find_last_of("\\/");
    if (last != std::string::npos) path = path.substr(0, last + 1);
    return path;
}