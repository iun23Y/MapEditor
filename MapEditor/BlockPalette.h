#pragma once
#include <SFML/Graphics.hpp>
#include <unordered_map>

class BlockPalette {
public:
    std::unordered_map<int, std::string> nameById;
    std::unordered_map<std::string, int> idByName;

    void addBlock(int id, const std::string& name);
    bool hasBlock(int id) const;
    bool hasBlock(const std::string& name) const;
    int  getId(const std::string& name) const;
    std::string getName(int id) const;
};