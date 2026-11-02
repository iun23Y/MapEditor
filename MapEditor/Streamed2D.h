#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Constants.h"
#include "BlockData.h"
#include "ChunkData.h"
#include "helper.h"
#include "Side.h"
#include "Chunk.h"

class Streamed2D
{
public:
    Streamed2D(Side side) : m_side(side) {
        shader = std::make_unique<sf::Shader>();
        m_shaderLoaded = shader->loadFromFile(
            getExeDirectory() + "Resources\\Shaders\\region.vert",
            getExeDirectory() + "Resources\\Shaders\\region.frag");
        if (!m_shaderLoaded)
            std::cerr << "shader load failed\n";
    }

    void setSide(Side side) {
        if (m_side == side) return;
        m_side = side;
        invalidateAll();
    }

    void setData(const ChunkData* chunkData) {
        if (m_chunkData == chunkData) return;
        m_chunkData = chunkData;
        invalidateAll();
    }

    void invalidateAll() {
        m_boundsCache.clear();
        m_failed.clear();
        for (auto& [cc, fut] : m_loading)
            (void)fut;
        m_loading.clear();
        m_cache.clear();
        m_dirty = true;
    }

    void invalidateChunk(glm::ivec3 chunkCoord) {
        m_cache.erase(chunkCoord);
        m_loading.erase(chunkCoord);
        m_failed.erase(chunkCoord);
        m_boundsCache.erase(chunkCoord);
        m_dirty = true;
    }

    void setLoadsPerFrame(size_t n) { m_loadsPerFrame = n; }
    void setMaxConcurrentLoads(size_t n) { m_maxConcurrentLoads = n; }
    void setMaxCacheSize(size_t n) { m_maxCacheSize = n; }

    void draw(sf::RenderTarget& target) {
        const sf::View& view = target.getView();
        const sf::Vector2f center = view.getCenter();
        const sf::Vector2f size = view.getSize();

        const float left = center.x - size.x * 0.5f;
        const float top = center.y - size.y * 0.5f;

        const glm::ivec4 vpRect{
            (int)std::floor(left),
            (int)std::floor(top),
            (int)std::ceil(size.x),
            (int)std::ceil(size.y)
        };

        update(vpRect);

        for (const auto& [cc, tile] : m_cache) {
            if (!tile.hasTexture) continue;
            if (!rectIntersects(tile.rect, vpRect)) continue;

            sf::Sprite sprite(tile.texture);
            sprite.setPosition(sf::Vector2f(
                static_cast<float>(tile.offset.x),
                static_cast<float>(tile.offset.y)));
            target.draw(sprite);
        }
    }

    std::optional<std::tuple<glm::ivec3, int>> pickAtScreen(
        sf::Vector2f screenPos,
        const sf::RenderTarget& target) const
    {
        const sf::Vector2f world = target.mapPixelToCoords(
            sf::Vector2i((int)screenPos.x, (int)screenPos.y));

        const int tx = (int)std::floor(world.x);
        const int ty = (int)std::floor(world.y);

        for (const auto& [cc, tile] : m_cache) {
            if (!tile.hasTexture) continue;

            const auto sz = tile.texture.getSize();
            const int lx = tx - tile.offset.x;
            const int ly = ty - tile.offset.y;
            if (lx < 0 || ly < 0 || (unsigned)lx >= sz.x || (unsigned)ly >= sz.y)
                continue;

            const glm::ivec2 key = tile.offset + glm::ivec2(lx, ly);
            auto it = tile.topBlocks.find(key);
            if (it == tile.topBlocks.end()) continue;
            return std::make_tuple(it->second.pos, it->second.id);
        }
        return std::nullopt;
    }

private:
    struct TopBlock {
        glm::ivec3 pos;
        int id;
    };

    struct Tile {
        glm::ivec2 offset{ 0, 0 };
        glm::ivec4 rect{ 0, 0, 0, 0 };
        std::unordered_map<glm::ivec2, TopBlock> topBlocks;
        sf::Texture texture;
        bool hasTexture = false;
    };

    struct PartialTile {
        glm::ivec2 offset{ 0, 0 };
        glm::ivec4 rect{ 0, 0, 0, 0 };
        std::unordered_map<glm::ivec2, TopBlock> topBlocks;
    };

    void update(glm::ivec4 vpRect) {
        for (auto it = m_loading.begin(); it != m_loading.end(); ) {
            if (it->second.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            try {
                PartialTile p = it->second.get();
                if (p.topBlocks.empty()) {
                    m_failed.insert(it->first);
                }
                else {
                    Tile t;
                    t.offset = p.offset;
                    t.rect = p.rect;
                    t.topBlocks = std::move(p.topBlocks);
                    t.texture = buildTexture(t.topBlocks, t.offset);
                    t.hasTexture = t.texture.getSize().x > 0 && t.texture.getSize().y > 0;
                    if (t.hasTexture)
                        m_cache[it->first] = std::move(t);
                    else
                        m_failed.insert(it->first);
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Streamed2D load: " << e.what() << '\n';
                m_failed.insert(it->first);
            }
            it = m_loading.erase(it);
        }

        if (!m_dirty && m_loading.empty() && m_cache.empty()) return;

        evictDistant(vpRect);

        std::vector<std::pair<float, glm::ivec3>> candidates;
        for (const auto& [cc, chunk] : m_chunkData ? m_chunkData->getChunks()
            : std::unordered_map<glm::ivec3, Chunk>{}) {
            (void)chunk;
            if (m_cache.count(cc)) continue;
            if (m_loading.count(cc)) continue;
            if (m_failed.count(cc)) continue;

            glm::ivec4 rect = chunkBounds(cc);
            if (!rectIntersects(rect, vpRect)) continue;

            candidates.emplace_back(rectDistance(rect, vpRect), cc);
        }

        if (candidates.empty()) {
            m_dirty = false;
            return;
        }

        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        const size_t slots = (m_loading.size() < m_maxConcurrentLoads)
            ? (m_maxConcurrentLoads - m_loading.size())
            : 0;
        const size_t toStart = std::min({ m_loadsPerFrame, slots, candidates.size() });

        for (size_t i = 0; i < toStart; ++i)
            startLoad(candidates[i].second);

        m_dirty = false;
    }

    void startLoad(glm::ivec3 cc) {
        if (!m_chunkData) return;
        const Chunk* chunk = m_chunkData->tryGetChunk(cc * Chunk::SIZE);
        if (!chunk || chunk->isEmpty()) {
            m_failed.insert(cc);
            return;
        }
        const Side side = m_side;
        const Chunk* chunkPtr = chunk;
        m_loading.emplace(cc, std::async(std::launch::async,
            [chunkPtr, side]() { return computeChunkPartial(*chunkPtr, side); }));
    }

    glm::ivec4 chunkBounds(glm::ivec3 cc) {
        auto it = m_boundsCache.find(cc);
        if (it != m_boundsCache.end()) return it->second;

        const Chunk* chunk = m_chunkData ? m_chunkData->tryGetChunk(cc * Chunk::SIZE) : nullptr;
        if (!chunk) return { 0, 0, 0, 0 };

        const glm::ivec3 minP = chunk->getMinPos();
        const glm::ivec3 maxP = chunk->getMaxPos() - 1;

        int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
        for (int i = 0; i < 8; ++i) {
            const glm::ivec3 corner{
                (i & 1) ? maxP.x : minP.x,
                (i & 2) ? maxP.y : minP.y,
                (i & 4) ? maxP.z : minP.z
            };
            const glm::ivec2 k = projectKey(m_side, corner);
            minX = std::min(minX, k.x); maxX = std::max(maxX, k.x);
            minY = std::min(minY, k.y); maxY = std::max(maxY, k.y);
        }
        const glm::ivec4 rect{ minX, minY, maxX - minX + 1, maxY - minY + 1 };
        m_boundsCache.emplace(cc, rect);
        return rect;
    }

    void evictDistant(glm::ivec4 vpRect) {
        if (m_cache.size() <= m_maxCacheSize) return;

        std::vector<std::pair<float, glm::ivec3>> outside;
        outside.reserve(m_cache.size());
        for (const auto& [cc, tile] : m_cache) {
            if (rectIntersects(tile.rect, vpRect)) continue;
            outside.emplace_back(rectDistance(tile.rect, vpRect), cc);
        }
        std::sort(outside.begin(), outside.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        const size_t toEvict = std::min(
            outside.size(),
            m_cache.size() - m_maxCacheSize);
        for (size_t i = 0; i < toEvict; ++i)
            m_cache.erase(outside[i].second);
    }

    static bool rectIntersects(glm::ivec4 a, glm::ivec4 b) {
        return a.x < b.x + b.z && b.x < a.x + a.z &&
            a.y < b.y + b.w && b.y < a.y + a.w;
    }

    static float rectDistance(glm::ivec4 a, glm::ivec4 b) {
        const float ax = a.x + a.z * 0.5f;
        const float ay = a.y + a.w * 0.5f;
        const float bx = b.x + b.z * 0.5f;
        const float by = b.y + b.w * 0.5f;
        const float dx = ax - bx, dy = ay - by;
        return dx * dx + dy * dy;
    }

    static PartialTile computeChunkPartial(const Chunk& chunk, Side side) {
        PartialTile p;
        const int localMaxY = chunk.maxY();
        if (localMaxY < 0) return p;

        const glm::ivec3 chunkMin = chunk.getMinPos();
        int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;

        for (int y = 0; y <= localMaxY; ++y) {
            const int worldY = chunkMin.y + y;
            const auto layer = chunk.getLayer(worldY);
            if (layer.empty()) continue;

            for (int z = 0; z < Chunk::SIZE; ++z) {
                for (int x = 0; x < Chunk::SIZE; ++x) {
                    const uint16_t id = layer[static_cast<size_t>(z) * Chunk::SIZE + x];
                    if (id == Chunk::AIR) continue;

                    const glm::ivec3 worldPos = chunkMin + glm::ivec3(x, y, z);
                    const glm::ivec2 key = projectKey(side, worldPos);
                    auto it = p.topBlocks.find(key);
                    if (it == p.topBlocks.end()) {
                        p.topBlocks.emplace(key, TopBlock{ worldPos, static_cast<int>(id) });
                        minX = std::min(minX, key.x); maxX = std::max(maxX, key.x);
                        minY = std::min(minY, key.y); maxY = std::max(maxY, key.y);
                    }
                    else if (isCloser(side, worldPos, it->second.pos)) {
                        it->second = TopBlock{ worldPos, static_cast<int>(id) };
                    }
                }
            }
        }

        if (!p.topBlocks.empty()) {
            p.offset = { minX, minY };
            p.rect = { minX, minY, maxX - minX + 1, maxY - minY + 1 };
        }
        return p;
    }

    sf::Texture buildTexture(
        const std::unordered_map<glm::ivec2, TopBlock>& topBlocks,
        glm::ivec2 minKey)
    {
        if (topBlocks.empty() || !m_shaderLoaded || !textureManager) return {};

        glm::ivec2 maxKey{ INT_MIN, INT_MIN };
        int minY = INT_MAX;
        for (const auto& [pos, block] : topBlocks) {
            maxKey.x = std::max(maxKey.x, pos.x);
            maxKey.y = std::max(maxKey.y, pos.y);
            minY = std::min(minY, block.pos.y);
        }

        const int W = maxKey.x - minKey.x + 1;
        const int H = maxKey.y - minKey.y + 1;
        if (W <= 0 || H <= 0) return {};

        constexpr int P = 1;
        const int IW = W + 2 * P;
        const int IH = H + 2 * P;

        sf::Image idMap({ (unsigned)IW, (unsigned)IH }, sf::Color(0, 0, 0, 0));

        for (const auto& [pos, block] : topBlocks) {
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

    const ChunkData* m_chunkData = nullptr;
    std::unique_ptr<sf::Shader> shader;
    bool m_shaderLoaded = false;

    Side m_side = Side::Top;

    std::unordered_map<glm::ivec3, Tile> m_cache;
    std::unordered_map<glm::ivec3, std::future<PartialTile>> m_loading;
    std::unordered_set<glm::ivec3> m_failed;
    std::unordered_map<glm::ivec3, glm::ivec4> m_boundsCache;

    size_t m_loadsPerFrame = 2;
    size_t m_maxConcurrentLoads = 4;
    size_t m_maxCacheSize = 256;

    bool m_dirty = true;
};

//ïreview