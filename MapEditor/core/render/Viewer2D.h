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
	Viewer2D()
		: m_view(sf::FloatRect({ 0.f, 0.f }, { 1.f, 1.f })) {}

	Streamed2D* streamed() noexcept { return m_streamed2D.get(); }
	const Streamed2D* streamed() const noexcept { return m_streamed2D.get(); }
	Side getSide() const noexcept { return m_side; }

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
	void setCenterF(const sf::Vector2f& c) { m_view.setCenter(c); }
	void setSizeF(const sf::Vector2f& s) { m_view.setSize(s); }
	sf::Vector2f getCenterF() const { return m_view.getCenter(); }
	sf::Vector2f getSizeF() const { return m_view.getSize(); }

	void move(glm::ivec2 offset) {
		m_view.move(toSFMLf(offset));
	}
	glm::ivec4 viewRect() const {
		const sf::Vector2f center = m_view.getCenter();
		const sf::Vector2f size = m_view.getSize();
		return {
			static_cast<int>(std::floor(center.x - size.x * 0.5f)),
			static_cast<int>(std::floor(center.y - size.y * 0.5f)),
			static_cast<int>(std::ceil(size.x)),
			static_cast<int>(std::ceil(size.y))
		};
	}

	void setData(const ChunkData& data) {
		if (!m_streamed2D) {
			m_streamed2D = std::make_unique<Streamed2D>(m_side);
		}
		m_streamed2D->setData(&data);
		m_simple2D.reset();
	}
	void setData(const BlockData& data) {
		if (!m_simple2D) {
			m_simple2D = std::make_unique<Simple2D>(m_side);
		}
		m_simple2D->setData(data);
		m_streamed2D.reset();
	}

	void draw(sf::RenderTarget& target) {
		const sf::View savedView = target.getView();
		target.setView(m_view);

		render2D(target);

		target.setView(savedView);
	}

	void invalidate() {
		if (m_streamed2D) m_streamed2D->invalidateAll();
		if (m_simple2D) return; //заглушка 
	}

	std::optional<std::tuple<glm::ivec3, int>> pickAtScreen(
		sf::Vector2f screenPos, const sf::RenderTarget& target) const
	{
        if (m_streamed2D) {
			const sf::Vector2f world = target.mapPixelToCoords(
				sf::Vector2i((int)screenPos.x, (int)screenPos.y), m_view);
			return m_streamed2D->pickAtWorld(world);
		}
		return std::nullopt;
	}

private:
	void updateViewport() {
		m_view.setViewport(sf::FloatRect(
			{ static_cast<float>(m_viewportPosition.x) / windowSize.x,
			static_cast<float>(m_viewportPosition.y) / windowSize.y, },
			{ static_cast<float>(m_viewportSize.x) / windowSize.x,
			static_cast<float>(m_viewportSize.y) / windowSize.y }
		));
		float vh = static_cast<float>(m_viewportSize.y);
		float vw = vh * (static_cast<float>(m_viewportSize.x) / static_cast<float>(m_viewportSize.y));
		m_view.setSize({ vw, vh });
		m_view.setCenter({ vw * 0.5f, vh * 0.5f });
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