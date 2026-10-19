#pragma once

#include <glm/glm.hpp>
#include "constants.h"
#include "helper.h"
#include "BlockData.h"
#include "Chunk.h"
#include <SFML/Graphics.hpp>
#include <vector>

#include "BlockPalette.h"
#include "TextureManager.h"

class Viewer2D
{
public:
	Viewer2D() = default;
	void setViewport(glm::ivec2 pos, glm::ivec2 size) {
		m_viewportPosition = pos;
		m_viewportSize = size;
		updateViewport();
	}

	void zoom(float factor) {
		m_view.zoom(factor);
	}
	void setCenter(glm::ivec2 center) {
		m_view.setCenter(toSFMLf(center));
	}
	void move(glm::ivec2 offset) {
		m_view.move(toSFMLf(offset));
	}

	void setData(const std::vector<Chunk>& data) {
		m_chunks = &data;
		m_blockData = nullptr;
	}
	void setData(const BlockData& blockData) {
		m_blockData = &blockData;
		m_chunks = nullptr;
	}

	void draw(sf::RenderTarget& target) {
		if (!texureManager) return;
		if (m_topBlocks.empty()) return;

		const sf::Texture& atlas = texureManager->getAtlas();

		const sf::View savedView = target.getView();
		target.setView(m_view);

		for (const auto& tb : m_topBlocks) {
			const uint16_t slot = texureManager->getAtlasSlot(tb.blockId);
			if (slot == 0) continue;

			const int col = slot % TextureManager::COLS;
			const int row = slot / TextureManager::COLS;

			sf::Sprite sprite(atlas);
			sprite.setTextureRect(sf::IntRect(
				{ col * TextureManager::TILE, row * TextureManager::TILE },
				{ TextureManager::TILE, TextureManager::TILE }
			));
			sprite.setPosition({
				static_cast<float>(tb.position.x),
				static_cast<float>(tb.position.z)
				});

			target.draw(sprite);
		}

		target.setView(savedView);
	}

	void update() {
		buildTopBlocks();
	}
private:
	void updateViewport() {
		m_view.setViewport(sf::FloatRect(
			{ static_cast<float>(m_viewportPosition.x) / windowSize.x,
			static_cast<float>(m_viewportPosition.y) / windowSize.y, },
			{ static_cast<float>(m_viewportSize.x) / windowSize.x,
			static_cast<float>(m_viewportSize.y) / windowSize.y }
		));
	}
	void buildTopBlocks() {
		m_topBlocks.clear();
		if (m_blockData) {
			buildFromBlockData();
		}
		else if (m_chunks) {
			buildFromChunks();
		}
	}

	void buildFromBlockData() {
		std::unordered_map<glm::ivec2, TopBlock> topPerXZ;
		topPerXZ.reserve(m_blockData->size());

		for (const auto& [pos, id] : m_blockData->raw()) {
			glm::ivec2 key(pos.x, pos.z);
			auto it = topPerXZ.find(key);
			if (it == topPerXZ.end() || pos.y > it->second.position.y) {
				topPerXZ[key] = TopBlock{ pos, id };
			}
		}

		m_topBlocks.reserve(topPerXZ.size());
		for (auto& [key, tb] : topPerXZ) {
			m_topBlocks.push_back(std::move(tb));
		}
	}
	void buildFromChunks() {
		for (size_t ci = 0; ci < m_chunks->size(); ++ci) {
			const Chunk& chunk = (*m_chunks)[ci];
			if (chunk.isEmpty()) continue;

			const int baseX = static_cast<int>(ci) * Chunk::SIZE;

			for (int z = 0; z < Chunk::SIZE; ++z) {
				for (int x = 0; x < Chunk::SIZE; ++x) {
					for (int y = Chunk::SIZE - 1; y >= 0; --y) {
						const uint16_t id = chunk.get(x, y, z);
						if (id != Chunk::AIR) {
							m_topBlocks.push_back({
								glm::ivec3(baseX + x, y, z),
								static_cast<int>(id)
								});
							break;
						}
					}
				}
			}
		}
	}

	struct TopBlock {
		glm::ivec3 position;
		int        blockId;
	};
	std::vector<TopBlock> m_topBlocks;

	const std::vector<Chunk>* m_chunks = nullptr;
	const BlockData* m_blockData = nullptr;

	sf::View m_view;

	glm::ivec2 m_viewportPosition{ 0, 0 };
	glm::ivec2 m_viewportSize{ 800, 600 };
};