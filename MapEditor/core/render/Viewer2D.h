#pragma once

#include <glm/glm.hpp>
#include <SFML/Graphics.hpp>
#include <optional>
#include <vector>

#include "constants.h"
#include "helper.h"
#include "BlockData.h"
#include "Chunk.h"
#include "BlockPalette.h"
#include "ChunkData.h"
#include "TextureManager.h"
#include "Side.h"

#include "Simple2D.h"
#include "Streamed2D.h"

class Viewer2D
{
public:
	Viewer2D() = default;
	void setViewport(glm::ivec2 pos, glm::ivec2 size) {
		m_viewportPosition = pos;
		m_viewportSize = size;
		updateViewport();
	}

	void setSide(Side side) {
		m_side = side;
		updateSide();
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

	void setData(const ChunkData& data) {
		m_streamed2D = std::make_unique<Streamed2D>(m_side);
		m_streamed2D->setData(&data);
		m_simple2D.reset();
	}
	void setData(const BlockData& data) {
		m_simple2D = std::make_unique<Simple2D>(m_side);
		m_simple2D->setData(data);
		m_streamed2D.reset();
	}

	void draw(sf::RenderTarget& target) {
		const sf::View savedView = target.getView();
		target.setView(m_view);

		render2D(target);

		target.setView(savedView);
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
	void updateSide() {
		if (m_simple2D) {
			m_simple2D->setSide(m_side);
		}
		if (m_streamed2D) {
			m_streamed2D->setSide(m_side);
		}
	}
	void render2D(sf::RenderTarget& target) {
		if (m_simple2D) {
			m_simple2D->draw(target);
		}
		if (m_streamed2D) {
			m_streamed2D->draw(target);
		}
	}

	Side m_side = Side::Front;

	std::unique_ptr<Simple2D> m_simple2D;
	std::unique_ptr<Streamed2D> m_streamed2D;

	sf::View m_view;

	glm::ivec2 m_viewportPosition;
	glm::ivec2 m_viewportSize;
};