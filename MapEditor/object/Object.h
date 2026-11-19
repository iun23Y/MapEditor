#pragma once
#include "data/ObjectTypes.h"
#include "data/Bounds.h"
#include <string>
#include <glm/glm.hpp>
#include "ObjectLayer.h"

//Abstract
class Object
{
public:
    Object(ObjectType type) : m_type(type) {}
    virtual ~Object() = default;

    ObjectID id() const { return m_id; }
    ObjectLayer layer() const { return m_layer; }
    int priority() const { return m_priority; }

    glm::ivec3 position{ 0 };

    virtual Bounds bounds() const = 0;

    void setId(ObjectID id) { m_id = id; }
    void setLayer(ObjectLayer l) { m_layer = l; }
    void setPriority(int p) { m_priority = p; }
    void setName(const std::string& n) { m_name = n; }
    const std::string& name() const { return m_name; }

    bool isType(ObjectType type) const;
	void setType(ObjectType type) { m_type = type; }
protected:
    ObjectID m_id = NO_OBJECT;
    ObjectLayer m_layer = ObjectLayer::Medium;
    int m_priority = 0;
    std::string m_name;

    ObjectType m_type;
};