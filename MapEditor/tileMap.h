#pragma once

#include <SFML/Graphics.hpp>
#include <unordered_map>
#include <cmath>
#include <tuple>
#include <optional>
#include <memory>
#include "tileLoader.h"
#include <future>
#include <mutex>
#include <algorithm>
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

class TileMap {
private:
    // Структура для хранения ТОЛЬКО текстуры и позиции
    enum class TileState {
        Empty,
        Loading,
        Loaded
    };

    struct TileData {
        TileState state = TileState::Empty;
        sf::Texture texture;
        sf::Image loadingImage;
        sf::Transform transform;
        bool valid = false;

        TileData() = default;
        TileData(const sf::Transform& tr)
            : state(TileState::Loading), transform(tr), valid(false) {
        }

        bool isReady() const {
            return valid && texture.getSize().x > 0 && texture.getSize().y > 0;
        }
    };

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

    mutable BTEGeoConventor::GeoConventor airocean;
    int m_zoom;
    sf::Vector2f m_worldOffset;

    // Храним ТОЛЬКО текстуры с позициями
    std::unordered_map<TileKey, TileData, TileKeyHash> m_tiles;
    mutable std::mutex m_tilesMutex;
    std::vector<std::future<void>> m_futures;
    TileLoader m_loader;

    bool isValidTile(int x, int y) {
        int maxIndex = static_cast<int>(std::pow(2.0, m_zoom)) - 1;
        return x >= 0 && x <= maxIndex && y >= 0 && y <= maxIndex;
    }

    void addTile(int x, int y) {
        if (!isValidTile(x, y)) return;
        TileKey key{ x, y };

        // 1. Проверяем, не загружается ли уже или не загружен
        {
            std::lock_guard<std::mutex> lock(m_tilesMutex);
            auto it = m_tiles.find(key);
            if (it != m_tiles.end()) {
                if (it->second.state == TileState::Loaded) return;
                if (it->second.state == TileState::Loading) return;
            }
        }

        // 2. Вычисляем трансформацию (как в вашем синхронном коде)
        //    Используем стандартный размер тайла 256x256 (все OSM/Google тайлы такие)
        const float TILE_SIZE = 256.f;

        auto [lon_min, lat_min] = tileToLonLat(x, y, m_zoom);
        auto [lon_max, lat_max] = tileToLonLat(x + 1, y + 1, m_zoom);

        auto p1 = airocean.fromGeo(lat_min, lon_min); // юго-запад
        auto p2 = airocean.fromGeo(lat_min, lon_max); // юго-восток
        auto p3 = airocean.fromGeo(lat_max, lon_min); // северо-запад

        sf::Vector2f worldOrigin(p1[0] - m_worldOffset.x, p1[1] - m_worldOffset.y);
        sf::Vector2f worldXVec(p2[0] - p1[0], p2[1] - p1[1]);
        sf::Vector2f worldYVec(p3[0] - p1[0], p3[1] - p1[1]);

        sf::Transform transform;
        transform.translate(worldOrigin);
        transform.combine(sf::Transform(
            worldXVec.x / TILE_SIZE, worldXVec.y / TILE_SIZE, 0.f,
            worldYVec.x / TILE_SIZE, worldYVec.y / TILE_SIZE, 0.f,
            0.f, 0.f, 1.f
        ));

        // 3. Создаём запись тайла в состоянии Loading
        {
            std::lock_guard<std::mutex> lock(m_tilesMutex);
            m_tiles[key] = TileData(transform); // конструктор с трансформацией
        }

        // 4. Запускаем фоновую загрузку
        m_futures.push_back(std::async(std::launch::async, [this, key]() {
            sf::Image image = m_loader.loadTileImage(m_zoom, key.x, key.y);
            std::lock_guard<std::mutex> lock(m_tilesMutex);
            auto it = m_tiles.find(key);
            if (it != m_tiles.end() && it->second.state == TileState::Loading) {
                if (image.getSize().x > 0 && image.getSize().y > 0) {
                    it->second.loadingImage = std::move(image);
                    it->second.state = TileState::Loaded; // изображение загружено, ждём создания текстуры
                }
                else {
                    // Ошибка загрузки – сбрасываем состояние
                    it->second.state = TileState::Empty;
                }
            }
            }));
    }

    sf::FloatRect getVisibleRect(const sf::View& view) {
        sf::Vector2f center = view.getCenter();
        sf::Vector2f size = view.getSize();
        return sf::FloatRect(
            { center.x - size.x / 2, center.y - size.y / 2 },
            { size.x, size.y }
        );
    }

    void processLoadedTextures() {
        std::lock_guard<std::mutex> lock(m_tilesMutex);
        for (auto& pair : m_tiles) {
            auto& data = pair.second;
            if (data.state == TileState::Loaded && data.texture.getSize().x == 0) {
                if (data.loadingImage.getSize().x > 0) {
                    data.texture.loadFromImage(data.loadingImage);
                    data.loadingImage = sf::Image(); // освобождаем память
                    data.valid = true;
                }
                else {
                    data.valid = false;
                    data.state = TileState::Empty;
                }
            }
        }
    }

    void cleanFutures() {
        m_futures.erase(std::remove_if(m_futures.begin(), m_futures.end(),
            [](const std::future<void>& f) {
                return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            }),
            m_futures.end());
    }

    std::tuple<int, int, int, int> getTileRange(const sf::FloatRect& rect) {
        sf::Vector2f center = { rect.position.x + rect.size.x / 2,
                               rect.position.y + rect.size.y / 2 };

        sf::Vector2f worldCenterPos = { center.x + m_worldOffset.x, center.y + m_worldOffset.y };
        auto geo = airocean.toGeo(worldCenterPos.x, worldCenterPos.y);
        double lon = geo[1];
        double lat = geo[0];

        auto [centerX, centerY] = lonLatToTile(lon, lat, m_zoom);

        sf::Vector2f topLeft = { rect.position.x + m_worldOffset.x, rect.position.y + m_worldOffset.y };
        sf::Vector2f bottomRight = { rect.position.x + rect.size.x + m_worldOffset.x,
                             rect.position.y + rect.size.y + m_worldOffset.y };

        auto geoTL = airocean.toGeo(topLeft.x, topLeft.y);
        auto geoBR = airocean.toGeo(bottomRight.x, bottomRight.y);

        double lonDiff = std::abs(geoBR[1] - geoTL[1]);
        double latDiff = std::abs(geoBR[0] - geoTL[0]);

        double n = std::pow(2.0, m_zoom);
        int radiusX = static_cast<int>(std::ceil(lonDiff / 360.0 * n / 2.0)) + 2;
        int radiusY = static_cast<int>(std::ceil(latDiff / 180.0 * n / 2.0)) + 2;

        radiusX = std::min(radiusX, 10);
        radiusY = std::min(radiusY, 10);

        return { centerX - radiusX, centerX + radiusX,
                 centerY - radiusY, centerY + radiusY };
    }
public:
    TileMap(int zoom, const sf::Vector2f& worldOffset = { 0.f, 0.f })
        : m_zoom(zoom), m_worldOffset(worldOffset) {
    }

    void update(const sf::View& view) {
        sf::FloatRect visibleRect = getVisibleRect(view);
        auto [minX, maxX, minY, maxY] = getTileRange(visibleRect);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                if (isValidTile(x, y)) {
                    addTile(x, y);
                }
            }
        }
        processLoadedTextures();
        cleanFutures();
    }

    void draw(sf::RenderTarget& target, sf::RenderStates states) const {
        // Если используете многопоточность – раскомментируйте следующую строку:
        std::lock_guard<std::mutex> lock(m_tilesMutex);

        for (const auto& pair : m_tiles) {
            const auto& data = pair.second;
            if (!data.valid) continue;

            auto [lon_min, lat_min] = tileToLonLat(pair.first.x, pair.first.y, m_zoom);
            auto [lon_max, lat_max] = tileToLonLat(pair.first.x + 1, pair.first.y + 1, m_zoom);

            auto p1 = airocean.fromGeo(lat_min, lon_min);
            auto p2 = airocean.fromGeo(lat_min, lon_max);
            auto p3 = airocean.fromGeo(lat_max, lon_min);
            auto p4 = airocean.fromGeo(lat_max, lon_max);

            // 3. Подготавливаем массив вершин (6 штук для 2-х треугольников)
            sf::Vector2u texSize = pair.second.texture.getSize();

            // Координаты текстуры (UV) – отзеркалены по вертикали (Y перевёрнут)
            sf::Vector2f uv1(0.f, static_cast<float>(texSize.y));   // было (0,0)
            sf::Vector2f uv2(static_cast<float>(texSize.x), static_cast<float>(texSize.y)); // было (x,0)
            sf::Vector2f uv3(0.f, 0.f);                             // было (0, texSize.y)
            sf::Vector2f uv4(static_cast<float>(texSize.x), 0.f);   // было (x, texSize.y)

            // Применяем m_worldOffset к координатам мира
            sf::Vector2f w1(p1[0] - m_worldOffset.x, p1[1] - m_worldOffset.y); // P1 (ЛН)
            sf::Vector2f w2(p2[0] - m_worldOffset.x, p2[1] - m_worldOffset.y); // P2 (ПН)
            sf::Vector2f w3(p3[0] - m_worldOffset.x, p3[1] - m_worldOffset.y); // P3 (ЛВ)
            sf::Vector2f w4(p4[0] - m_worldOffset.x, p4[1] - m_worldOffset.y); // P4 (ПВ)

            sf::VertexArray vertices(sf::PrimitiveType::Triangles, 6);

            // Треугольник 1 (ЛВ -> ПВ -> ЛН)
            vertices[0].position = w3;
            vertices[0].texCoords = uv1; // (0, texSize.y) – левый верх теперь стал левым низом

            vertices[1].position = w4;
            vertices[1].texCoords = uv2; // (texSize.x, texSize.y) – правый верх стал правым низом

            vertices[2].position = w1;
            vertices[2].texCoords = uv3; // (0, 0) – левый низ стал левым верхом

            // Треугольник 2 (ПВ -> ПН -> ЛН)
            vertices[3].position = w4;
            vertices[3].texCoords = uv2; // (texSize.x, texSize.y)

            vertices[4].position = w2;
            vertices[4].texCoords = uv4; // (texSize.x, 0) – правый низ стал правым верхом

            vertices[5].position = w1;
            vertices[5].texCoords = uv3; // (0, 0)

            sf::Color alphaColor(255, 255, 255, 128); // 128 = 50% прозрачности

            for (int i = 0; i < 6; ++i) {
                vertices[i].color = alphaColor;
            }

            states.texture = &pair.second.texture;
            target.draw(vertices, states);
        }
    }

    void setTileSource(TileSource source) {
        m_loader.setSource(source);
        clearAllTiles();
    }

    void setCustomTileSource(const std::string& url) {
        m_loader.setCustomUrl(url);
        clearAllTiles();
    }

    void clearAllTiles() {
        m_tiles.clear();
        m_loader.clearCache();
    }

};