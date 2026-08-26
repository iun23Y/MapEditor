#pragma once

#include "schematic.h"
#include "schematicTexture.h"
#include "textureManager.h"
#include "helper.h"
#include <algorithm>

#include <SFML/Graphics.hpp>

enum class counterType {
	rectangle,
	polygon,
	circle
};

class Counter {
private:
	std::vector<sf::Vector2f> points;
	counterType type;
	SchematicMap* schem;
	textureManager* texManager;
    bool completed = false;

    std::vector<sf::Vector2i> generateCirclePixels(int cx, int cz, int radius) {
        std::vector<sf::Vector2i> pixels;
        if (radius <= 0) {
            pixels.emplace_back(cx, cz);
            return pixels;
        }

        int x = radius;
        int y = 0;
        int err = 1 - radius;

        while (x >= y) {
            // 8 симметричных точек
            pixels.emplace_back(cx + x, cz + y);
            pixels.emplace_back(cx - x, cz + y);
            pixels.emplace_back(cx + x, cz - y);
            pixels.emplace_back(cx - x, cz - y);
            pixels.emplace_back(cx + y, cz + x);
            pixels.emplace_back(cx - y, cz + x);
            pixels.emplace_back(cx + y, cz - x);
            pixels.emplace_back(cx - y, cz - x);

            ++y;
            if (err < 0) {
                err += 2 * y + 1;
            }
            else {
                --x;
                err += 2 * (y - x) + 1;
            }
        }
        // Удаляем дубликаты (может быть немного повторов на осях)
        std::sort(pixels.begin(), pixels.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return (a.x < b.x) || (a.x == b.x && a.y < b.y);
            });
        pixels.erase(std::unique(pixels.begin(), pixels.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return a.x == b.x && a.y == b.y;
            }), pixels.end());
        return pixels;
    }

	void drawRectanglePreview(sf::RenderWindow& window) {
        sf::Vector2f mousePos = static_cast<sf::Vector2f>(window.mapPixelToCoords(sf::Mouse::getPosition(window)));
        if (points.empty()) return;
        if (points.size() == 1) {
            sf::Vertex line[] = {
                sf::Vertex(points[0], sf::Color(255, 255, 0, 128)),
                sf::Vertex(mousePos, sf::Color(255, 255, 0, 128))
            };
            window.draw(line, 2, sf::PrimitiveType::Lines);
        }
        else if (points.size() == 2) {
            sf::Vertex line1[] = {
                sf::Vertex(points[0], sf::Color::Red),
                sf::Vertex(points[1], sf::Color::Red)
            };
            window.draw(line1, 2, sf::PrimitiveType::Lines);

            sf::Vector2f dir = points[1] - points[0];
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 1e-6f) {
                dir /= len;
                sf::Vector2f perp(-dir.y, dir.x);
                sf::Vector2f toMouse = mousePos - points[1];
                float w = toMouse.x * perp.x + toMouse.y * perp.y;
                sf::Vector2f projected = points[1] + perp * w;
                sf::Vertex line2[] = {
                    sf::Vertex(points[1], sf::Color(255, 255, 0, 128)),
                    sf::Vertex(projected, sf::Color(255, 255, 0, 128))
                };
                window.draw(line2, 2, sf::PrimitiveType::Lines);
            }
        }
        else if (points.size() == 4) {
            for (size_t i = 0; i < 4; ++i) {
                size_t j = (i + 1) % 4;
                sf::Vertex line[] = {
                    sf::Vertex(points[i], sf::Color::Red),
                    sf::Vertex(points[j], sf::Color::Red)
                };
                window.draw(line, 2, sf::PrimitiveType::Lines);
            }
            return;
        }
	}
    void drawPolygonPreview(sf::RenderWindow& window) {
        if (points.empty()) return;
        sf::Vector2f mousePos = static_cast<sf::Vector2f>(window.mapPixelToCoords(sf::Mouse::getPosition(window)));
        for (size_t i = 0; i < points.size() - 1; ++i) {
            sf::Vertex line[] = {
                sf::Vertex(points[i], sf::Color(255, 255, 0, 128)),
                sf::Vertex(points[i + 1], sf::Color(255, 255, 0, 128))
            };
            window.draw(line, 2, sf::PrimitiveType::Lines);
        }
        // Draw line from last point to mouse position
        if (!completed) {
            sf::Vertex line[] = {
                sf::Vertex(points.back(), sf::Color(255, 255, 0, 128)),
                sf::Vertex(mousePos, sf::Color(255, 255, 0, 128))
            };
            window.draw(line, 2, sf::PrimitiveType::Lines);
        }
	}
    void drawCirclePreview(sf::RenderWindow& window) {
        if (points.empty()) return;
        sf::Vector2f mousePos = static_cast<sf::Vector2f>(window.mapPixelToCoords(sf::Mouse::getPosition(window)));
        float radius = 0;
        if (completed) { radius = std::sqrt(std::pow(points[1].x - points[0].x, 2) + std::pow(points[1].y - points[0].y, 2)); }
        else { radius = std::sqrt(std::pow(mousePos.x - points[0].x, 2) + std::pow(mousePos.y - points[0].y, 2)); }
        sf::CircleShape circle(radius);
        circle.setPosition(points[0] - sf::Vector2f(radius, radius));
        circle.setFillColor(sf::Color::Transparent);
        if (completed) { circle.setOutlineColor(sf::Color(255, 0, 0, 128)); }
        else { circle.setOutlineColor(sf::Color(255, 255, 0, 128)); }
        circle.setOutlineThickness(0.1f);
        window.draw(circle);
	}

    void buildRectangle(schematicTexture* schemTex) {
        if (points.size() != 4) return;

        std::vector<sf::Vector2i> borderLocal;
        for (size_t i = 0; i < 4; ++i) {
            size_t j = (i + 1) % 4;
            int ax = static_cast<int>(std::floor(points[i].x));
            int az = static_cast<int>(std::floor(points[i].y));
            int bx = static_cast<int>(std::floor(points[j].x));
            int bz = static_cast<int>(std::floor(points[j].y));
            auto line = bresenhamLine(ax, az, bx, bz);
            borderLocal.insert(borderLocal.end(), line.begin(), line.end());
        }

        std::sort(borderLocal.begin(), borderLocal.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return (a.x < b.x) || (a.x == b.x && a.y < b.y);
            });
        borderLocal.erase(std::unique(borderLocal.begin(), borderLocal.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return a.x == b.x && a.y == b.y;
            }), borderLocal.end());

        sf::Vector3i pos1 = schem->getPos1();
        sf::Vector3i pos2 = schem->getPos2();
        int offsetX = pos1.x;
        int offsetZ = pos1.z;
        int yMinGlobal = pos1.y;
        int yMaxGlobal = pos2.y - 1;

        int xMin = borderLocal[0].x, xMax = borderLocal[0].x;
        int zMin = borderLocal[0].y, zMax = borderLocal[0].y;
        for (const auto& b : borderLocal) {
            xMin = std::min(xMin, b.x);
            xMax = std::max(xMax, b.x);
            zMin = std::min(zMin, b.y);
            zMax = std::max(zMax, b.y);
        }

        auto blocks = schem->getBlocksInArea(xMin + offsetX, yMinGlobal, zMin + offsetZ,
            xMax + offsetX, yMaxGlobal, zMax + offsetZ);

        int minHeight = 0, maxHeight = 0;
        if (!blocks.empty()) {
            minHeight = blocks[0].y;
            maxHeight = blocks[0].y;
            for (const auto& b : blocks) {
                minHeight = std::min(minHeight, b.y);
                maxHeight = std::max(maxHeight, b.y);
            }
        }

        int startY = minHeight - 1;
        int endY = maxHeight + 1;

        auto& palette = schem->getPalette();
        int blockId = palette.getId("minecraft:red_wool");
        if (blockId < 0) {
            int maxId = -1;
            for (const auto& [id, name] : palette.nameById) {
                if (id > maxId) maxId = id;
            }
            int newId = maxId + 1;
            palette.addBlock(newId, "minecraft:red_wool");
            blockId = newId;
        }

        std::vector<std::tuple<int, int, int, int>> blocksToSet;
        blocksToSet.reserve(borderLocal.size() * (endY - startY + 1));

        for (const auto& b : borderLocal) {
            int absX = b.x + offsetX;
            int absZ = b.y + offsetZ;
            schemTex->updateRegion({ absX, absZ });
            for (int y = startY; y <= endY; ++y) {
                blocksToSet.emplace_back(absX, y, absZ, blockId);
            }
        }

        if (!blocksToSet.empty()) {
            schem->setBlocks(blocksToSet);
        }
    }
    void buildPolygon(schematicTexture* schemTex) {
        if (points.size() < 3) return;
        // Замыкаем полигон
        std::vector<sf::Vector2i> borderLocal;
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            int ax = static_cast<int>(std::floor(points[i].x));
            int az = static_cast<int>(std::floor(points[i].y));
            int bx = static_cast<int>(std::floor(points[i + 1].x));
            int bz = static_cast<int>(std::floor(points[i + 1].y));
            auto line = bresenhamLine(ax, az, bx, bz);
            borderLocal.insert(borderLocal.end(), line.begin(), line.end());
        }
        // Замыкаем последнюю с первой
        int ax = static_cast<int>(std::floor(points.back().x));
        int az = static_cast<int>(std::floor(points.back().y));
        int bx = static_cast<int>(std::floor(points.front().x));
        int bz = static_cast<int>(std::floor(points.front().y));
        auto lineClose = bresenhamLine(ax, az, bx, bz);
        borderLocal.insert(borderLocal.end(), lineClose.begin(), lineClose.end());

        // Удаление дубликатов
        std::sort(borderLocal.begin(), borderLocal.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return (a.x < b.x) || (a.x == b.x && a.y < b.y);
            });
        borderLocal.erase(std::unique(borderLocal.begin(), borderLocal.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return a.x == b.x && a.y == b.y;
            }), borderLocal.end());

        // Далее аналогично прямоугольнику – определяем высоты, блоки, устанавливаем
        sf::Vector3i pos1 = schem->getPos1();
        sf::Vector3i pos2 = schem->getPos2();
        int yMinGlobal = pos1.y;
        int yMaxGlobal = pos2.y - 1;

        int xMin = borderLocal[0].x, xMax = borderLocal[0].x;
        int zMin = borderLocal[0].y, zMax = borderLocal[0].y;
        for (const auto& b : borderLocal) {
            xMin = std::min(xMin, b.x);
            xMax = std::max(xMax, b.x);
            zMin = std::min(zMin, b.y);
            zMax = std::max(zMax, b.y);
        }

        auto blocks = schem->getBlocksInArea(xMin + pos1.x, yMinGlobal, zMin + pos1.z,
            xMax + pos1.x, yMaxGlobal, zMax + pos1.z);
        int minHeight = 0, maxHeight = 0;
        if (!blocks.empty()) {
            minHeight = blocks[0].y;
            maxHeight = blocks[0].y;
            for (const auto& b : blocks) {
                minHeight = std::min(minHeight, b.y);
                maxHeight = std::max(maxHeight, b.y);
            }
        }
        int startY = minHeight - 1;
        int endY = maxHeight + 1;

        auto& palette = schem->getPalette();
        int blockId = palette.getId("minecraft:red_wool");
        if (blockId < 0) {
            int maxId = -1;
            for (const auto& [id, name] : palette.nameById) {
                if (id > maxId) maxId = id;
            }
            int newId = maxId + 1;
            palette.addBlock(newId, "minecraft:red_wool");
            blockId = newId;
        }

        std::vector<std::tuple<int, int, int, int>> blocksToSet;
        blocksToSet.reserve(borderLocal.size() * (endY - startY + 1));
        for (const auto& b : borderLocal) {
            int absX = b.x + pos1.x;
            int absZ = b.y + pos1.z;
            schemTex->updateRegion({ absX, absZ });
            for (int y = startY; y <= endY; ++y) {
                blocksToSet.emplace_back(absX, y, absZ, blockId);
            }
        }
        if (!blocksToSet.empty()) {
            schem->setBlocks(blocksToSet);
        }
    }
    void buildCircle(schematicTexture* schemTex) {
        if (points.size() < 2) return;
        sf::Vector2f center = points[0];
        sf::Vector2f onCircle = points[1];
        float dx = onCircle.x - center.x;
        float dy = onCircle.y - center.y;
        float radiusFloat = std::sqrt(dx * dx + dy * dy);
        int radius = static_cast<int>(std::round(radiusFloat));
        if (radius < 1) return;

        int cx = static_cast<int>(std::floor(center.x));
        int cz = static_cast<int>(std::floor(center.y));

        auto borderLocal = generateCirclePixels(cx, cz, radius);

        // Далее аналогично
        sf::Vector3i pos1 = schem->getPos1();
        sf::Vector3i pos2 = schem->getPos2();
        int yMinGlobal = pos1.y;
        int yMaxGlobal = pos2.y - 1;

        int xMin = cx - radius, xMax = cx + radius;
        int zMin = cz - radius, zMax = cz + radius;

        auto blocks = schem->getBlocksInArea(xMin + pos1.x, yMinGlobal, zMin + pos1.z,
            xMax + pos1.x, yMaxGlobal, zMax + pos1.z);
        int minHeight = 0, maxHeight = 0;
        if (!blocks.empty()) {
            minHeight = blocks[0].y;
            maxHeight = blocks[0].y;
            for (const auto& b : blocks) {
                minHeight = std::min(minHeight, b.y);
                maxHeight = std::max(maxHeight, b.y);
            }
        }
        int startY = minHeight - 1;
        int endY = maxHeight + 1;

        auto& palette = schem->getPalette();
        int blockId = palette.getId("minecraft:red_wool");
        if (blockId < 0) {
            int maxId = -1;
            for (const auto& [id, name] : palette.nameById) {
                if (id > maxId) maxId = id;
            }
            int newId = maxId + 1;
            palette.addBlock(newId, "minecraft:red_wool");
            blockId = newId;
        }

        std::vector<std::tuple<int, int, int, int>> blocksToSet;
        blocksToSet.reserve(borderLocal.size() * (endY - startY + 1));
        for (const auto& b : borderLocal) {
            int absX = b.x + pos1.x;
            int absZ = b.y + pos1.z;
            schemTex->updateRegion({ absX, absZ });
            for (int y = startY; y <= endY; ++y) {
                blocksToSet.emplace_back(absX, y, absZ, blockId);
            }
        }
        if (!blocksToSet.empty()) {
            schem->setBlocks(blocksToSet);
        }
    }

    void addPointRectangle(const sf::Vector2f& mousePos) {
        if (points.size() != 2) {
            if (points.size() >= 4) return;
            points.push_back(mousePos);
        } else {
            sf::Vector2f dir = points[1] - points[0];
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1e-6f) return;
            dir /= len;
            sf::Vector2f perp(-dir.y, dir.x);

            sf::Vector2f toMouse = mousePos - points[1];
            float w = toMouse.x * perp.x + toMouse.y * perp.y;
            if (w < 0) {
                perp = -perp;
                w = -w;
            }

            int lenBlocks = static_cast<int>(std::round(len));
            int widthBlocks = static_cast<int>(std::round(w));
            if (lenBlocks == 0 || widthBlocks == 0) return;

            int startX = static_cast<int>(std::floor(points[0].x));
            int startZ = static_cast<int>(std::floor(points[0].y));

            int c1x = startX, c1z = startZ;
            int c2x = startX + static_cast<int>(std::round(dir.x * lenBlocks));
            int c2z = startZ + static_cast<int>(std::round(dir.y * lenBlocks));
            int c3x = c2x + static_cast<int>(std::round(perp.x * widthBlocks));
            int c3z = c2z + static_cast<int>(std::round(perp.y * widthBlocks));
            int c4x = startX + static_cast<int>(std::round(perp.x * widthBlocks));
            int c4z = startZ + static_cast<int>(std::round(perp.y * widthBlocks));

            points.clear();
            points.emplace_back(static_cast<float>(c1x) + 0.5f, static_cast<float>(c1z) + 0.5f);
            points.emplace_back(static_cast<float>(c2x) + 0.5f, static_cast<float>(c2z) + 0.5f);
            points.emplace_back(static_cast<float>(c3x) + 0.5f, static_cast<float>(c3z) + 0.5f);
            points.emplace_back(static_cast<float>(c4x) + 0.5f, static_cast<float>(c4z) + 0.5f);
            completed = true;
        }
    }
    void addPointPolygon(sf::Vector2f mousePos) {
        points.push_back(mousePos);
    }
    void addPointCircle(sf::Vector2f mousePos) {
        if (points.size() < 2) {
            points.push_back(mousePos);
        }
        if (points.size() >= 2) completed = true;
    }
public:
	Counter(SchematicMap* schem, textureManager* texManager, counterType type) : type(type), schem(schem), texManager(texManager) {}

    void drawLinePreview(sf::RenderWindow& window) {
        switch (type) {
        case counterType::rectangle: {
            drawRectanglePreview(window);
            break;
        }
        case counterType::polygon: {
            drawPolygonPreview(window);
            break;
        }
        case counterType::circle: {
            drawCirclePreview(window);
            break;
        }
        }
	}

    void finish(schematicTexture* schemTex) {
        if (type != counterType::polygon) return;
        buildPolygon(schemTex);
        completed = true;
    }

    void buildCounter(schematicTexture* schemTex) {
        switch (type) {
        case counterType::rectangle: {
            buildRectangle(schemTex);
            break;
        }
        case counterType::polygon: {
            buildPolygon(schemTex);
            break;
        }
        case counterType::circle: {
            buildCircle(schemTex);
            break;
        }
        }
        completed = true;
    }

	void addPoint(sf::Vector2f mousePos) {
        if (completed) return;
        switch (type) {
        case counterType::rectangle: {
            addPointRectangle(mousePos);
            break;
        }
        case counterType::polygon: {
            addPointPolygon(mousePos);
            break;
        }
        case counterType::circle: {
            addPointCircle(mousePos);
            break;
        }
        }

	}

    void move(sf::Vector2f move) {
        if (!completed) return;
        for (auto& p : points) p += move;
    }

    bool isCompleted() { return completed; };
};