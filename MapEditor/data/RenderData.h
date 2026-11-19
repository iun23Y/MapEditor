#pragma once

#include "ChunkData.h"
#include "BigObject.h"
#include "SmallObject.h"
#include "MultiObject.h"
#include "Data.h"

#include <future>

class RenderData
{
public:
	void build(const Data& data) {
		m_data.clear();

		auto ordered = data.orderedRenderObjects();

		for (const Object* obj : ordered) {
			bool overwrite = (static_cast<int>(obj->layer()) == 0);

			if (obj->isType(ObjectType::SmallObject))
				mergeSmall(*static_cast<const SmallObject*>(obj), overwrite);
			else if (obj->isType(ObjectType::BigObject))
				mergeBig(*static_cast<const BigObject*>(obj), overwrite);
			else if (obj->isType(ObjectType::MultiObject))
				mergeMulti(*static_cast<const MultiObject*>(obj), overwrite);
		}
	}

    std::future<RenderData> buildAsync(const Data& data) const {
        return std::async(std::launch::async, [&data] {
            RenderData next;
            next.build(data);
            return next;
        });
    }

	const ChunkData& getRenderData() { return m_data; }
private:
    void mergeObject(const Object* obj, bool overwrite) {
        if (!obj) return;

        if (obj->isType(ObjectType::SmallObject))
            mergeSmall(*static_cast<const SmallObject*>(obj), overwrite);
        else if (obj->isType(ObjectType::BigObject))
            mergeBig(*static_cast<const BigObject*>(obj), overwrite);
        else if (obj->isType(ObjectType::MultiObject))
            mergeMulti(*static_cast<const MultiObject*>(obj), overwrite);
    }

    void mergeSmall(const SmallObject& so, bool overwrite) {
        const glm::ivec3 pos = so.position;
        for (const auto& [localPos, id] : so.data().raw()) {
            if (id == Chunk::AIR) continue;
            const glm::ivec3 w = pos + localPos;
            if (overwrite || m_data.getBlock(w).value_or(Chunk::AIR) == Chunk::AIR)
                m_data.setBlock(w, static_cast<uint16_t>(id));
        }
    }
    void mergeBig(const BigObject& bo, bool overwrite) {
        const glm::ivec3 pos = bo.position;
        for (const auto& [cc, chunk] : bo.data().getChunks()) {
            if (chunk.isEmpty()) continue;
            const glm::ivec3 worldMin = pos + chunk.getMinPos();
            const glm::ivec3 worldMax = pos + chunk.getMaxPos();

            const bool aligned = (worldMin.x % Chunk::SIZE == 0 &&
                worldMin.y % Chunk::SIZE == 0 &&
                worldMin.z % Chunk::SIZE == 0);

            if (aligned && !m_data.hasChunk(worldMin)) {
                m_data.addChunk(Chunk(chunk.data(), worldMin, worldMax));
                continue;
            }

            for (int y = 0; y <= chunk.maxY(); ++y) {
                const auto layer = chunk.getLayer(chunk.getMinPos().y + y);
                for (int z = 0; z < Chunk::SIZE; ++z)
                    for (int x = 0; x < Chunk::SIZE; ++x) {
                        const uint16_t id = layer[z * Chunk::SIZE + x];
                        if (id == Chunk::AIR) continue;
                        const glm::ivec3 w = worldMin + glm::ivec3(x, y, z);
                        if (overwrite || m_data.getBlock(w).value_or(Chunk::AIR) == Chunk::AIR)
                            m_data.setBlock(w, id);
                    }
            }
        }
    }
    void mergeMulti(const MultiObject& mo, bool overwrite) {
        for (const auto& child : mo.children())
            mergeObject(child.get(), overwrite);
    }

	ChunkData m_data;
};

