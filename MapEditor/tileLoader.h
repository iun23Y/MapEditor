// tileLoader.h
#pragma once

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>
#include <SFML/Graphics.hpp>
#include <string>
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <random>
#include <future>
#include <vector>
#include <curl/curl.h>
#include <algorithm>

enum class TileSource {
    OSM_Standard,
    OSM_Hot,
    OSM_Transport,
    Google,
    Google_Satellite,
    Google_Terrain,
    Yandex,
    Custom
};

class TileLoader {
private:
    struct CachedTexture {
        sf::Texture texture;
        std::chrono::steady_clock::time_point lastUsed;
        std::string url;
    };

    std::unordered_map<std::string, CachedTexture> m_textureCache;
    mutable std::mutex m_cacheMutex;
    static constexpr size_t MAX_CACHE_SIZE = 500;

    TileSource m_source = TileSource::OSM_Standard;
    std::string m_customUrl;

    std::vector<std::string> osmSubdomains = { "a", "b", "c" };
    std::vector<std::string> googleSubdomains = { "mt0", "mt1", "mt2", "mt3" };
    std::mt19937 rng{ std::random_device{}() };

    // Используем inline static - C++17
    inline static bool curlInitialized = false;

    // Структура для ответа
    struct MemoryStruct {
        char* memory;
        size_t size;
    };

    // Callback для cURL
    static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t realsize = size * nmemb;
        MemoryStruct* mem = (MemoryStruct*)userp;

        char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
        if (!ptr) {
            std::cerr << "[FAIL] Not enough memory for cURL response" << std::endl;
            return 0;
        }

        mem->memory = ptr;
        memcpy(&(mem->memory[mem->size]), contents, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;

        return realsize;
    }

    // Загрузка через cURL
    std::vector<uint8_t> downloadWithCurl(const std::string& url) {
        if (!curlInitialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curlInitialized = true;
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "[FAIL] Failed to initialize cURL" << std::endl;
            return {};
        }

        MemoryStruct chunk;
        chunk.memory = (char*)malloc(1);
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, br");

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: image/webp,image/png,image/*,*/*;q=0.8");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
        headers = curl_slist_append(headers, "Cache-Control: no-cache");
        headers = curl_slist_append(headers, "Connection: keep-alive");
        headers = curl_slist_append(headers, "Referer: https://www.openstreetmap.org/");

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);

        std::vector<uint8_t> result;
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

            if (response_code == 200) {
                result.assign(chunk.memory, chunk.memory + chunk.size);
                std::cout << "[OK] Loaded: " << url << std::endl;
            }
            else {
                std::cerr << "[FAIL] HTTP error " << response_code << ": " << url << std::endl;
            }
        }
        else {
            std::cerr << "[FAIL] cURL error: " << curl_easy_strerror(res) << " - " << url << std::endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(chunk.memory);

        return result;
    }

    // Создание текстуры из данных
    sf::Texture createTextureFromData(const std::vector<uint8_t>& data) {
        sf::Texture texture;
        if (data.empty()) return texture;

        if (texture.loadFromMemory(data.data(), data.size())) {
            return texture;
        }

        sf::Image image;
        if (image.loadFromMemory(data.data(), data.size())) {
            texture.loadFromImage(image);
            return texture;
        }

        return texture;
    }

    static std::string replaceAll(const std::string& str, const std::string& from, const std::string& to) {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    }

    std::string buildTileUrl(int z, int x, int y) {
        std::string url;

        switch (m_source) {
        case TileSource::OSM_Standard: {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(osmSubdomains.size()) - 1);
            std::string subdomain = osmSubdomains[dist(rng)];
            url = "https://" + subdomain + ".tile.openstreetmap.org/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::OSM_Hot: {
            url = "https://tile-a.openstreetmap.fr/hot/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::OSM_Transport: {
            url = "https://tile.memomaps.de/tilegen/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::Google: {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(googleSubdomains.size()) - 1);
            std::string subdomain = googleSubdomains[dist(rng)];
            url = "https://" + subdomain + ".google.com/vt/lyrs=m&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Google_Satellite: {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(googleSubdomains.size()) - 1);
            std::string subdomain = googleSubdomains[dist(rng)];
            url = "https://" + subdomain + ".google.com/vt/lyrs=s&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Google_Terrain: {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(googleSubdomains.size()) - 1);
            std::string subdomain = googleSubdomains[dist(rng)];
            url = "https://" + subdomain + ".google.com/vt/lyrs=t&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Yandex: {
            url = "https://core-renderer-tiles.maps.yandex.net/tiles?l=map&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Custom: {
            url = m_customUrl;
            url = replaceAll(url, "{z}", std::to_string(z));
            url = replaceAll(url, "{x}", std::to_string(x));
            url = replaceAll(url, "{y}", std::to_string(y));
            break;
        }
        }

        return url;
    }

    std::string buildTileUrlConst(int z, int x, int y) const {
        std::string url;

        switch (m_source) {
        case TileSource::OSM_Standard: {
            url = "https://tile.openstreetmap.org/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::OSM_Hot: {
            url = "https://tile-a.openstreetmap.fr/hot/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::OSM_Transport: {
            url = "https://tile.memomaps.de/tilegen/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::Google: {
            url = "https://mt0.google.com/vt/lyrs=m&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Google_Satellite: {
            url = "https://mt0.google.com/vt/lyrs=s&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Google_Terrain: {
            url = "https://mt0.google.com/vt/lyrs=t&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Yandex: {
            url = "https://core-renderer-tiles.maps.yandex.net/tiles?l=map&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Custom: {
            url = m_customUrl;
            url = replaceAll(url, "{z}", std::to_string(z));
            url = replaceAll(url, "{x}", std::to_string(x));
            url = replaceAll(url, "{y}", std::to_string(y));
            break;
        }
        }

        return url;
    }

    void cleanCache() {
        if (m_textureCache.size() <= MAX_CACHE_SIZE / 2) return;

        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> items;
        for (const auto& pair : m_textureCache) {
            items.push_back({ pair.first, pair.second.lastUsed });
        }

        std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) {
                return a.second < b.second;
            });

        size_t toRemove = items.size() - MAX_CACHE_SIZE / 2;
        for (size_t i = 0; i < toRemove && i < items.size(); ++i) {
            m_textureCache.erase(items[i].first);
        }
    }

public:
    TileLoader() {
        if (!curlInitialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curlInitialized = true;
        }
        std::cout << "[OK] TileLoader initialized" << std::endl;
    }

    ~TileLoader() = default;

    void setSource(TileSource source) {
        m_source = source;
        clearCache();
        std::cout << "Source changed to: " << static_cast<int>(source) << std::endl;
    }

    void setCustomUrl(const std::string& url) {
        m_customUrl = url;
        m_source = TileSource::Custom;
        clearCache();
    }

    TileSource getSource() const {
        return m_source;
    }

    sf::Texture loadTile(int z, int x, int y) {
        int maxCoord = (1 << z) - 1;
        if (z < 0 || z > 19 || x < 0 || x > maxCoord || y < 0 || y > maxCoord) {
            std::cerr << "[FAIL] Invalid tile: z=" << z << " x=" << x << " y=" << y << std::endl;
            return sf::Texture();
        }

        std::string url = buildTileUrl(z, x, y);
        // Create cache key that doesn't depend on subdomain for load balancing
        std::string cacheKey = std::to_string(static_cast<int>(m_source)) + "_" + 
                              std::to_string(z) + "_" + std::to_string(x) + "_" + std::to_string(y);
        if (m_source == TileSource::Custom) {
            cacheKey += "_" + m_customUrl;
        }

        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_textureCache.find(cacheKey);
            if (it != m_textureCache.end()) {
                it->second.lastUsed = std::chrono::steady_clock::now();
                return it->second.texture;
            }
        }

        std::vector<uint8_t> data = downloadWithCurl(url);
        sf::Texture texture = createTextureFromData(data);

        if (texture.getSize().x > 0) {
            std::lock_guard<std::mutex> lock(m_cacheMutex);

            if (m_textureCache.size() >= MAX_CACHE_SIZE) {
                cleanCache();
            }

            CachedTexture cached;
            cached.texture = texture;
            cached.lastUsed = std::chrono::steady_clock::now();
            cached.url = url; // Store the actual URL used
            m_textureCache[cacheKey] = cached;
        }

        return texture;
    }

    sf::Image loadTileImage(int z, int x, int y) {
        // Проверка валидности
        int maxCoord = (1 << z) - 1;
        if (z < 0 || z > 19 || x < 0 || x > maxCoord || y < 0 || y > maxCoord) {
            std::cerr << "[FAIL] Invalid tile: z=" << z << " x=" << x << " y=" << y << std::endl;
            return sf::Image();
        }

        std::string url = buildTileUrl(z, x, y);
        std::vector<uint8_t> data = downloadWithCurl(url);

        sf::Image image;
        if (!data.empty() && image.loadFromMemory(data.data(), data.size())) {
            return image;
        }
        return sf::Image(); // пустое изображение при ошибке
    }

    std::future<sf::Texture> loadTileAsync(int z, int x, int y) {
        return std::async(std::launch::async, [this, z, x, y]() {
            return loadTile(z, x, y);
            });
    }

    bool isTileLoaded(int z, int x, int y) const {
        // Create cache key that doesn't depend on subdomain for load balancing
        std::string cacheKey = std::to_string(static_cast<int>(m_source)) + "_" + 
                              std::to_string(z) + "_" + std::to_string(x) + "_" + std::to_string(y);
        if (m_source == TileSource::Custom) {
            cacheKey += "_" + m_customUrl;
        }
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        return m_textureCache.find(cacheKey) != m_textureCache.end();
    }

    void clearCache() {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_textureCache.clear();
        std::cout << "[OK] Cache cleared" << std::endl;
    }

    size_t getCacheSize() const {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        return m_textureCache.size();
    }

    void printStats() const {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        std::cout << "=== TileLoader Stats ===" << std::endl;
        std::cout << "Cache size: " << m_textureCache.size() << "/" << MAX_CACHE_SIZE << std::endl;
        std::cout << "Source: " << static_cast<int>(m_source) << std::endl;
        std::cout << "========================" << std::endl;
    }

    std::string getTileUrl(int z, int x, int y) const {
        return buildTileUrlConst(z, x, y);
    }
};