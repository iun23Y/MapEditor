#pragma once

#include <glm/glm.hpp>
#include "data/ObjectTypes.h"

struct ObjectMeta {
    ObjectID id;
    glm::ivec3 position;
    int layer = 0;
    int priority = 0;
    int regionIndex = -1;
};