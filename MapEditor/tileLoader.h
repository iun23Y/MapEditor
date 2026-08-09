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

enum class TileSource {
    OSM,
    Google,
    Yandex,
    Custom
};

class TileLoader {
private:
    std::unordered_map<std::string, sf::Texture> m_textureCache;
    mutable std::mutex m_cacheMutex;
    static constexpr size_t MAX_CACHE_SIZE = 500;

    TileSource m_source = TileSource::OSM;
    std::string m_customUrl;

    // Построение URL для разных источников (без поддоменов для OSM)
    std::string buildTileUrl(int z, int x, int y) {
        std::string url;

        switch (m_source) {
        case TileSource::OSM: {
            // Используем основной домен без поддоменов
            url = "https://tile.openstreetmap.org/" +
                std::to_string(z) + "/" + std::to_string(x) + "/" +
                std::to_string(y) + ".png";
            break;
        }
        case TileSource::Google: {
            // Используем статический поддомен для Google
            url = "https://mt0.google.com/vt/lyrs=m&x=" +
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
            // Заменяем шаблоны
            size_t pos;
            while ((pos = url.find("{z}")) != std::string::npos)
                url.replace(pos, 3, std::to_string(z));
            while ((pos = url.find("{x}")) != std::string::npos)
                url.replace(pos, 3, std::to_string(x));
            while ((pos = url.find("{y}")) != std::string::npos)
                url.replace(pos, 3, std::to_string(y));
            break;
        }
        }

        std::cout << "Loading tile: " << url << std::endl;
        return url;
    }

    // Версия для const методов
    std::string buildTileUrlConst(int z, int x, int y) const {
        std::string url;

        switch (m_source) {
        case TileSource::OSM: {
            url = "https://tile.openstreetmap.org/" +
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
        case TileSource::Yandex: {
            url = "https://core-renderer-tiles.maps.yandex.net/tiles?l=map&x=" +
                std::to_string(x) + "&y=" + std::to_string(y) + "&z=" +
                std::to_string(z);
            break;
        }
        case TileSource::Custom: {
            url = m_customUrl;
            url = std::regex_replace(url, std::regex("\\{z\\}"), std::to_string(z));
            url = std::regex_replace(url, std::regex("\\{x\\}"), std::to_string(x));
            url = std::regex_replace(url, std::regex("\\{y\\}"), std::to_string(y));
            break;
        }
        }

        return url;
    }

    // Простая загрузка с обработкой редиректов
    sf::Texture downloadTile(const std::string& url) {
        // Парсим URL
        std::string fullUrl = url;
        std::string protocol = "https://";
        std::string host, path;

        if (fullUrl.find("https://") == 0) {
            fullUrl = fullUrl.substr(8);
        }
        else if (fullUrl.find("http://") == 0) {
            fullUrl = fullUrl.substr(7);
            protocol = "http://";
        }

        size_t pos = fullUrl.find("/");
        if (pos != std::string::npos) {
            host = fullUrl.substr(0, pos);
            path = fullUrl.substr(pos);
        }
        else {
            host = fullUrl;
            path = "/";
        }

        // Создаем клиент
        httplib::Client client(host);
        client.set_default_headers({
            {"User-Agent", "MapEditor/1.0 (your_email@example.com)"}
            });
        client.set_connection_timeout(5, 0);
        client.set_read_timeout(10, 0);
        client.enable_server_certificate_verification(false);

        // Пробуем загрузить
        const int MAX_RETRIES = 3;
        for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
            auto response = client.Get(path);

            if (response) {
                if (response->status == 200) {
                    sf::Texture texture;
                    if (texture.loadFromMemory(response->body.data(), response->body.size())) {
                        std::cout << "Successfully loaded: " << url << std::endl;
                        return texture;
                    }
                    else {
                        std::cerr << "Failed to decode image: " << url << std::endl;
                        return sf::Texture();
                    }
                }
                else if (response->status == 301 || response->status == 302) {
                    // Попробуем следовать редиректу вручную
                    auto locationIt = response->headers.find("Location");
                    if (locationIt != response->headers.end()) {
                        std::string newUrl = locationIt->second;
                        std::cout << "Redirect to: " << newUrl << std::endl;

                        // Если редирект на тот же URL, прекращаем
                        if (newUrl == url) {
                            std::cerr << "Redirect loop detected" << std::endl;
                            return sf::Texture();
                        }

                        // Рекурсивно загружаем новый URL
                        return downloadTile(newUrl);
                    }
                }
                else if (response->status == 429 || response->status == 503) {
                    std::cerr << "Rate limited, retrying... (attempt " << attempt + 1 << ")" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
                    continue;
                }
                else {
                    std::cerr << "HTTP error " << response->status << ": " << url << std::endl;
                    return sf::Texture();
                }
            }
            else {
                std::cerr << "No response (attempt " << attempt + 1 << "): " << url << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        std::cerr << "Failed to load after " << MAX_RETRIES << " attempts: " << url << std::endl;
        return sf::Texture();
    }

public:
    TileLoader() {
        // Инициализация
    }

    void setSource(TileSource source) {
        m_source = source;
        clearCache();
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
        // Валидация
        int maxCoord = (1 << z) - 1;
        if (z < 0 || z > 19 || x < 0 || x > maxCoord || y < 0 || y > maxCoord) {
            std::cerr << "Invalid tile: z=" << z << " x=" << x << " y=" << y << std::endl;
            return sf::Texture();
        }

        std::string url = buildTileUrl(z, x, y);
        std::string cacheKey = url;

        // Проверяем кэш
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_textureCache.find(cacheKey);
            if (it != m_textureCache.end()) {
                std::cout << "Cache hit: " << url << std::endl;
                return it->second;
            }
        }

        // Загружаем
        sf::Texture texture = downloadTile(url);

        if (texture.getSize().x > 0) {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_textureCache.size() >= MAX_CACHE_SIZE) {
                m_textureCache.clear();
            }
            m_textureCache[cacheKey] = texture;
        }

        return texture;
    }

    void clearCache() {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_textureCache.clear();
        std::cout << "Cache cleared" << std::endl;
    }

    bool isTileLoaded(int z, int x, int y) const {
        std::string url = buildTileUrlConst(z, x, y);
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        return m_textureCache.find(url) != m_textureCache.end();
    }

    size_t getCacheSize() const {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        return m_textureCache.size();
    }
};