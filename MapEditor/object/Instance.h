#pragma once

#include "data/ObjectTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct Instance {
    ObjectID id = NO_OBJECT;
    ObjectID protoId = NO_OBJECT;
    glm::ivec3 position{ 0 };
    glm::quat rotation{ 1, 0, 0, 0 };
};