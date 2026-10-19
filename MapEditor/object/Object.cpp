#include "Object.h"

Object::Object(ObjectID id, ObjectType type)
    : m_id(id), m_type(type) {
}

Bounds Object::bounds() const {
    if (m_data.empty()) return Bounds{ glm::ivec3(0), glm::ivec3(0) };
    return Bounds(m_data.minCorner(), m_data.maxCorner());
}