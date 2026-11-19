#pragma once
#include "data/objectTypes.h"
#include "object/Object.h"
#include "object/Instance.h"
#include "data/BlockPalette.h"

#include <memory>
#include <algorithm>
#include <unordered_map>
#include <vector>

enum class ObjectRenderType {
    Renderer,
    NonRenderer
};

class Data {
public:
    Data() = default;

    void setWorldOffset(glm::ivec3 value) noexcept { m_worldOffset = value; }
    glm::ivec3 worldOffset() const noexcept { return m_worldOffset; }

    glm::ivec3 toLocal(glm::ivec3 worldPos) const noexcept {
        return worldPos - m_worldOffset;
    }

    glm::ivec3 toWorld(glm::ivec3 localPos) const noexcept {
        return localPos + m_worldOffset;
    }

    BlockPalette& palette() { return m_palette; }
    const BlockPalette& palette() const { return m_palette; }

    ObjectID add(std::unique_ptr<Object> obj, ObjectRenderType type) {
        if (!obj) return NO_OBJECT;
        ObjectID id = obj->id();
        if (id == NO_OBJECT) {
            id = nextId++;
            obj->setId(id);
        }
        else {
            nextId = std::max(nextId, id + 1);
        }
        insertObject(id, std::move(obj), type);
        return id;
    }

    ObjectID addLoaded(std::unique_ptr<Object> obj, ObjectRenderType type) {
        if (!obj) return NO_OBJECT;
        const ObjectID id = obj->id();
        nextId = std::max(nextId, id + 1);
        insertObject(id, std::move(obj), type);
        return id;
    }

    Object* get(ObjectID id) { return find(id); }
    const Object* get(ObjectID id) const { return find(id); }

    void remove(ObjectID id) {
        m_objects.erase(id);
        m_renderObjects.erase(id);
    }

    ObjectID spawn(const Instance& inst) {
        const ObjectID id = nextId++;
        Instance copy = inst;
        copy.id = id;
        m_instances.push_back(copy);
        return id;
    }

    void removeInstance(ObjectID id) {
        for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
            if (it->id == id) { m_instances.erase(it); return; }
        }
    }

    std::vector<Instance>& instances() { return m_instances; }
    const std::vector<Instance>& instances() const { return m_instances; }

    auto& objects() { return m_objects; }
    const auto& objects() const { return m_objects; }

    auto& renderObjects() { return m_renderObjects; }
    const auto& renderObjects() const { return m_renderObjects; }

    ObjectID nextObjectId() const { return nextId; }
    void setNextObjectId(ObjectID id) { nextId = id; }

    void clear() {
        m_renderObjects.clear();
        m_objects.clear();
    }

    std::vector<const Object*> orderedRenderObjects() const;
    Object* getObjectAt(glm::ivec3 pos);
private:
    static bool objectHasBlock(const Object* obj, glm::ivec3 worldPos);

    void insertObject(ObjectID id, std::unique_ptr<Object> obj, ObjectRenderType type) {
        switch (type) {
        case ObjectRenderType::Renderer:
            m_renderObjects.emplace(id, std::move(obj));
            break;
        case ObjectRenderType::NonRenderer:
            m_objects.emplace(id, std::move(obj));
            break;
        }
    }

    Object* find(ObjectID id) {
        if (auto it = m_objects.find(id); it != m_objects.end())
            return it->second.get();
        if (auto it = m_renderObjects.find(id); it != m_renderObjects.end())
            return it->second.get();
        return nullptr;
    }
    const Object* find(ObjectID id) const {
        if (auto it = m_objects.find(id); it != m_objects.end())
            return it->second.get();
        if (auto it = m_renderObjects.find(id); it != m_renderObjects.end())
            return it->second.get();
        return nullptr;
    }

    BlockPalette m_palette;
    ObjectID nextId = 1;

    std::unordered_map<ObjectID, std::unique_ptr<Object>> m_objects;
    std::unordered_map<ObjectID, std::unique_ptr<Object>> m_renderObjects;
    std::vector<Instance> m_instances;
    glm::ivec3 m_worldOffset{ 0 };
};