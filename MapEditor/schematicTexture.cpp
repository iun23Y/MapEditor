#include "schematicTexture.h"
#include "helper.h"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <iostream>
#include <vector>

bool schematicTexture::isRegionInsideSchematic(int rx, int rz) {
    sf::Vector3i pos1 = schematic->getPos1();
    sf::Vector3i pos2 = schematic->getPos2();
    return (rx < pos2.x && rx + regionSize > pos1.x &&
        rz < pos2.z && rz + regionSize > pos1.z);
}

void schematicTexture::processCompletedUploads() {
    constexpr std::size_t maxUploadsPerFrame = 4;
    for (std::size_t i = 0; i < maxUploadsPerFrame; ++i) {
        RegionResult result;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (completedQueue.empty()) return;
            result = std::move(completedQueue.front());
            completedQueue.pop_front();
            notFullCV.notify_one();          // место освободилось Ч будим worker'а
        }
        sf::Texture texture;
        if (!texture.loadFromImage(result.image))
            continue;
        regionCache[{ result.rx, result.rz }] = std::move(texture);
    }
}

void schematicTexture::workerThread() {
    while (true) {
        RegionTask task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [this]() {
                return stopWorkers.load() || !taskQueue.empty();
                });
            if (stopWorkers.load() && taskQueue.empty()) return;
            if (stopWorkers.load()) return;           // выходим сразу, не дообрабатыва€
            task = taskQueue.top();
            taskQueue.pop();
        }

        try {
            RegionResult result;
            result.rx = task.rx;
            result.rz = task.rz;
            result.image = generateRegionImage(task.rx, task.rz);

            if (stopWorkers.load()) {
                std::lock_guard<std::mutex> lock(queueMutex);
                pendingTasks.erase({ task.rx, task.rz });
                return;
            }

            {
                std::unique_lock<std::mutex> lock(queueMutex);
                notFullCV.wait(lock, [this]() {
                    return stopWorkers.load() || completedQueue.size() < MAX_COMPLETED;
                    });
                if (stopWorkers.load()) {
                    pendingTasks.erase({ task.rx, task.rz });
                    return;
                }
                completedQueue.push_back(std::move(result));
                pendingTasks.erase({ task.rx, task.rz });   // сн€ть с "в работе"
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Region worker error: " << e.what() << '\n';
            std::lock_guard<std::mutex> lock(queueMutex);
            pendingTasks.erase({ task.rx, task.rz });
        }
    }
}

void schematicTexture::enqueueRegion(int rx, int rz, int centerRx, int centerRz) {
    if (!isRegionInsideSchematic(rx, rz)) return;
    if (isRegionCached(rx, rz)) return;

    std::lock_guard<std::mutex> lock(queueMutex);
    const std::pair<int, int> key{ rx, rz };
    if (pendingTasks.contains(key)) return;

    // Ќе enqueue'ить, если в полЄте уже много Ч иначе на старте улетает 400 задач сразу.
    // Ќа следующем кадре часть завершитс€ и эти регионы попадут в очередь.
    const std::size_t inFlight = taskQueue.size() + completedQueue.size();
    if (inFlight >= MAX_INFLIGHT) return;

    const int dx = rx - centerRx, dz = rz - centerRz;
    pendingTasks.insert(key);
    taskQueue.push({ rx, rz, dx * dx + dz * dz });
    queueCV.notify_one();
}

sf::Image schematicTexture::generateRegionImage(int rx, int rz) {
    sf::Image regionImage(
        { static_cast<unsigned int>(regionSize * 16),
          static_cast<unsigned int>(regionSize * 16) },
        sf::Color(0, 0, 0, 0));

    auto blocks = schematic->getTopBlocksInArea(rx, rz,
        rx + regionSize - 1,
        rz + regionSize - 1);

    for (const auto& b : blocks) {
        if (stopWorkers.load(std::memory_order_relaxed))  // ранний выход
            return regionImage;

        const int lx = b.x - rx, lz = b.z - rz;
        if (lx < 0 || lx >= regionSize || lz < 0 || lz >= regionSize) continue;

        const std::string blockName = schematic->getPalette().getName(b.blockId);
        const sf::Image* img = textures->getImage(blockName);
        if (!img) continue;

        regionImage.copy(*img,
            { static_cast<unsigned int>(lx * 16),
              static_cast<unsigned int>(lz * 16) },
            sf::IntRect({ 0, 0 }, { 16, 16 }), true);
    }
    return regionImage;
}

void schematicTexture::removeOutdatedRegions(int minRx, int minRz, int maxRx, int maxRz) {
    std::vector<std::pair<int, int>> toRemove;
    for (const auto& [key, _] : regionCache) {
        if (key.first < minRx || key.first >= maxRx ||
            key.second < minRz || key.second >= maxRz)
            toRemove.push_back(key);
    }
    for (const auto& key : toRemove)
        regionCache.erase(key);
}

bool schematicTexture::isRegionCached(int rx, int rz) const {
    return regionCache.find({ rx, rz }) != regionCache.end();
}

void schematicTexture::updateCache(const sf::View& view) {
    sf::Vector2f center = view.getCenter();
    sf::Vector2f size = view.getSize();

    const float left = center.x - size.x / 2.f;
    const float top = center.y - size.y / 2.f;
    const float right = center.x + size.x / 2.f;
    const float bottom = center.y + size.y / 2.f;

    int minRx = static_cast<int>(std::floor(left / regionSize)) * regionSize;
    int minRz = static_cast<int>(std::floor(top / regionSize)) * regionSize;
    int maxRx = static_cast<int>(std::ceil(right / regionSize)) * regionSize;
    int maxRz = static_cast<int>(std::ceil(bottom / regionSize)) * regionSize;

    minRx -= cacheAddSize * regionSize;
    minRz -= cacheAddSize * regionSize;
    maxRx += cacheAddSize * regionSize;
    maxRz += cacheAddSize * regionSize;

    const int centerRx = static_cast<int>(std::round(center.x / regionSize)) * regionSize;
    const int centerRz = static_cast<int>(std::round(center.y / regionSize)) * regionSize;

    minRx = std::max(minRx, centerRx - maxSize * regionSize);
    minRz = std::max(minRz, centerRz - maxSize * regionSize);
    maxRx = std::min(maxRx, centerRx + maxSize * regionSize);
    maxRz = std::min(maxRz, centerRz + maxSize * regionSize);

    // –аньше тут был ранний return Ч из-за этого removeOutdatedRegions не вызывалс€,
    // пока границы не сдвинутс€. “еперь выполн€ем каждый кадр (дЄшево).
    for (int rx = minRx; rx < maxRx; rx += regionSize) {
        for (int rz = minRz; rz < maxRz; rz += regionSize) {
            if (isRegionCached(rx, rz)) continue;
            if (!isRegionInsideSchematic(rx, rz)) continue;
            enqueueRegion(rx, rz, centerRx, centerRz);
        }
    }
    removeOutdatedRegions(minRx, minRz, maxRx, maxRz);
}

schematicTexture::schematicTexture(SchematicMap* schematic, textureManager* textureManager)
    : schematic(schematic), textures(textureManager) {
    const unsigned int threadCount = std::max(1u, std::min(4u,
        std::thread::hardware_concurrency() > 2
        ? std::thread::hardware_concurrency() - 2 : 1u));
    for (unsigned int i = 0; i < threadCount; ++i)
        workers.emplace_back(&schematicTexture::workerThread, this);
}

schematicTexture::~schematicTexture() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopWorkers = true;
    }
    queueCV.notify_all();
    notFullCV.notify_all();   // разбудить тех, кто ждЄт места в completedQueue
    for (auto& worker : workers)
        if (worker.joinable()) worker.join();
}

void schematicTexture::draw(sf::RenderTarget& target, sf::RenderStates states) {
    sf::View view = target.getView();
    updateCache(view);
    processCompletedUploads();

    for (const auto& [key, texture] : regionCache) {
        sf::Sprite sprite(texture);
        sprite.setPosition({ static_cast<float>(key.first),
                              static_cast<float>(key.second) });
        sprite.setScale({ 1.f / 16.f, 1.f / 16.f });
        target.draw(sprite, states);
    }
}

void schematicTexture::updateRegion(sf::Vector2i pos) {
    const int rx = static_cast<int>(std::floor(static_cast<float>(pos.x) / regionSize)) * regionSize;
    const int rz = static_cast<int>(std::floor(static_cast<float>(pos.y) / regionSize)) * regionSize;
    regionCache.erase({ rx, rz });
    enqueueRegion(rx, rz, rx, rz);
}