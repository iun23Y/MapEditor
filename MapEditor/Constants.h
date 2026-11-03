#pragma once

#include <glm/glm.hpp>

#include "TextureManager.h"

extern glm::ivec2 windowSize;
extern TextureManager* textureManager;
extern glm::ivec3 offset;

inline glm::ivec3 worldToLocal(glm::ivec3 pos) noexcept { return pos - offset; }
inline glm::ivec3 localToWorld(glm::ivec3 pos) noexcept { return pos + offset; }