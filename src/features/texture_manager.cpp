#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../../vendor/stb_image.h"

#include "texture_manager.hpp"
#include <d3d11.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

namespace TextureMgr {

    static ID3D11Device* s_device = nullptr;
    static std::mutex s_mutex;
    static std::atomic<bool> s_shuttingDown{ false };
    static std::atomic<int> s_activeWorkers{ 0 };

    struct TexEntry {
        ID3D11ShaderResourceView* srv = nullptr;
        std::atomic<bool> loading{false};
        std::atomic<bool> failed{false};
        std::chrono::steady_clock::time_point failTime{};
        int retries = 0;
    };

    static std::unordered_map<std::string, TexEntry*> s_cache;

    void Init(ID3D11Device* device) {
        s_shuttingDown.store(false);
        s_device = device;
    }

    // Download raw bytes from a URL using WinINet
    static bool DownloadToMemory(const std::string& url, std::vector<unsigned char>& outData) {
        HINTERNET hInternet = InternetOpenA("MindCheat/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!hInternet) return false;

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (!hUrl) { InternetCloseHandle(hInternet); return false; }

        outData.clear();
        outData.reserve(65536);
        char buf[8192];
        DWORD bytesRead = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
            outData.insert(outData.end(), buf, buf + bytesRead);
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return !outData.empty();
    }

    // Create a DX11 texture from RGBA pixel data (must be called from render thread)
    static ID3D11ShaderResourceView* CreateTextureFromRGBA(const unsigned char* pixels, int w, int h) {
        if (!s_device || !pixels) return nullptr;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pixels;
        initData.SysMemPitch = w * 4;

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = s_device->CreateTexture2D(&desc, &initData, &tex);
        if (FAILED(hr) || !tex) return nullptr;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* srv = nullptr;
        hr = s_device->CreateShaderResourceView(tex, &srvDesc, &srv);
        tex->Release();
        if (FAILED(hr)) return nullptr;

        return srv;
    }

    // Pending decoded images waiting to be turned into textures on the render thread
    struct DecodedImage {
        std::string url;
        unsigned char* pixels;
        int w, h;
    };
    static std::mutex s_pendingMutex;
    static std::vector<DecodedImage> s_pending;

    // Background worker: downloads + decodes, then queues for GPU upload
    static void DownloadAndDecode(TexEntry* entry, std::string url) {
        s_activeWorkers.fetch_add(1);
        std::vector<unsigned char> data;
        if (s_shuttingDown.load() || !DownloadToMemory(url, data)) {
            entry->failed.store(true);
            entry->failTime = std::chrono::steady_clock::now();
            entry->loading.store(false);
            s_activeWorkers.fetch_sub(1);
            return;
        }

        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &channels, 4);
        if (!pixels) {
            entry->failed.store(true);
            entry->failTime = std::chrono::steady_clock::now();
            entry->loading.store(false);
            s_activeWorkers.fetch_sub(1);
            return;
        }

        // Queue for GPU upload on render thread
        if (s_shuttingDown.load()) {
            stbi_image_free(pixels);
            entry->failed.store(true);
            entry->loading.store(false);
            s_activeWorkers.fetch_sub(1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(s_pendingMutex);
            s_pending.push_back({ url, pixels, w, h });
        }

        s_activeWorkers.fetch_sub(1);
    }

    // Process pending GPU uploads — call once per frame from the render thread
    static void ProcessPending() {
        std::lock_guard<std::mutex> lock(s_pendingMutex);
        for (auto& img : s_pending) {
            ID3D11ShaderResourceView* srv = CreateTextureFromRGBA(img.pixels, img.w, img.h);
            stbi_image_free(img.pixels);

            std::lock_guard<std::mutex> cacheLock(s_mutex);
            auto it = s_cache.find(img.url);
            if (it != s_cache.end()) {
                it->second->srv = srv;
                it->second->loading.store(false);
            } else if (srv) {
                srv->Release();
            }
        }
        s_pending.clear();
    }

    static constexpr int MAX_RETRIES = 3;
    static constexpr int RETRY_DELAY_SEC = 5;

    ID3D11ShaderResourceView* Get(const std::string& url) {
        if (url.empty() || !s_device || s_shuttingDown.load()) return nullptr;

        // Process any pending GPU uploads first
        ProcessPending();

        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            TexEntry* e = it->second;
            // Retry failed entries after a delay
            if (e->failed.load() && !e->loading.load() && e->retries < MAX_RETRIES) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - e->failTime).count() >= RETRY_DELAY_SEC) {
                    e->failed.store(false);
                    e->loading.store(true);
                    e->retries++;
                    std::thread(DownloadAndDecode, e, url).detach();
                }
            }
            return e->srv;
        }

        // New entry — start background download
        TexEntry* entry = new TexEntry();
        entry->loading.store(true);
        s_cache[url] = entry;

        std::thread(DownloadAndDecode, entry, url).detach();
        return nullptr;
    }

    void Shutdown() {
        s_shuttingDown.store(true);

        while (s_activeWorkers.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& [url, entry] : s_cache) {
            if (entry->srv) entry->srv->Release();
            delete entry;
        }
        s_cache.clear();

        std::lock_guard<std::mutex> pendLock(s_pendingMutex);
        for (auto& img : s_pending) stbi_image_free(img.pixels);
        s_pending.clear();
        s_device = nullptr;
    }

} // namespace TextureMgr
