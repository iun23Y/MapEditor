#pragma once

#include "schematic.h"
#include "textureManager.h"
#include "helper.h"

#include <SFML/Graphics.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

class schematicTexture {
private:
    SchematicMap* schematic;
    textureManager* textures;

    // --- ТЮН. Меньше regionSize = меньше памяти на регион.
    //     32 → 512×512 px ≈ 1 МБ на картинку (вместо 16.8 МБ при 128).
    static constexpr int regionSize = 128;
    static constexpr int cacheAddSize = 2;
    static constexpr int maxSize = 5;          // ±5 регионов = ~320×320 блоков

    // --- Лимиты очередей — главная защита от утечки
    static constexpr std::size_t MAX_INFLIGHT = 32;   // taskQueue + completedQueue суммарно
    static constexpr std::size_t MAX_COMPLETED = 8;    // готовых картинок в очереди

    std::unordered_map<std::pair<int, int>, sf::Texture, PairHash> regionCache;
    sf::Vector2i cachedMinRegion = { 0, 0 };
    sf::Vector2i cachedMaxRegion = { 0, 0 };
    bool cacheInitialized = false;

    struct RegionTask {
        int rx, rz, priority;
        bool operator<(const RegionTask& other) const { return priority > other.priority; }
    };
    struct RegionResult { int rx, rz; sf::Image image; };

    std::priority_queue<RegionTask>  taskQueue;
    std::deque<RegionResult>        completedQueue;
    std::unordered_set<std::pair<int, int>, PairHash> pendingTasks;

    std::mutex              queueMutex;
    std::condition_variable queueCV;     // будит worker'ов, когда появилась задача
    std::condition_variable notFullCV;   // будит worker'ов, когда освободилось место

    std::vector<std::thread> workers;
    std::atomic<bool>        stopWorkers{ false };   // atomic: читаем без лока

    bool isRegionInsideSchematic(int rx, int rz);
    void processCompletedUploads();
    void workerThread();
    void enqueueRegion(int rx, int rz, int centerRx, int centerRz);
    sf::Image generateRegionImage(int rx, int rz);
    void removeOutdatedRegions(int minRx, int minRz, int maxRx, int maxRz);
    bool isRegionCached(int rx, int rz) const;
    void updateCache(const sf::View& view);

public:
    schematicTexture(SchematicMap* schematic, textureManager* textureManager);
    ~schematicTexture();
    void draw(sf::RenderTarget& target, sf::RenderStates states);
    void updateRegion(sf::Vector2i pos);
};