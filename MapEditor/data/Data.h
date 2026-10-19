#pragma once
#include "data/objectTypes.h"
#include "object/Object.h"
#include "object/Instance.h"
#include "data/BlockPalette.h"

#include <memory>
#include <unordered_map>
#include <vector>

class Data {
public:
    Data() = default;

    BlockPalette& palette() { return m_palette; }
    const BlockPalette& palette() const { return m_palette; }

    ObjectID add(std::unique_ptr<Object> obj) {
        ObjectID id = nextId++;
        obj->setId(id);
        m_objects[id] = std::move(obj);
        return id;
    }

    Object* get(ObjectID id) { return find(id); }
    const Object* get(ObjectID id) const { return find(id); }

    void remove(ObjectID id) {
        m_objects.erase(id);
    }

    ObjectID spawn(const Instance& inst) {
        ObjectID id = nextId++;
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

    ObjectID nextObjectId() const { return nextId; }
    void setNextObjectId(ObjectID id) { nextId = id; }

private:
    Object* find(ObjectID id) {
        auto it = m_objects.find(id);
        return it == m_objects.end() ? nullptr : it->second.get();
    }
    const Object* find(ObjectID id) const {
        auto it = m_objects.find(id);
        return it == m_objects.end() ? nullptr : it->second.get();
    }

    BlockPalette m_palette;
    ObjectID nextId = 1;

    std::unordered_map<ObjectID, std::unique_ptr<Object>> m_objects;
    std::unordered_map<ObjectID, std::unique_ptr<Object>> m_RenderObjects;
    std::vector<Instance> m_instances;
};