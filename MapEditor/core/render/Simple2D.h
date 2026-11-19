#pragma once

#include <SFML/Graphics.hpp>

#include <future>
#include <iostream>

#include "Constants.h"
#include "BlockData.h"
#include "helper.h"
#include "Side.h"

class Simple2D
{
public:
    Simple2D(Side side) : m_side(side) {
        shader = std::make_unique<sf::Shader>();
        m_shaderLoaded = shader->loadFromFile(
            getExeDirectory() + "Resources\\Shaders\\region.vert",
            getExeDirectory() + "Resources\\Shaders\\region.frag");
        if (!m_shaderLoaded) {
            std::cerr << "shader load failed\n";
        }
    }

    void setSide(Side side) {
        if (m_side == side) return;
        m_side = side;
        markDirty();
    }
    void setData(const BlockData& blockData) {
        m_data = &blockData;
		markDirty();
    }
    void markDirty() {
        m_dirty = true;
    }

    void draw(sf::RenderTarget& target) {
        update();
        if (!m_hasTexture) return;
        sf::Sprite sprite(m_texture);
		target.draw(sprite);
    }

    std::optional<std::tuple<glm::ivec3, int>> pickAtScreen(sf::Vector2f screenPos) const {
        if (m_future.valid() || m_dirty) return std::nullopt;
        if (!m_hasTexture) return std::nullopt;

        const int tx = static_cast<int>(std::floor(screenPos.x));
        const int ty = static_cast<int>(std::floor(screenPos.y));

        const auto sz = m_texture.getSize();
        if (tx < 0 || ty < 0 ||
            (unsigned)tx >= sz.x || (unsigned)ty >= sz.y)
            return std::nullopt;

        const glm::ivec2 key = m_offset + glm::ivec2(tx, ty);

        auto it = m_topBlocks.find(key);
        if (it == m_topBlocks.end()) return std::nullopt;

        return std::make_tuple(it->second.pos, it->second.id);
    }
private:
    void update() {
        if (m_dirty && !m_future.valid()) {
            if (startBuild()) m_dirty = false;
        }

        if (m_future.valid() &&
            m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try {
                sf::Texture tex = m_future.get();
                if (tex.getSize().x > 0 && tex.getSize().y > 0) {
                    m_texture = std::move(tex);
                    m_hasTexture = true;
                }
                else {
                    m_texture = sf::Texture();
                    m_hasTexture = false;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Simple2D build: " << e.what() << '\n';
            }
        }
    }

    bool startBuild() {
        if (!m_data || !m_shaderLoaded || !textureManager) return false;
        const Side side = m_side;

        const BlockData* data = m_data;
        m_future = std::async(std::launch::async, [this, side, data]() {
            buildBlocks(side, data);
            return buildTexture();
            });
        return true;
    }
    sf::Texture buildTexture() {
        if (m_topBlocks.empty() || !m_shaderLoaded || !textureManager) return {};

        glm::ivec2 minKey{ INT_MAX,  INT_MAX };
        glm::ivec2 maxKey{ INT_MIN,  INT_MIN };
        int minY = INT_MAX;
        for (const auto& [pos, block] : m_topBlocks) {
            minKey.x = std::min(minKey.x, pos.x);
            minKey.y = std::min(minKey.y, pos.y);
            maxKey.x = std::max(maxKey.x, pos.x);
            maxKey.y = std::max(maxKey.y, pos.y);
            minY = std::min(minY, block.pos.y);
        }
        const int W = maxKey.x - minKey.x + 1;
        const int H = maxKey.y - minKey.y + 1;
        if (W <= 0 || H <= 0) return {};

        m_offset = minKey;

        constexpr int P = 1;
        const int IW = W + 2 * P;
        const int IH = H + 2 * P;

        sf::Image idMap({ (unsigned)IW, (unsigned)IH }, sf::Color(0, 0, 0, 0));

        for (const auto& [pos, block] : m_topBlocks) {
            const uint16_t slot = textureManager->getAtlasSlot(block.id);
            if (slot == 0) continue;

            const uint16_t h16 = (uint16_t)std::clamp(block.pos.y - minY, 0, 65535);
            idMap.setPixel(
                { (unsigned)(pos.x - minKey.x + P), (unsigned)(pos.y - minKey.y + P) },
                sf::Color(slot & 0xFF, (slot >> 8) & 0xFF,
                    h16 & 0xFF, (h16 >> 8) & 0xFF));
        }

        sf::Texture idTex;
        idTex.setSmooth(false);
        idTex.setRepeated(false);
        if (!idTex.loadFromImage(idMap))
            return {};

        sf::RenderTexture rt({ (unsigned)W, (unsigned)H });
        rt.clear(sf::Color::Transparent);

        shader->setUniform("regionSize", sf::Glsl::Vec2((float)W, (float)H));
        shader->setUniform("idMapSize", sf::Glsl::Vec2((float)IW, (float)IH));
        shader->setUniform("atlasSize", sf::Glsl::Vec2(
            (float)TextureManager::ATLAS_W, (float)TextureManager::ATLAS_H));
        shader->setUniform("atlasGrid", sf::Glsl::Vec2(
            (float)TextureManager::COLS, (float)TextureManager::ROWS));
        shader->setUniform("tileSize", (float)TextureManager::TILE);
        shader->setUniform("atlas", textureManager->getAtlas());
        shader->setUniform("idMap", idTex);

        sf::Sprite sprite(idTex);
        sprite.setTextureRect(sf::IntRect({ P, P }, { W, H }));

        sf::RenderStates st;
        st.shader = shader.get();
        st.texture = &textureManager->getAtlas();
        rt.draw(sprite, st);
        rt.display();

        sf::Texture out = rt.getTexture();
        return out;
    }
    void buildBlocks(Side side, const BlockData* data) {
        if (!data) return;
        m_topBlocks.clear();
        m_topBlocks.reserve(std::min<std::size_t>(data->raw().size(), 65536));

        for (const auto& [pos, id] : data->raw()) {
            glm::ivec2 key = projectKey(side, pos);
            auto it = m_topBlocks.find(key);
            if (it == m_topBlocks.end()) {
                m_topBlocks.emplace(key, TopBlock{ pos, id });
            }
            else if (isCloser(side, pos, it->second.pos)) {
                it->second = TopBlock{ pos, id };
            }
        }
    }

    struct TopBlock {
        glm::ivec3 pos;
        int id;
    };

    const BlockData* m_data = nullptr;
    std::unique_ptr<sf::Shader> shader;
	bool m_shaderLoaded = false;

    Side m_side = Side::Top;

    std::unordered_map<glm::ivec2, TopBlock> m_topBlocks;
    glm::ivec2 m_offset{ 0, 0 };

    sf::Texture m_texture;
    bool m_hasTexture = false;

    bool m_dirty = true;
    std::future<sf::Texture> m_future;
};