#include "Data.h"
#include "BigObject.h"
#include "SmallObject.h"
#include "Chunk.h"

bool Data::objectHasBlock(const Object* obj, glm::ivec3 worldPos) {
    if (obj->isType(ObjectType::BigObject)) {
        auto* bo = static_cast<const BigObject*>(obj);
        auto opt = bo->data().getBlock(worldPos - bo->position);
        return opt && opt.value() != Chunk::AIR;
    }
    if (obj->isType(ObjectType::SmallObject)) {
        auto* so = static_cast<const SmallObject*>(obj);
        return so->data().getBlock(worldPos - so->position) != Chunk::AIR;
    }
    return false;
}

std::vector<const Object*> Data::orderedRenderObjects() const {
    std::vector<const Object*> layered, layerZero;
    for (const auto& [id, obj] : m_renderObjects) {
        if (static_cast<int>(obj->layer()) == 0) layerZero.push_back(obj.get());
        else layered.push_back(obj.get());
    }
    std::sort(layered.begin(), layered.end(), [](const Object* a, const Object* b) {
        if (a->layer() != b->layer()) return a->layer() < b->layer();
        return a->priority() > b->priority();
        });
    std::sort(layerZero.begin(), layerZero.end(), [](const Object* a, const Object* b) {
        return a->priority() < b->priority();
        });

    std::vector<const Object*> result;
    result.reserve(layerZero.size() + layered.size());
    result.insert(result.end(), layered.begin(), layered.end());
    result.insert(result.end(), layerZero.begin(), layerZero.end());
    return result;
}

Object* Data::getObjectAt(glm::ivec3 pos) {
    auto ordered = orderedRenderObjects();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        const Object* obj = *it;
        if (obj->bounds().contains(pos) && objectHasBlock(obj, pos))
            return const_cast<Object*>(obj);
    }
    return nullptr;
}