#pragma once

#include <glm/glm.hpp>

#include "TextureManager.h"

glm::ivec2 windowSize = { 600, 800 };
TextureManager* textureManager;

glm::ivec3 offset = { 0, 0, 0 };

inline glm::ivec3 worldToLocal(glm::ivec3 pos) noexcept { return pos - offset; }
inline glm::ivec3 localToWorld(glm::ivec3 pos) noexcept { return pos + offset; }