#pragma once
#include <glm/glm.hpp>

struct Bounds {
    glm::ivec3 min{ 0 };
    glm::ivec3 max{ 0 };

    Bounds() = default;
    Bounds(const glm::ivec3& a, const glm::ivec3& b) {
        min = glm::min(a, b);
        max = glm::max(a, b);
    }

    glm::ivec3 size()   const { return max - min + 1; }
    glm::ivec3 center() const { return (min + max) / 2; }

    bool contains(const glm::ivec3& p) const {
        return p.x >= min.x && p.x <= max.x &&
            p.y >= min.y && p.y <= max.y &&
            p.z >= min.z && p.z <= max.z;
    }

    bool intersects(const Bounds& o) const {
        return !(o.min.x > max.x || o.max.x < min.x ||
            o.min.y > max.y || o.max.y < min.y ||
            o.min.z > max.z || o.max.z < min.z);
    }
};