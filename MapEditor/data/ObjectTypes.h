#pragma once
#include <cstdint>

using ObjectID = uint64_t;
constexpr ObjectID NO_OBJECT = 0;

enum class ObjectType : uint16_t {
    Unknown = 0,
    House,
    Road,
    Trees,
    Grass,
    Biome,
    Group,
};