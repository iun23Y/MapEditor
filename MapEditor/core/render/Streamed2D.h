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
    Streamed2D(Side side)
        : m_side(side),
        m_spriteTexture(sf::Vector2u(1, 1)),
        m_sprite(m_spriteTexture)
    {
        shader = std::make_unique<sf::Shader>();
        m_shaderLoaded = shader->loadFromFile(
            getExeDirectory() + "Resources\\Shaders\\region.vert",
            getExeDirectory() + "Resources\\Shaders\\region.frag");
        if (!m_shaderLoaded)
            std::cerr << "shader load failed\n";
    }

    size_t loadsPerFrame() const noexcept { return m_loadsPerFrame; }
    size_t maxConcurrentLoads() const noexcept { return m_maxConcurrentLoads; }
    size_t maxCacheSize() const noexcept { return m_maxCacheSize; }
    size_t cacheSize() const noexcept { return m_cache.size(); }
    size_t loadingSize() const noexcept { return m_loading.size(); }
    Side side() const noexcept { return m_side; }

    void setSide(Side side) {
        if (m_side == side) return;
        m_side = side;
        invalidateAll();
    }

    void setData(const ChunkData* chunkData) {
        if (m_chunkData == chunkData) return;
        m_chunkData = chunkData;
        m_dataRevision = chunkData ? chunkData->revision() : 0;
        rebuildIdCaches();
        invalidateAll();
    }

    void invalidateAll() {
        m_boundsCache.clear();
        m_failed.clear();
        m_refresh.clear();
        ++m_generation;

        for (auto it = m_cache.begin(); it != m_cache.end(); ) {
            const Chunk* chunk = m_chunkData
                ? m_chunkData->tryGetChunk(it->first * Chunk::SIZE)
                : nullptr;
            if (!chunk || chunk->isEmpty()) {
                it = m_cache.erase(it);
            }
            else {
                m_refresh.insert(it->first);
                ++it;
            }
        }
        m_dirty = true;
    }

    void invalidateRegion(glm::ivec3 wMin, glm::ivec3 wMax) {
        std::vector<glm::ivec3> toErase;
        for (const auto& [cc, tile] : m_cache) {
            const glm::ivec3 cMin = cc * Chunk::SIZE;
            const glm::ivec3 cMax = cMin + glm::ivec3(Chunk::SIZE);
            if (wMin.x < cMax.x && wMax.x > cMin.x &&
                wMin.y < cMax.y && wMax.y > cMin.y &&
                wMin.z < cMax.z && wMax.z > cMin.z)
                toErase.push_back(cc);
        }
        for (auto& cc : toErase) {
            m_cache.erase(cc);
            m_refresh.erase(cc);
            m_boundsCache.erase(cc);
            m_failed.erase(cc);
            m_dirty = true;
        }
    }

    void invalidateChunk(glm::ivec3 chunkCoord) {
        m_cache.erase(chunkCoord);
        m_refresh.erase(chunkCoord);
        m_failed.erase(chunkCoord);
        m_boundsCache.erase(chunkCoord);
        m_dirty = true;
    }

    void setLoadsPerFrame(size_t n) { m_loadsPerFrame = n; }
    void setMaxConcurrentLoads(size_t n) { m_maxConcurrentLoads = n; }
    void setMaxCacheSize(size_t n) { m_maxCacheSize = n; }

    void draw(sf::RenderTarget& target) {
        if (m_chunkData && m_chunkData->revision() != m_dataRevision) {
            m_dataRevision = m_chunkData->revision();
            invalidateAll();
        }

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

        if (vpRect != m_lastVpRect || m_dirty || !m_loading.empty()) {
            update(vpRect);
            m_lastVpRect = vpRect;
        }

        m_visible.clear();
        if (m_visible.capacity() < m_cache.size())
            m_visible.reserve(m_cache.size());

        for (const auto& [cc, tile] : m_cache) {
            if (!tile.hasTexture) continue;
            if (!rectIntersects(tile.rect, vpRect)) continue;
            m_visible.emplace_back(cc, &tile);
        }

        std::sort(m_visible.begin(), m_visible.end(),
            [this](const auto& a, const auto& b) {
                switch (m_side) {
                case Side::Top:    return a.first.y < b.first.y;
                case Side::Bottom: return a.first.y > b.first.y;
                case Side::Front:  return a.first.z > b.first.z;
                case Side::Back:   return a.first.z < b.first.z;
                case Side::Left:   return a.first.x > b.first.x;
                case Side::Right:  return a.first.x < b.first.x;
                }
                return false;
            });

        constexpr int TILE = TextureManager::TILE;
        constexpr int P = 1;
        m_sprite.setScale({ 1.f / TILE, 1.f / TILE });

        for (const auto& [cc, tile] : m_visible) {
            m_sprite.setTexture(tile->renderTexture.getTexture());
            m_sprite.setTextureRect(sf::IntRect(
                { P * TILE, P * TILE },
                { tile->rect.z * TILE, tile->rect.w * TILE }));
            m_sprite.setPosition(sf::Vector2f(
                static_cast<float>(tile->offset.x),
                static_cast<float>(tile->offset.y)));
            target.draw(m_sprite);
        }
    }

    std::optional<std::tuple<glm::ivec3, int>> pickAtScreen(
        sf::Vector2f screenPos,
        const sf::RenderTarget& target) const
    {
        const sf::Vector2f world = target.mapPixelToCoords(
            sf::Vector2i((int)screenPos.x, (int)screenPos.y));

        return pickAtWorld(world);
    }

    std::optional<std::tuple<glm::ivec3, int>> pickAtWorld(
        sf::Vector2f world) const
    {
        const int tx = (int)std::floor(world.x);
        const int ty = (int)std::floor(world.y);

        for (const auto& [cc, tile] : m_cache) {
            if (!tile.hasTexture) continue;

            const int lx = tx - tile.offset.x;
            const int ly = ty - tile.offset.y;
            if (lx < 0 || ly < 0 || lx >= tile.rect.z || ly >= tile.rect.w)
                continue;

            const glm::ivec2 key = tile.offset + glm::ivec2(lx, ly);
            auto it = std::find_if(tile.topBlocks.begin(), tile.topBlocks.end(),
                [&key](const auto& pair) { return pair.first == key; });
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
        std::vector<std::pair<glm::ivec2, TopBlock>> topBlocks;
        sf::RenderTexture renderTexture;
        bool hasTexture = false;

        Tile() = default;
        Tile(Tile&&) noexcept = default;
        Tile& operator=(Tile&&) noexcept = default;
    };

    struct PartialTile {
        glm::ivec2 offset{ 0, 0 };
        glm::ivec4 rect{ 0, 0, 0, 0 };
        std::vector<std::pair<glm::ivec2, TopBlock>> topBlocks;

        PartialTile() = default;
        PartialTile(PartialTile&&) noexcept = default;
        PartialTile& operator=(PartialTile&&) noexcept = default;
    };

    struct ChunkSnapshot {
        glm::ivec3 chunkMin{ 0 };
        std::optional<Chunk> chunk;
        std::unordered_map<glm::ivec3, uint16_t> margin;
    };

    void rebuildIdCaches() {
        if (!palette || !textureManager) return;
        if (m_idCachesBuilt) return;

        m_noSolidById.clear();
        for (const auto& [id, rawName] : palette->nameById) {
            const uint16_t uid = static_cast<uint16_t>(id);
            const std::string stripped = stripBlockStates(rawName);
            if (textureManager->getBlockType(stripped) == "nosolid")
                m_noSolidById.insert(uid);
        }
        m_idCachesBuilt = true;
    }

    static ChunkSnapshot makeSnapshot(const ChunkData& cd, glm::ivec3 cc) {
        ChunkSnapshot snap;
        snap.chunkMin = cc * Chunk::SIZE;

        const Chunk* chunk = cd.tryGetChunk(snap.chunkMin);
        if (!chunk || chunk->isEmpty()) return snap;

        snap.chunk = *chunk;

        const Chunk* neighbors[3][3]{};
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const glm::ivec3 neighborMin = snap.chunkMin +
                    glm::ivec3(dx * Chunk::SIZE, 0, dz * Chunk::SIZE);
                neighbors[dz + 1][dx + 1] = cd.tryGetChunk(neighborMin);
            }

        for (int ly = 0; ly < Chunk::SIZE; ++ly)
            for (int lz = -1; lz <= Chunk::SIZE; ++lz)
                for (int lx = -1; lx <= Chunk::SIZE; ++lx) {
                    if (lx >= 0 && lx < Chunk::SIZE &&
                        lz >= 0 && lz < Chunk::SIZE) continue;

                    const int dx = lx < 0 ? -1 : (lx >= Chunk::SIZE ? 1 : 0);
                    const int dz = lz < 0 ? -1 : (lz >= Chunk::SIZE ? 1 : 0);
                    const Chunk* neighbor = neighbors[dz + 1][dx + 1];
                    if (!neighbor || neighbor->isEmpty()) continue;

                    const glm::ivec3 local{ lx, ly, lz };
                    const uint16_t id = neighbor->get(snap.chunkMin + local);
                    if (id != Chunk::AIR)
                        snap.margin.emplace(local, id);
                }
        return snap;
    }

    void update(glm::ivec4 vpRect) {
        constexpr int PREFETCH_CHUNKS = 5;
        constexpr int prefetchMargin = PREFETCH_CHUNKS * Chunk::SIZE;
        const float viewCenterX = vpRect.x + vpRect.z * 0.5f;
        const float viewCenterY = vpRect.y + vpRect.w * 0.5f;
        const float previousCenterX = m_lastVpRect.x + m_lastVpRect.z * 0.5f;
        const float previousCenterY = m_lastVpRect.y + m_lastVpRect.w * 0.5f;
        const float moveX = m_lastVpRect.x == INT_MIN ? 0.f : viewCenterX - previousCenterX;
        const float moveY = m_lastVpRect.y == INT_MIN ? 0.f : viewCenterY - previousCenterY;
        const float moveScale = std::max(std::abs(moveX), std::abs(moveY));
        const int leadX = moveScale > 0.f
            ? static_cast<int>(std::round(prefetchMargin * std::abs(moveX) / moveScale))
            : 0;
        const int leadY = moveScale > 0.f
            ? static_cast<int>(std::round(prefetchMargin * std::abs(moveY) / moveScale))
            : 0;
        const glm::ivec4 loadRect{
            vpRect.x - prefetchMargin - (moveX < 0.f ? leadX : 0),
            vpRect.y - prefetchMargin - (moveY < 0.f ? leadY : 0),
            vpRect.z + 2 * prefetchMargin + (moveX > 0.f ? leadX : 0),
            vpRect.w + 2 * prefetchMargin + (moveY > 0.f ? leadY : 0)
        };

        const size_t loadingBefore = m_loading.size();
        bool textureBuiltThisFrame = false;
        for (auto it = m_loading.begin(); it != m_loading.end(); ) {
            if (it->second.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            const glm::ivec3 cc = it->first;
            try {
                PartialTile p = it->second.get();
                if (m_loadingGeneration[cc] != m_generation) {
                    it = m_loading.erase(it);
                    m_loadingGeneration.erase(cc);
                    continue;
                }
                if (p.topBlocks.empty()) {
                    m_failed.insert(cc);
                }
                else {
                    Tile t;
                    t.offset = p.offset;
                    t.rect = p.rect;
                    t.topBlocks = std::move(p.topBlocks);
                    t.renderTexture = buildTexture(t.topBlocks, t.offset, t.rect.z, t.rect.w);
                    textureBuiltThisFrame = true;
                    t.renderTexture.setSmooth(false);
                    t.renderTexture.setRepeated(false);
                    t.hasTexture = t.renderTexture.getSize().x > 0 && t.renderTexture.getSize().y > 0;
                    if (t.hasTexture)
                        m_cache[cc] = std::move(t);
                    else
                        m_failed.insert(cc);
                    m_refresh.erase(cc);
                }
            }
            catch (const std::exception&) {
                m_failed.insert(cc);
            }
            it = m_loading.erase(it);
            m_loadingGeneration.erase(cc);
            if (textureBuiltThisFrame)
                break;
        }

        if (!m_dirty && m_loading.size() == loadingBefore && vpRect == m_lastVpRect)
            return;

        evictDistant(vpRect);

        struct Candidate {
            glm::ivec3 coord;
            bool visible;
            bool ahead;
            float distance;
        };
        std::vector<Candidate> candidates;
        if (m_chunkData) {
            for (const auto& [cc, chunk] : m_chunkData->getChunks()) {
                (void)chunk;
                if (m_cache.count(cc) && !m_refresh.count(cc)) continue;
                if (m_loading.count(cc)) continue;
                if (m_failed.count(cc)) continue;

                const glm::ivec4 rect = chunkBounds(cc);
                if (!rectIntersects(rect, loadRect)) continue;

                const float chunkCenterX = rect.x + rect.z * 0.5f;
                const float chunkCenterY = rect.y + rect.w * 0.5f;
                const float forwardDistance = moveX * (chunkCenterX - viewCenterX) +
                    moveY * (chunkCenterY - viewCenterY);
                candidates.push_back({
                    cc,
                    rectIntersects(rect, vpRect),
                    moveScale > 0.f && forwardDistance > 0.f,
                    rectDistance(rect, vpRect)
                });
            }
        }

        pruneBoundsCache();
        if (candidates.empty()) {
            m_dirty = false;
            return;
        }

        const size_t slots = (m_loading.size() < m_maxConcurrentLoads)
            ? (m_maxConcurrentLoads - m_loading.size())
            : 0;
        const size_t toStart = textureBuiltThisFrame
            ? 0
            : std::min({ m_loadsPerFrame, slots, candidates.size() });
        if (toStart == 0) {
            m_dirty = false;
            return;
        }

        const auto candidateOrder = [](const Candidate& a, const Candidate& b) {
            if (a.visible != b.visible) return a.visible;
            if (a.ahead != b.ahead) return a.ahead;
            return a.distance < b.distance;
        };
        std::partial_sort(candidates.begin(), candidates.begin() + toStart,
            candidates.end(), candidateOrder);

        for (size_t i = 0; i < toStart; ++i)
            startLoad(candidates[i].coord);

        pruneBoundsCache();
        m_dirty = false;
    }

    void startLoad(glm::ivec3 cc) {
        if (!m_chunkData) return;
        const Chunk* chunk = m_chunkData->tryGetChunk(cc * Chunk::SIZE);
        if (!chunk || chunk->isEmpty()) { m_failed.insert(cc); return; }

        auto snap = makeSnapshot(*m_chunkData, cc);
        const Side side = m_side;
        m_loadingGeneration[cc] = m_generation;
        m_loading.emplace(cc, std::async(std::launch::async,
            [snap = std::move(snap), noSolidById = m_noSolidById, side]() {
                return computeChunkPartial(snap, noSolidById, side);
            }));
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

        std::vector<std::pair<float, glm::ivec3>> ranked;
        ranked.reserve(m_cache.size());
        for (const auto& [cc, tile] : m_cache)
            ranked.emplace_back(rectDistance(tile.rect, vpRect), cc);

        const auto farthestFirst = [](const auto& a, const auto& b) {
            return a.first > b.first;
        };
        const size_t toEvict = m_cache.size() - m_maxCacheSize;
        if (toEvict < ranked.size())
            std::nth_element(ranked.begin(), ranked.begin() + toEvict,
                ranked.end(), farthestFirst);

        for (size_t i = 0; i < toEvict; ++i) {
            const glm::ivec3 cc = ranked[i].second;
            m_cache.erase(cc);
            m_refresh.erase(cc);
            m_boundsCache.erase(cc);
        }
    }

    void pruneBoundsCache() {
        for (auto it = m_boundsCache.begin(); it != m_boundsCache.end(); ) {
            if (m_cache.count(it->first) || m_loading.count(it->first))
                ++it;
            else
                it = m_boundsCache.erase(it);
        }
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

    static PartialTile computeChunkPartial(
        const ChunkSnapshot& snap,
        const std::unordered_set<uint16_t>& noSolidById,
        Side side)
    {
        PartialTile p;
        const glm::ivec3 chunkMin = snap.chunkMin;

        if (!snap.chunk || snap.chunk->isEmpty()) return p;

        p.topBlocks.reserve((Chunk::SIZE + 2) * (Chunk::SIZE + 2));
        std::unordered_map<glm::ivec2, size_t> topBlockIndex;
        topBlockIndex.reserve((Chunk::SIZE + 2) * (Chunk::SIZE + 2));
        int cxMin = INT_MAX, cyMin = INT_MAX, cxMax = INT_MIN, cyMax = INT_MIN;

        const auto addTopBlock = [&](glm::ivec2 key, glm::ivec3 wp, uint16_t id) {
            const auto [it, inserted] = topBlockIndex.emplace(key, p.topBlocks.size());
            if (inserted) {
                p.topBlocks.emplace_back(key, TopBlock{ wp, (int)id });
                cxMin = std::min(cxMin, key.x); cxMax = std::max(cxMax, key.x);
                cyMin = std::min(cyMin, key.y); cyMax = std::max(cyMax, key.y);
            }
            else if (isCloser(side, wp, p.topBlocks[it->second].second.pos)) {
                p.topBlocks[it->second].second = TopBlock{ wp, (int)id };
            }
            };

        for (int ly = 0; ly < Chunk::SIZE; ++ly) {
            const int worldY = chunkMin.y + ly;
            auto layer = snap.chunk->getLayer(worldY);
            if (layer.empty()) continue;

            for (int lz = 0; lz < Chunk::SIZE; ++lz)
                for (int lx = 0; lx < Chunk::SIZE; ++lx) {
                    const uint16_t id = layer[lz * Chunk::SIZE + lx];
                    if (id == Chunk::AIR || noSolidById.count(id)) continue;

                    const glm::ivec3 wp = chunkMin + glm::ivec3(lx, ly, lz);
                    const glm::ivec2 key = projectKey(side, wp);

                    addTopBlock(key, wp, id);
                }
        }

        if (p.topBlocks.empty()) return p;
        p.offset = { cxMin, cyMin };
        p.rect = { cxMin, cyMin, cxMax - cxMin + 1, cyMax - cyMin + 1 };

        for (const auto& [local, id] : snap.margin) {
            if (noSolidById.count(id)) continue;
            const glm::ivec3 wp = chunkMin + local;
            const glm::ivec2 key = projectKey(side, wp);

            addTopBlock(key, wp, id);
        }

        return p;
    }

    sf::RenderTexture buildTexture(
        const std::vector<std::pair<glm::ivec2, TopBlock>>& topBlocks,
        glm::ivec2 offset,
        int coreW, int coreH)
    {
        if (topBlocks.empty() || !m_shaderLoaded || !textureManager) return {};

        constexpr int TILE = TextureManager::TILE;
        constexpr int P = 1;

        const int W = coreW;
        const int H = coreH;
        if (W <= 0 || H <= 0) return {};

        const int IW = W + 2 * P;
        const int IH = H + 2 * P;

        int minH = INT_MAX;
        for (const auto& [pos, block] : topBlocks)
            minH = std::min(minH, shadingHeight(m_side, block.pos));

        sf::Image idMap({ (unsigned)IW, (unsigned)IH }, sf::Color(0, 0, 0, 0));

        for (const auto& [pos, block] : topBlocks) {
            const int ix = pos.x - offset.x + P;
            const int iy = pos.y - offset.y + P;
            if (ix < 0 || ix >= IW || iy < 0 || iy >= IH) continue;

            const uint16_t slot = textureManager->getAtlasSlot(block.id);
            if (slot == 0) continue;

            const int hAbs = shadingHeight(m_side, block.pos) - minH;
            const uint16_t h16 = (uint16_t)std::clamp(hAbs, 0, 65535);

            idMap.setPixel(
                { (unsigned)ix, (unsigned)iy },
                sf::Color(slot & 0xFF, (slot >> 8) & 0xFF,
                    h16 & 0xFF, (h16 >> 8) & 0xFF));
        }

        sf::Texture idTex;
        idTex.setSmooth(false);
        idTex.setRepeated(false);
        if (!idTex.loadFromImage(idMap)) return {};

        const int OW = IW * TILE;
        const int OH = IH * TILE;

        sf::RenderTexture rt({ (unsigned)OW, (unsigned)OH });
        rt.clear(sf::Color::Transparent);

        shader->setUniform("regionSize", sf::Glsl::Vec2((float)W, (float)H));
        shader->setUniform("idMapSize", sf::Glsl::Vec2((float)IW, (float)IH));
        shader->setUniform("atlasSize", sf::Glsl::Vec2(
            (float)TextureManager::ATLAS_W, (float)TextureManager::ATLAS_H));
        shader->setUniform("atlasGrid", sf::Glsl::Vec2(
            (float)TextureManager::COLS, (float)TextureManager::ROWS));
        shader->setUniform("tileSize", (float)TILE);
        shader->setUniform("atlas", textureManager->getAtlas());
        shader->setUniform("idMap", idTex);

        sf::Sprite sprite(idTex);
        sprite.setScale({ (float)TILE, (float)TILE });

        sf::RenderStates st;
        st.shader = shader.get();
        st.texture = &textureManager->getAtlas();
        rt.draw(sprite, st);
        rt.display();
        return rt;
    }

    const ChunkData* m_chunkData = nullptr;
    std::uint64_t m_dataRevision = 0;
    std::unique_ptr<sf::Shader> shader;
    bool m_shaderLoaded = false;

    Side m_side = Side::Top;

    std::unordered_set<uint16_t> m_noSolidById;
    bool m_idCachesBuilt = false;

    std::unordered_map<glm::ivec3, Tile> m_cache;
    std::unordered_map<glm::ivec3, std::future<PartialTile>> m_loading;
    std::unordered_map<glm::ivec3, std::uint64_t> m_loadingGeneration;
    std::unordered_set<glm::ivec3> m_failed;
    std::unordered_set<glm::ivec3> m_refresh;
    std::unordered_map<glm::ivec3, glm::ivec4> m_boundsCache;

    size_t m_loadsPerFrame = 1;
    size_t m_maxConcurrentLoads = 2;
    size_t m_maxCacheSize = 128;

    bool m_dirty = true;
    std::uint64_t m_generation = 0;

    sf::Texture m_spriteTexture;
    sf::Sprite m_sprite;

    std::vector<std::pair<glm::ivec3, const Tile*>> m_visible;
    glm::ivec4 m_lastVpRect{ INT_MIN, INT_MIN, 0, 0 };
};