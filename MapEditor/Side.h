#pragma once

enum class Side { Top, Bottom, Front, Back, Left, Right };

static glm::ivec2 projectKey(Side side, glm::ivec3 p) {
    switch (side) {
    case Side::Top:
    case Side::Bottom: return { p.x, p.z };
    case Side::Front:
    case Side::Back:   return { p.x, p.y };
    case Side::Left:
    case Side::Right:  return { p.z, p.y };
    }
    return { p.x, p.z };
}
static bool isCloser(Side side, glm::ivec3 a, glm::ivec3 b) {
    switch (side) {
    case Side::Top:    return a.y > b.y;
    case Side::Bottom: return a.y < b.y;
    case Side::Front:  return a.z > b.z;
    case Side::Back:   return a.z < b.z;
    case Side::Right:  return a.x > b.x;
    case Side::Left:   return a.x < b.x;
    }
    return false;
}