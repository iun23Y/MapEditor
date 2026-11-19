#pragma once
#include "Object.h"
#include "ObjectTypes.h"
#include "Bounds.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <climits>
#include <glm/glm.hpp>

class MultiObject : public Object
{
public:
    MultiObject() : Object(ObjectType::MultiObject) {}

    const std::vector<std::unique_ptr<Object>>& children() const { return m_children; }

    void addChild(std::unique_ptr<Object> child) {
        if (!child) return;
        m_metaDirty = true;
        m_children.push_back(std::move(child));
    }

    std::unique_ptr<Object> removeChild(ObjectID id) {
        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            if ((*it)->id() == id) {
                auto out = std::move(*it);
                m_children.erase(it);
                m_metaDirty = true;
                return out;
            }
        }
        return nullptr;
    }

    Object* findChild(ObjectID id) {
        for (auto& c : m_children)
            if (c->id() == id) return c.get();
        return nullptr;
    }

    const Object* findChild(ObjectID id) const {
        for (auto& c : m_children)
            if (c->id() == id) return c.get();
        return nullptr;
    }

    void move(glm::ivec3 delta) {
        position += delta;
        for (auto& child : m_children) {
            if (auto* nested = dynamic_cast<MultiObject*>(child.get()))
                nested->move(delta);
            else
                child->position += delta;
        }
    }

    Bounds bounds() const override {
        glm::ivec3 mn{ INT_MAX }, mx{ INT_MIN };
        bool any = false;
        for (auto& c : m_children) {
            Bounds b = c->bounds();
            if (b.min.x > b.max.x) continue;
            mn = glm::min(mn, b.min);
            mx = glm::max(mx, b.max);
            any = true;
        }
        return any ? Bounds{ mn, mx } : Bounds{ position, position };
    }

    bool hasBlock(glm::ivec3 worldPos) const {
        for (auto& c : m_children) {
            if (auto* nested = dynamic_cast<const MultiObject*>(c.get())) {
                if (nested->hasBlock(worldPos)) return true;
            }
            else if (c->bounds().contains(worldPos) && objectHasBlockRecursive(c.get(), worldPos)) {
                return true;
            }
        }
        return false;
    }

    bool isDirty() const {
        if (m_metaDirty) return true;
        for (auto& c : m_children)
            if (c->isType(ObjectType::BigObject)) {
                auto* bo = static_cast<const BigObject*>(c.get());
                if (bo->isDirty()) return true;
            }
        return false;
    }
    bool isMetaDirty() const { return m_metaDirty; }
    void markMetaClean() { m_metaDirty = false; }

    static bool objectHasBlockRecursive(const Object* obj, glm::ivec3 pos) {
        if (obj->isType(ObjectType::BigObject)) {
            auto* bo = static_cast<const BigObject*>(obj);
            auto opt = bo->data().getBlock(pos - bo->position);
            return opt && opt.value() != Chunk::AIR;
        }
        if (obj->isType(ObjectType::SmallObject)) {
            auto* so = static_cast<const SmallObject*>(obj);
            return so->data().getBlock(pos - so->position) != Chunk::AIR;
        }
        if (obj->isType(ObjectType::MultiObject)) {
            return static_cast<const MultiObject*>(obj)->hasBlock(pos);
        }
        return false;
    }

private:
    std::vector<std::unique_ptr<Object>> m_children;
    bool m_metaDirty = false;
};