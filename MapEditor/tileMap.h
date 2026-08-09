#pragma once
#include <SFML/Graphics.hpp>
#include <unordered_map>
#include <cmath>
#include <tuple>
#include <optional>
#include <memory>
#include "tileLoader.h"
#include "BTEGeoConventor.h"

#ifndef PI
#define PI 3.14159265358979323846
#endif

namespace {
    static std::pair<double, double> tileToLonLat(int x, int y, int z) {
        double n = std::pow(2.0, z);
        double lon = (x / n) * 360.0 - 180.0;
        double latRad = std::atan(std::sinh(PI * (1 - 2 * y / n)));
        double lat = latRad * 180.0 / PI;
        return { lon, lat };
    }

    static std::pair<int, int> lonLatToTile(double lon, double lat, int z) {
        double n = std::pow(2.0, z);
        int x = static_cast<int>((lon + 180.0) / 360.0 * n);
        double latRad = lat * PI / 180.0;
        double mercatorY = std::log(std::tan(PI / 4.0 + latRad / 2.0));
        int y = static_cast<int>((1.0 - mercatorY / PI) * n / 2.0);
        return { x, y };
    }
}

class TileMap : public sf::Drawable {
public:
    TileMap(int zoom, const sf::Vector2f& worldOffset = { 0.f, 0.f })
        : m_zoom(zoom), m_worldOffset(worldOffset) {
    }

    void update(const sf::View& view) {
        sf::FloatRect visibleRect = getVisibleRect(view);
        auto [minX, maxX, minY, maxY] = getTileRange(visibleRect);

        // Загружаем только валидные тайлы
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                if (isValidTile(x, y)) {
                    addTile(x, y);
                }
            }
        }

        cleanup(visibleRect);
    }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const override {
        for (const auto& pair : m_sprites) {
            if (pair.second.has_value()) {
                target.draw(pair.second.value(), states);
            }
        }
    }

    // Переключение источника тайлов
    void setTileSource(TileSource source) {
        m_loader.setSource(source);
        clearAllTiles();
    }

    void setCustomTileSource(const std::string& url) {
        m_loader.setCustomUrl(url);
        clearAllTiles();
    }

    void clearAllTiles() {
        m_sprites.clear();
        m_textureCache.clear();
        m_loader.clearCache();
    }

private:
    BTEGeoConventor::GeoConventor airocean;

    struct TileKey {
        int x, y;
        bool operator==(const TileKey& other) const {
            return x == other.x && y == other.y;
        }
    };

    struct TileKeyHash {
        std::size_t operator()(const TileKey& key) const {
            return std::hash<int>()(key.x) ^ (std::hash<int>()(key.y) << 1);
        }
    };

    int m_zoom;
    sf::Vector2f m_worldOffset;
    std::unordered_map<TileKey, std::optional<sf::Sprite>, TileKeyHash> m_sprites;
    std::unordered_map<TileKey, sf::Texture, TileKeyHash> m_textureCache;
    TileLoader m_loader;

    bool isValidTile(int x, int y) {
        int maxIndex = static_cast<int>(std::pow(2.0, m_zoom)) - 1;
        return x >= 0 && x <= maxIndex && y >= 0 && y <= maxIndex;
    }

    sf::Vector2f tileToWorldPos(int x, int y) {
        auto [lon_min, lat_min] = tileToLonLat(x, y, m_zoom);
        auto [lon_max, lat_max] = tileToLonLat(x + 1, y + 1, m_zoom);
        auto p1 = airocean.fromGeo(static_cast<float>(lon_min), static_cast<float>(lat_min));
        auto p2 = airocean.fromGeo(static_cast<float>(lon_max), static_cast<float>(lat_max));

        float worldX = (p1[0] + p2[0]) / 2.0f + m_worldOffset.x;
        float worldY = (p1[1] + p2[1]) / 2.0f + m_worldOffset.y;

        return { worldX, worldY };
    }

    sf::Vector2f tileSizeInWorld(int x, int y) {
        auto [lon_min, lat_min] = tileToLonLat(x, y, m_zoom);
        auto [lon_max, lat_max] = tileToLonLat(x + 1, y + 1, m_zoom);

        auto p1 = airocean.fromGeo(static_cast<float>(lon_min), static_cast<float>(lat_min));
        auto p2 = airocean.fromGeo(static_cast<float>(lon_max), static_cast<float>(lat_min));
        auto p3 = airocean.fromGeo(static_cast<float>(lon_min), static_cast<float>(lat_max));

        float width = std::abs(p2[0] - p1[0]);
        float height = std::abs(p3[1] - p1[1]);

        return { width, height };
    }

    void addTile(int x, int y) {
        if (!isValidTile(x, y)) return;

        TileKey key{ x, y };
        if (m_sprites.find(key) != m_sprites.end()) return;

        sf::Texture texture;
        auto it = m_textureCache.find(key);
        if (it != m_textureCache.end()) {
            texture = it->second;
        }
        else {
            texture = m_loader.loadTile(m_zoom, x, y);
            if (texture.getSize().x == 0) {
                // Создаем "заглушку" для отсутствующего тайла
                std::cout << "Failed to load tile: z=" << m_zoom << " x=" << x << " y=" << y << std::endl;
                return;
            }
            m_textureCache[key] = texture;
        }

        sf::Sprite sprite(texture);
        sprite.setPosition(tileToWorldPos(x, y));

        sf::Vector2f size = tileSizeInWorld(x, y);
        sprite.setOrigin(sf::Vector2f(texture.getSize().x / 2.0f, texture.getSize().y / 2.0f));
        sprite.setScale(sf::Vector2f(
            size.x / static_cast<float>(texture.getSize().x),
            size.y / static_cast<float>(texture.getSize().y)
        ));

        m_sprites[key] = std::move(sprite);
    }

    sf::FloatRect getVisibleRect(const sf::View& view) {
        sf::Vector2f center = view.getCenter();
        sf::Vector2f size = view.getSize();
        return sf::FloatRect(
            { center.x - size.x / 2,
            center.y - size.y / 2 },
            { size.x,
            size.y }
        );
    }

    std::tuple<int, int, int, int> getTileRange(const sf::FloatRect& rect) {
        sf::Vector2f center = { rect.position.x + rect.size.x / 2,
                               rect.position.y + rect.size.y / 2 };

        sf::Vector2f worldPos = { center.x - m_worldOffset.x, center.y - m_worldOffset.y };
        auto geo = airocean.toGeo(worldPos.x, worldPos.y);
        double lon = geo[0];
        double lat = geo[1];

        auto [centerX, centerY] = lonLatToTile(lon, lat, m_zoom);

        // Вычисляем радиус на основе размера видимой области
        sf::Vector2f topLeft = { rect.position.x - m_worldOffset.x, rect.position.y - m_worldOffset.y };
        sf::Vector2f bottomRight = { rect.position.x + rect.size.x - m_worldOffset.x,
                                     rect.position.y + rect.size.y - m_worldOffset.y };

        auto geoTL = airocean.toGeo(topLeft.x, topLeft.y);
        auto geoBR = airocean.toGeo(bottomRight.x, bottomRight.y);

        double lonDiff = std::abs(geoBR[0] - geoTL[0]);
        double latDiff = std::abs(geoBR[1] - geoTL[1]);

        double n = std::pow(2.0, m_zoom);
        int radiusX = static_cast<int>(std::ceil(lonDiff / 360.0 * n / 2.0)) + 2;
        int radiusY = static_cast<int>(std::ceil(latDiff / 180.0 * n / 2.0)) + 2;

        radiusX = std::min(radiusX, 10);
        radiusY = std::min(radiusY, 10);

        return { centerX - radiusX, centerX + radiusX,
                 centerY - radiusY, centerY + radiusY };
    }

    void cleanup(const sf::FloatRect& visibleRect) {
        sf::FloatRect expandedRect(
            { visibleRect.position.x - 100,
            visibleRect.position.y - 100 },
            {visibleRect.size.x + 200,
            visibleRect.size.y + 200 }
        );

        for (auto it = m_sprites.begin(); it != m_sprites.end(); ) {
            if (it->second.has_value()) {
                sf::FloatRect spriteBounds = it->second->getGlobalBounds();
                if (!rectIntersects(spriteBounds, expandedRect)) {
                    it = m_sprites.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    static bool rectIntersects(const sf::FloatRect& a, const sf::FloatRect& b) {
        return !(a.position.x + a.size.x < b.position.x ||
            b.position.x + b.size.x < a.position.x ||
            a.position.y + a.size.y < b.position.y ||
            b.position.y + b.size.y < a.position.y);
    }
};