#pragma once

#include "data/ObjectTypes.h"
#include <glm/glm.hpp>

struct Instance {
    ObjectID   id = NO_OBJECT;
    ObjectID   protoId = NO_OBJECT;
    glm::ivec3 position{ 0 };
    float      rotation = 0.f;
};