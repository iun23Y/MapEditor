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
    std::vector<std::tuple<int, int, int>> placedBlocks;
    counterType type;
    SchematicMap* schem;
    textureManager* texManager;
    bool completed = false;
    bool selected = false;
    int contourHeight = 1;

    int getSurfaceHeight(int lx, int lz) const {
        sf::Vector3i pos1 = schem->getPos1();
        for (int y = schem->getPos2().y - 1; y >= pos1.y; --y) {
            if (schem->getBlock(lx + pos1.x, y, lz + pos1.z) != -1) {
                return y; // глобальная Y
            }
        }
        return pos1.y - 1; // если нет блоков
    }
    int getMaxSurfaceHeightInArea(int xMin, int xMax, int zMin, int zMax) const {
        int maxY = schem->getPos1().y - 1;
        for (int lx = xMin; lx <= xMax; ++lx) {
            for (int lz = zMin; lz <= zMax; ++lz) {
                int y = getSurfaceHeight(lx, lz);
                if (y > maxY) maxY = y;
            }
        }
        return maxY;
    }
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
    sf::Color getLineColor() const {
        if (selected) return sf::Color::Cyan;
        if (completed) return sf::Color::Red;
        return sf::Color(255, 255, 0, 128);
    }

    void drawRectanglePreview(sf::RenderWindow& window) {
        sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
        if (points.empty()) return;
        sf::Color lineColor = getLineColor();

        if (points.size() == 1) {
            sf::Vertex line[] = { sf::Vertex(points[0], lineColor), sf::Vertex(mousePos, lineColor) };
            window.draw(line, 2, sf::PrimitiveType::Lines);
        }
        else if (points.size() == 2) {
            sf::Vertex line1[] = { sf::Vertex(points[0], lineColor), sf::Vertex(points[1], lineColor) };
            window.draw(line1, 2, sf::PrimitiveType::Lines);

            sf::Vector2f dir = points[1] - points[0];
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 1e-6f) {
                dir /= len;
                sf::Vector2f perp(-dir.y, dir.x);
                sf::Vector2f toMouse = mousePos - points[1];
                float w = toMouse.x * perp.x + toMouse.y * perp.y;
                sf::Vector2f projected = points[1] + perp * w;
                sf::Vertex line2[] = { sf::Vertex(points[1], lineColor), sf::Vertex(projected, lineColor) };
                window.draw(line2, 2, sf::PrimitiveType::Lines);
            }
        }
        else if (points.size() == 4) {
            for (size_t i = 0; i < 4; ++i) {
                size_t j = (i + 1) % 4;
                sf::Vertex line[] = { sf::Vertex(points[i], lineColor), sf::Vertex(points[j], lineColor) };
                window.draw(line, 2, sf::PrimitiveType::Lines);
            }
        }
    }
    void drawPolygonPreview(sf::RenderWindow& window) {
        if (points.empty()) return;
        sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
        sf::Color lineColor = getLineColor();

        for (size_t i = 0; i + 1 < points.size(); ++i) {
            sf::Vertex line[] = { sf::Vertex(points[i], lineColor), sf::Vertex(points[i + 1], lineColor) };
            window.draw(line, 2, sf::PrimitiveType::Lines);
        }
        if (!completed) {
            sf::Vertex line[] = { sf::Vertex(points.back(), lineColor), sf::Vertex(mousePos, lineColor) };
            window.draw(line, 2, sf::PrimitiveType::Lines);
        }
    }
    void drawCirclePreview(sf::RenderWindow& window) {
        if (points.empty()) return;
        sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
        sf::Color lineColor = getLineColor();

        float radius = 0;
        if (completed) {
            radius = std::sqrt(std::pow(points[1].x - points[0].x, 2) +
                std::pow(points[1].y - points[0].y, 2));
        }
        else {
            radius = std::sqrt(std::pow(mousePos.x - points[0].x, 2) +
                std::pow(mousePos.y - points[0].y, 2));
        }
        if (radius < 0.5f) return;

        sf::CircleShape circle(radius);
        circle.setPosition(points[0] - sf::Vector2f(radius, radius));
        circle.setFillColor(sf::Color::Transparent);
        circle.setOutlineColor(lineColor);
        circle.setOutlineThickness(0.1f);
        window.draw(circle);
    }

    void buildRectangle(schematicTexture* schemTex) {
        if (points.size() != 4) return;

        // Генерация контура
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

        // Удаление дубликатов
        std::sort(borderLocal.begin(), borderLocal.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return (a.x < b.x) || (a.x == b.x && a.y < b.y);
            });
        borderLocal.erase(std::unique(borderLocal.begin(), borderLocal.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return a.x == b.x && a.y == b.y;
            }), borderLocal.end());

        // Определяем область контура
        int xMin = borderLocal[0].x, xMax = borderLocal[0].x;
        int zMin = borderLocal[0].y, zMax = borderLocal[0].y;
        for (const auto& b : borderLocal) {
            xMin = std::min(xMin, b.x);
            xMax = std::max(xMax, b.x);
            zMin = std::min(zMin, b.y);
            zMax = std::max(zMax, b.y);
        }

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

        // Подготовка блоков для установки
        std::vector<std::tuple<int, int, int, int>> blocksToSet;
        blocksToSet.reserve(borderLocal.size() * contourHeight);
        int maxSurfaceY = getMaxSurfaceHeightInArea(xMin, xMax, zMin, zMax);
        if (maxSurfaceY < schem->getPos1().y) maxSurfaceY = schem->getPos1().y - 1;
        int topY = maxSurfaceY + 1;

        sf::Vector3i pos1 = schem->getPos1();
        for (const auto& b : borderLocal) {
            int absX = b.x + pos1.x;
            int absZ = b.y + pos1.z;
            int surfaceY = getSurfaceHeight(b.x, b.y);
            for (int y = surfaceY + 1; y <= topY; ++y) {
                blocksToSet.emplace_back(absX, y, absZ, blockId);
                placedBlocks.emplace_back(b.x, y, b.y); // локальные координаты
            }
            schemTex->updateRegion({ absX, absZ });
        }

        if (!blocksToSet.empty()) {
            schem->setBlocks(blocksToSet);
        }
    }
    void buildPolygon(schematicTexture* schemTex) {
        if (points.size() < 3) return;

        std::vector<sf::Vector2i> borderLocal;
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            int ax = static_cast<int>(std::floor(points[i].x));
            int az = static_cast<int>(std::floor(points[i].y));
            int bx = static_cast<int>(std::floor(points[i + 1].x));
            int bz = static_cast<int>(std::floor(points[i + 1].y));
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

        int xMin = borderLocal[0].x, xMax = borderLocal[0].x;
        int zMin = borderLocal[0].y, zMax = borderLocal[0].y;
        for (const auto& b : borderLocal) {
            xMin = std::min(xMin, b.x);
            xMax = std::max(xMax, b.x);
            zMin = std::min(zMin, b.y);
            zMax = std::max(zMax, b.y);
        }

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
        blocksToSet.reserve(borderLocal.size() * contourHeight);
        int maxSurfaceY = getMaxSurfaceHeightInArea(xMin, xMax, zMin, zMax);
        if (maxSurfaceY < schem->getPos1().y) maxSurfaceY = schem->getPos1().y - 1;
        int topY = maxSurfaceY + 1;

        sf::Vector3i pos1 = schem->getPos1();
        for (const auto& b : borderLocal) {
            int absX = b.x + pos1.x;
            int absZ = b.y + pos1.z;
            int surfaceY = getSurfaceHeight(b.x, b.y);
            for (int y = surfaceY + 1; y <= topY; ++y) {
                blocksToSet.emplace_back(absX, y, absZ, blockId);
                placedBlocks.emplace_back(b.x, y, b.y);
            }
            schemTex->updateRegion({ absX, absZ });
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

        // Область контура
        int xMin = cx - radius, xMax = cx + radius;
        int zMin = cz - radius, zMax = cz + radius;

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
        blocksToSet.reserve(borderLocal.size() * contourHeight);
        int maxSurfaceY = getMaxSurfaceHeightInArea(xMin, xMax, zMin, zMax);
        if (maxSurfaceY < schem->getPos1().y) maxSurfaceY = schem->getPos1().y - 1;
        int topY = maxSurfaceY + 1;

        sf::Vector3i pos1 = schem->getPos1();
        for (const auto& b : borderLocal) {
            int absX = b.x + pos1.x;
            int absZ = b.y + pos1.z;
            int surfaceY = getSurfaceHeight(b.x, b.y);
            for (int y = surfaceY + 1; y <= topY; ++y) {
                blocksToSet.emplace_back(absX, y, absZ, blockId);
                placedBlocks.emplace_back(b.x, y, b.y);
            }
            schemTex->updateRegion({ absX, absZ });
        }

        if (!blocksToSet.empty()) {
            schem->setBlocks(blocksToSet);
        }
    }

    void addPointRectangle(const sf::Vector2f& mousePos) {
        if (points.size() != 2) {
            if (points.size() >= 4) return;
            points.push_back(mousePos);
        }
        else {
            sf::Vector2f dir = points[1] - points[0];
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1e-6f) return;
            dir /= len;
            sf::Vector2f perp(-dir.y, dir.x);

            sf::Vector2f toMouse = mousePos - points[1];
            float w = toMouse.x * perp.x + toMouse.y * perp.y;
            if (w < 0) { perp = -perp; w = -w; }

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
            points.emplace_back(static_cast<float>(c1x), static_cast<float>(c1z));
            points.emplace_back(static_cast<float>(c2x), static_cast<float>(c2z));
            points.emplace_back(static_cast<float>(c3x), static_cast<float>(c3z));
            points.emplace_back(static_cast<float>(c4x), static_cast<float>(c4z));
            completed = true;
        }
    }
    void addPointPolygon(const sf::Vector2f& mousePos) {
        points.push_back(mousePos);
    }
    void addPointCircle(const sf::Vector2f& mousePos) {
        if (points.size() < 2) {
            points.push_back(mousePos);
        }
        if (points.size() >= 2) completed = true;
    }

public:
    Counter(SchematicMap* schem, textureManager* texManager, counterType type)
        : schem(schem), texManager(texManager), type(type), selected(false), completed(false) {
    }

    void setSelected(bool sel) { selected = sel; }
    bool isSelected() const { return selected; }

    std::vector<sf::Vector2i> buildBorder() {
        std::vector<sf::Vector2i> border;
        switch (type) {
        case counterType::rectangle: {
            if (points.size() < 4) break;
            for (size_t i = 0; i < 4; ++i) {
                size_t j = (i + 1) % 4;
                int ax = static_cast<int>(std::floor(points[i].x));
                int az = static_cast<int>(std::floor(points[i].y));
                int bx = static_cast<int>(std::floor(points[j].x));
                int bz = static_cast<int>(std::floor(points[j].y));
                auto line = bresenhamLine(ax, az, bx, bz);
                border.insert(border.end(), line.begin(), line.end());
            }
            break;
        }
        case counterType::polygon: {
            if (points.size() < 3) break;
            for (size_t i = 0; i + 1 < points.size(); ++i) {
                int ax = static_cast<int>(std::floor(points[i].x));
                int az = static_cast<int>(std::floor(points[i].y));
                int bx = static_cast<int>(std::floor(points[i + 1].x));
                int bz = static_cast<int>(std::floor(points[i + 1].y));
                auto line = bresenhamLine(ax, az, bx, bz);
                border.insert(border.end(), line.begin(), line.end());
            }
            int ax = static_cast<int>(std::floor(points.back().x));
            int az = static_cast<int>(std::floor(points.back().y));
            int bx = static_cast<int>(std::floor(points.front().x));
            int bz = static_cast<int>(std::floor(points.front().y));
            auto line = bresenhamLine(ax, az, bx, bz);
            border.insert(border.end(), line.begin(), line.end());
            break;
        }
        case counterType::circle: {
            if (points.size() < 2) break;
            float dx = points[1].x - points[0].x;
            float dy = points[1].y - points[0].y;
            int radius = static_cast<int>(std::round(std::sqrt(dx * dx + dy * dy)));
            if (radius <= 0) break;
            int cx = static_cast<int>(std::floor(points[0].x));
            int cz = static_cast<int>(std::floor(points[0].y));
            auto pixels = generateCirclePixels(cx, cz, radius);
            border.insert(border.end(), pixels.begin(), pixels.end());
            break;
        }
        }
        std::sort(border.begin(), border.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return (a.x < b.x) || (a.x == b.x && a.y < b.y);
            });
        border.erase(std::unique(border.begin(), border.end(),
            [](const sf::Vector2i& a, const sf::Vector2i& b) {
                return a.x == b.x && a.y == b.y;
            }), border.end());
        return border;
    }
    void removePlacedBlocks(schematicTexture* schemTex) {
        if (placedBlocks.empty()) return;
        std::vector<std::tuple<int, int, int, int>> blocksToRemove;
        blocksToRemove.reserve(placedBlocks.size());
        sf::Vector3i pos1 = schem->getPos1();
        for (const auto& localPos : placedBlocks) {
            int absX = std::get<0>(localPos) + pos1.x;
            int absY = std::get<1>(localPos);
            int absZ = std::get<2>(localPos) + pos1.z;
            blocksToRemove.emplace_back(absX, absY, absZ, -1);
            schemTex->updateRegion({ absX, absZ });
        }
        schem->setBlocks(blocksToRemove);
        placedBlocks.clear();
    }

    void drawLinePreview(sf::RenderWindow& window) {
        switch (type) {
        case counterType::rectangle: 
        {
            drawRectanglePreview(window);
            break;
        }
        case counterType::polygon: 
        {
            drawPolygonPreview(window);
            break;
        }
        case counterType::circle: 
        {
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
        case counterType::rectangle: 
        {
            buildRectangle(schemTex);
            break;
        }
        case counterType::polygon:  
        {
            buildPolygon(schemTex);
            break;
        }
        case counterType::circle: 
        {
            buildCircle(schemTex);
            break;
        }
        }
        completed = true;
    }
    void addPoint(const sf::Vector2f& mousePos) {
        if (completed) return;
        switch (type) {
        case counterType::rectangle: 
        {
            addPointRectangle(mousePos);
            break;
        }
        case counterType::polygon: 
        {
            addPointPolygon(mousePos);
            break;
        }
        case counterType::circle: 
        {
            addPointCircle(mousePos);
            break;
        }
        }
    }
    void move(const sf::Vector2f& delta) {
        if (!completed) return;
        for (auto& p : points) {
            p += delta;
        }
    }

    bool isCompleted() const { return completed; }
    const std::vector<sf::Vector2f>& getPoints() const { return points; }
    counterType getType() const { return type; }
};