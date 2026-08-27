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

    static constexpr int regionSize = 128;
    static constexpr int cacheAddSize = 2;
    static constexpr int maxSize = 5;
    static constexpr std::size_t MAX_INFLIGHT = 32;
    static constexpr std::size_t MAX_COMPLETED = 8;

    std::unordered_map<std::pair<int, int>, sf::Texture, PairHash> regionCache;
    sf::Vector2i cachedMinRegion = { 0, 0 };
    sf::Vector2i cachedMaxRegion = { 0, 0 };
    bool cacheInitialized = false;

    struct RegionTask {
        int rx, rz, priority;
        bool operator<(const RegionTask& other) const { return priority > other.priority; }
    };
    struct RegionResult { int rx, rz; sf::Image image; };

    std::priority_queue<RegionTask> taskQueue;
    std::deque<RegionResult> completedQueue;
    std::unordered_set<std::pair<int, int>, PairHash> pendingTasks;

    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::condition_variable notFullCV;

    std::vector<std::thread> workers;
    std::atomic<bool> stopWorkers{ false };

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