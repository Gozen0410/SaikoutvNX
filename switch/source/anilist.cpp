#include "anilist.hpp"
#include "anilist_parser.hpp"

#include <curl/curl.h>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* out = static_cast<std::string*>(userdata);
    const size_t bytes = size * nmemb;
    constexpr size_t kMaxResponse = 512 * 1024;
    if (out->size() < kMaxResponse)
    {
        const size_t remaining = kMaxResponse - out->size();
        out->append(ptr, bytes < remaining ? bytes : remaining);
    }
    return bytes;
}
}

AnimeList fetchAniListTrending()
{
    AnimeList empty;

    Result socketRc = socketInitializeDefault();
    bool socketOwned = false;
    if (R_SUCCEEDED(socketRc))
        socketOwned = true;
    else if (socketRc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
        return empty;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        if (socketOwned) socketExit();
        return empty;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        curl_global_cleanup();
        if (socketOwned) socketExit();
        return empty;
    }

    const char* body = "{\"query\":\"query { Page(page: 1, perPage: 6) { media(type: ANIME, sort: TRENDING_DESC) { id title { english romaji native } coverImage { large } format averageScore } } }\"}";
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, "https://graphql.anilist.co");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(body)));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.3");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    const CURLcode requestRc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    AnimeList result;
    if (requestRc == CURLE_OK && httpCode >= 200 && httpCode < 300)
        result = parseAniListTrending(response);

    char logLine[160];
    std::snprintf(logLine, sizeof(logLine), "ANILIST HTTP %ld BYTES %zu PARSED %zu", httpCode, response.size(), result.size());
    FILE* log = std::fopen("sdmc:/switch/SaikouTV/saikou_debug.log", "a");
    if (log)
    {
        std::fprintf(log, "[Saikou] %s\n", logLine);
        for (const AnimeSummary& anime : result)
            std::fprintf(log, "[Saikou] ANILIST CARD id=%d title=%s\n", anime.id, anime.title.c_str());
        std::fflush(log);
        std::fclose(log);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();
    return result;
}
