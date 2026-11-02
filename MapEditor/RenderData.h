#pragma once

#include "ChunkData.h"
#include "BigObject.h"
#include "SmallObject.h"
#include "Data.h"

class RenderData
{
public:
	void build(const Data& data) {
		m_data.clear();

		std::vector<const Object*> layered;
		std::vector<const Object*> layerZero;

		for (const auto& [id, obj] : data.renderObjects()) {
			if (obj->layer() == 0) layerZero.push_back(obj.get());
			else layered.push_back(obj.get());
		}
		std::sort(layered.begin(), layered.end(), [](const Object* a, const Object* b) {
			if (a->layer() != b->layer()) return a->layer() < b->layer();
			return a->priority() > b->priority();
			});

		std::sort(layerZero.begin(), layerZero.end(), [](const Object* a, const Object* b) {
			return a->priority() > b->priority();
			});

		for (const Object* obj : layered) {
			if (obj->isType(ObjectType::SmallObject)) {
				mergeSmall(*static_cast<const SmallObject*>(obj), false);
			}
			else if (obj->isType(ObjectType::BigObject)) {
				mergeBig(*static_cast<const BigObject*>(obj), false);
			}
		}

		for (const Object* obj : layerZero) {
			if (obj->isType(ObjectType::SmallObject)) {
				mergeSmall(*static_cast<const SmallObject*>(obj), true);
			}
			else if (obj->isType(ObjectType::BigObject)) {
				mergeBig(*static_cast<const BigObject*>(obj), true);
			}
		}
	}
	const ChunkData& getRenderData() { return m_data; }
private:
	void mergeSmall(const SmallObject& so, bool overwrite) {
		const glm::ivec3 pos = so.position;
		for (const auto& [localPos, id] : so.data().raw()) {
			if (id == Chunk::AIR) continue;
			const glm::ivec3 w = pos + localPos;
			if (overwrite || m_data.getBlock(w) == Chunk::AIR)
				m_data.setBlock(w, static_cast<uint16_t>(id));
		}
	}
	void mergeBig(const BigObject& bo, bool overwrite) {
		const glm::ivec3 pos = bo.position;
		for (const auto& [cc, chunk] : bo.data().getChunks()) {
			if (chunk.isEmpty()) continue;
			const glm::ivec3 worldMin = pos + chunk.getMinPos();
			const glm::ivec3 worldMax = pos + chunk.getMaxPos();

			if (!m_data.hasChunk(worldMin)) {
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
						if (overwrite || m_data.getBlock(w) == Chunk::AIR)
							m_data.setBlock(w, id);
					}
			}
		}
	}
	ChunkData m_data;
};

