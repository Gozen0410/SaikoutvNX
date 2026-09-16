#include "image_cache.hpp"

#include <curl/curl.h>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace
{
static constexpr const char* kCacheDir = "sdmc:/switch/SaikouTV/cache";

static size_t file_write(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    return std::fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata));
}

static bool valid_file(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}
}

std::string ensureAnimeCoverCached(const AnimeSummary& anime)
{
    if (anime.id <= 0 || anime.coverUrl.empty()) return {};

    char path[192];
    std::snprintf(path, sizeof(path), "%s/anilist_cover_%d.jpg", kCacheDir, anime.id);
    const std::string cachePath = path;
    if (valid_file(cachePath)) return cachePath;

    Result socketRc = socketInitializeDefault();
    bool socketOwned = false;
    if (R_SUCCEEDED(socketRc))
        socketOwned = true;
    else if (socketRc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
        return {};

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        if (socketOwned) socketExit();
        return {};
    }

    CURL* curl = curl_easy_init();
    FILE* file = std::fopen(cachePath.c_str(), "wb");
    if (!curl || !file)
    {
        if (file) std::fclose(file);
        if (curl) curl_easy_cleanup(curl);
        curl_global_cleanup();
        if (socketOwned) socketExit();
        std::remove(cachePath.c_str());
        return {};
    }

    curl_easy_setopt(curl, CURLOPT_URL, anime.coverUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.3");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

    const CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    std::fclose(file);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();

    if (rc != CURLE_OK || httpCode < 200 || httpCode >= 300 || !valid_file(cachePath))
    {
        std::remove(cachePath.c_str());
        return {};
    }

    return cachePath;
}
