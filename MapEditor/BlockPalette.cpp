#include "BlockPalette.h"

void BlockPalette::addBlock(int id, const std::string& name) {
    if (id == -1) id = 0;
    if (idByName.find(name) != idByName.end() || nameById.find(id) != nameById.end()) return;
    nameById[id] = name;
    idByName[name] = id;
}
bool BlockPalette::hasBlock(int id) const {
    return nameById.find(id) != nameById.end();
}
bool BlockPalette::hasBlock(const std::string& name) const {
    return idByName.find(name) != idByName.end();
}
int BlockPalette::getId(const std::string& name) const {
    auto it = idByName.find(name);
    return it == idByName.end() ? -1 : it->second;
}
std::string BlockPalette::getName(int id) const {
    auto it = nameById.find(id);
    return it == nameById.end() ? "" : it->second;
}