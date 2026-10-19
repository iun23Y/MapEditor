#pragma once
#include "data/ObjectTypes.h"
#include "data/Bounds.h"
#include "data/BlockData.h"
#include <string>
#include <glm/glm.hpp>

class Object {
public:
    Object() = default;
    Object(ObjectID id, ObjectType type)
        : m_id(id), m_type(type) {
    }
    virtual ~Object() = default;

    ObjectID id() const { return m_id; }
    ObjectType type() const { return m_type; }
    int layer() const { return m_layer; }
    int priority() const { return m_priority; }

    glm::ivec3 position{ 0 };

    BlockData& data() { return m_data; }
    const BlockData& data() const { return m_data; }

    Bounds bounds() const {
        if (m_data.empty()) return Bounds{ glm::ivec3(0), glm::ivec3(0) };
        return Bounds(m_data.minCorner(), m_data.maxCorner());
    }

    void setId(ObjectID id) { m_id = id; }
    void setType(ObjectType t) { m_type = t; }
    void setLayer(int l) { m_layer = l; }
    void setPriority(int p) { m_priority = p; }
    void setName(const std::string& n) { m_name = n; }
    const std::string& name() const { return m_name; }

protected:
    ObjectID m_id = NO_OBJECT;
    ObjectType m_type = ObjectType::Unknown;
    int m_layer = 0;
    int m_priority = 0;
    std::string m_name;
    BlockData m_data;
};