#pragma once
#include <cstdint>

using ObjectID = uint64_t;
constexpr ObjectID NO_OBJECT = 0;

enum class ObjectType : uint8_t
{
	SmallObject = 0,
	BigObject = 1,
};