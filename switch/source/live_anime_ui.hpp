#pragma once

#include <borealis.hpp>
#include <curl/curl.h>
#include <switch/applets/swkbd.h>
#include <switch/services/nifm.h>
#include <switch.h>
#include "api_sources.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static void log_stage(const char* stage);

static const auto g_perfStart = std::chrono::steady_clock::now();

static long long perf_elapsed_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_perfStart).count();
}

static void perf_log(const char* stage)
{
    char marker[224];
    std::snprintf(marker, sizeof(marker), "PERF %lldms | %s", perf_elapsed_ms(), stage);
    log_stage(marker);
}

static void perf_log_count(const char* stage, size_t count)
{
    char marker[224];
    std::snprintf(marker, sizeof(marker), "PERF %lldms | %s count=%zu",
        perf_elapsed_ms(), stage, count);
    log_stage(marker);
}

struct SaikouAnime
{
    int id = 0;
    int score = 0;
    int episodes = 0;
    std::string title;
    std::string coverUrl;
    std::string bannerUrl;
    std::string description;
    std::string format;
    std::string status;
    std::string posterPath;
};

static bool g_providerEnabled[kApiSourceCount] = { true, true, true, true, true };
static int g_selectedApiSource = 0;
static bool g_restoreGlobalQuitAfterKeyboard = false;
static std::atomic<unsigned int> g_anilistAccountRevision{ 0 };
static bool g_socketOwned = false;
static bool g_curlOwned = false;
static constexpr const char* kSettingsPath = "sdmc:/switch/SaikouTV/settings.ini";
static constexpr const char* kAniListTokenPath = "sdmc:/switch/SaikouTV/anilistToken";
static constexpr const char* kCacheDir = "sdmc:/switch/SaikouTV/cache";
static constexpr const char* kLocalContinuePath = "sdmc:/switch/SaikouTV/continue.ini";
static constexpr size_t kLocalContinueCapacity = 120;

static void register_page_back_action(brls::View* root)
{
    if (!root)
        return;

    root->registerAction("Back", brls::BUTTON_B, [](brls::View*) {
        log_stage("NAV ACTION: Back");
        // Pop after the input traversal has released its current View pointers.
        brls::sync([] {
            const bool popped = brls::Application::popActivity(brls::TransitionAnimation::NONE, [] {}, true);
            log_stage(popped ? "NAV BACK: returned to previous screen" : "NAV BACK: no previous screen");
        });
        return true;
    });
}

static bool ensure_network_ready()
{
    static std::once_flag once;
    static Result socketResult = MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    std::call_once(once, [] {
        socketResult = socketInitializeDefault();
        g_socketOwned = R_SUCCEEDED(socketResult);
    });
    return R_SUCCEEDED(socketResult) ||
        socketResult == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
}

static bool ensure_curl_ready()
{
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] {
        result = curl_global_init(CURL_GLOBAL_DEFAULT);
        g_curlOwned = result == CURLE_OK;
    });
    return result == CURLE_OK;
}

static void shutdown_network()
{
    // All activity-owned network workers are joined during Borealis Application::exit()
    // before libnx calls userAppExit(). Release cURL first, then our socket service.
    if (g_curlOwned)
    {
        curl_global_cleanup();
        g_curlOwned = false;
    }

    if (g_socketOwned)
    {
        socketExit();
        g_socketOwned = false;
    }
}

extern "C" void userAppExit(void)
{
    shutdown_network();
}

static size_t append_http_data(char* data, size_t size, size_t count, void* userdata)
{
    std::string* output = static_cast<std::string*>(userdata);
    const size_t bytes = size * count;
    static constexpr size_t maxResponseBytes = 2 * 1024 * 1024;
    if (!output || output->size() + bytes > maxResponseBytes)
        return 0;
    output->append(data, bytes);
    return bytes;
}

static bool http_request(const std::string& url, const std::string* postBody,
    std::string& response, long timeoutSeconds = 10, const std::string* bearerToken = nullptr)
{
    if (!ensure_network_ready() || !ensure_curl_ready())
        return false;

    static constexpr int kMaxAttempts = 2;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        response.clear();

        CURL* curl = curl_easy_init();
        if (!curl)
            return false;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        if (postBody)
            headers = curl_slist_append(headers, "Content-Type: application/json");
        if (bearerToken && !bearerToken->empty())
        {
            const std::string authHeader = "Authorization: Bearer " + *bearerToken;
            headers = curl_slist_append(headers, authHeader.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouTV-NX/0.3");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_http_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        if (postBody)
        {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody->c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postBody->size()));
        }

        const CURLcode requestResult = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

        const bool success =
            requestResult == CURLE_OK && httpCode >= 200 && httpCode < 300;

        const bool retryableHttp =
            httpCode == 429 || httpCode >= 500;

        const bool retryableCurl =
            requestResult == CURLE_OPERATION_TIMEDOUT ||
            requestResult == CURLE_COULDNT_CONNECT ||
            requestResult == CURLE_COULDNT_RESOLVE_HOST ||
            requestResult == CURLE_RECV_ERROR ||
            requestResult == CURLE_SEND_ERROR ||
            requestResult == CURLE_GOT_NOTHING;

        char marker[256];
        std::snprintf(marker, sizeof(marker),
            "HTTP ATTEMPT %d/%d result=%d http=%ld bytes=%zu retry=%d url=%.120s",
            attempt, kMaxAttempts, static_cast<int>(requestResult), httpCode,
            response.size(),
            (!success && attempt < kMaxAttempts && (retryableHttp || retryableCurl)) ? 1 : 0,
            url.c_str());
        log_stage(marker);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (success)
            return true;

        if (attempt < kMaxAttempts && (retryableHttp || retryableCurl))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            continue;
        }

        return false;
    }

    return false;
}

static void append_utf8(std::string& out, unsigned int cp)
{
    if (cp <= 0x7F)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static unsigned int read_hex4(const std::string& text, size_t at)
{
    unsigned int value = 0;
    for (size_t i = 0; i < 4 && at + i < text.size(); ++i)
    {
        const char c = text[at + i];
        value <<= 4;
        if (c >= '0' && c <= '9') value += static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') value += static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value += static_cast<unsigned int>(c - 'A' + 10);
    }
    return value;
}

static std::string decode_json_string(const std::string& json, size_t quoteAt, size_t* endAt = nullptr)
{
    std::string out;
    if (quoteAt >= json.size() || json[quoteAt] != '"')
        return out;

    for (size_t i = quoteAt + 1; i < json.size(); ++i)
    {
        const char c = json[i];
        if (c == '"')
        {
            if (endAt) *endAt = i + 1;
            return out;
        }
        if (c != '\\')
        {
            out.push_back(c);
            continue;
        }
        if (++i >= json.size())
            break;
        switch (json[i])
        {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                unsigned int cp = read_hex4(json, i + 1);
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < json.size() &&
                    json[i + 1] == '\\' && json[i + 2] == 'u')
                {
                    const unsigned int low = read_hex4(json, i + 3);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }
                append_utf8(out, cp);
                break;
            }
            default: out.push_back(json[i]); break;
        }
    }
    if (endAt) *endAt = json.size();
    return out;
}

static size_t find_json_string_end(const std::string& json, size_t start)
{
    bool escaped = false;
    for (size_t i = start + 1; i < json.size(); ++i)
    {
        if (escaped) { escaped = false; continue; }
        if (json[i] == '\\') { escaped = true; continue; }
        if (json[i] == '"') return i;
    }
    return std::string::npos;
}

static size_t json_field_value(const std::string& json, const std::string& key, size_t from = 0)
{
    const std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle, from);
    if (p == std::string::npos) return p;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return p;
    ++p;
    while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
    return p;
}

static std::string json_string_field(const std::string& json, const std::string& key, size_t from = 0)
{
    const size_t p = json_field_value(json, key, from);
    return p < json.size() && json[p] == '"' ? decode_json_string(json, p) : std::string();
}

static int json_int_field(const std::string& json, const std::string& key, size_t from = 0)
{
    const size_t p = json_field_value(json, key, from);
    if (p >= json.size() || json[p] == 'n' || json[p] == 't' || json[p] == 'f') return 0;
    return std::atoi(json.c_str() + p);
}

static std::string json_object_field(const std::string& json, const std::string& key)
{
    size_t p = json_field_value(json, key);
    if (p >= json.size() || json[p] != '{') return std::string();
    const size_t start = p++;
    int depth = 1;
    bool inString = false;
    bool escaped = false;
    for (; p < json.size(); ++p)
    {
        const char c = json[p];
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return json.substr(start, p - start + 1);
    }
    return std::string();
}

static std::string json_array_field(const std::string& json, const std::string& key)
{
    size_t p = json_field_value(json, key);
    if (p >= json.size() || json[p] != '[') return std::string();
    const size_t start = p++;
    int depth = 1;
    bool inString = false;
    bool escaped = false;
    for (; p < json.size(); ++p)
    {
        const char c = json[p];
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '[') ++depth;
        else if (c == ']' && --depth == 0) return json.substr(start, p - start + 1);
    }
    return std::string();
}

static std::vector<std::string> json_object_array(const std::string& array)
{
    std::vector<std::string> objects;
    size_t p = array.find('[');
    if (p == std::string::npos) return objects;
    for (++p; p < array.size();)
    {
        while (p < array.size() && (std::isspace(static_cast<unsigned char>(array[p])) || array[p] == ',')) ++p;
        if (p >= array.size() || array[p] == ']') break;
        if (array[p] != '{') { ++p; continue; }
        const size_t start = p++;
        int depth = 1;
        bool inString = false;
        bool escaped = false;
        for (; p < array.size() && depth > 0; ++p)
        {
            const char c = array[p];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') ++depth;
            else if (c == '}') --depth;
        }
        if (depth == 0) objects.push_back(array.substr(start, p - start));
    }
    return objects;
}

static std::string json_quote(const std::string& text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0x0F]);
                    out.push_back(hex[c & 0x0F]);
                }
                else out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
    return out;
}

static SaikouAnime parse_anime_object(const std::string& object)
{
    SaikouAnime anime;
    anime.id = json_int_field(object, "id");
    anime.score = json_int_field(object, "averageScore");
    anime.episodes = json_int_field(object, "episodes");
    anime.format = json_string_field(object, "format");
    anime.status = json_string_field(object, "status");
    anime.description = json_string_field(object, "description");
    anime.bannerUrl = json_string_field(object, "bannerImage");

    const std::string title = json_object_field(object, "title");
    anime.title = json_string_field(title, "english");
    if (anime.title.empty()) anime.title = json_string_field(title, "userPreferred");
    if (anime.title.empty()) anime.title = json_string_field(title, "romaji");
    if (anime.title.empty()) anime.title = json_string_field(title, "native");

    const std::string cover = json_object_field(object, "coverImage");
    anime.coverUrl = json_string_field(cover, "extraLarge");
    if (anime.coverUrl.empty()) anime.coverUrl = json_string_field(cover, "large");
    if (anime.title.empty()) anime.title = "Untitled anime";
    return anime;
}

static std::vector<SaikouAnime> parse_anilist_media(
    const std::string& response, size_t maxItems = 24)
{
    std::vector<SaikouAnime> media;
    size_t arrayAt = response.find("\"media\"");
    if (arrayAt == std::string::npos) return media;
    arrayAt = response.find('[', arrayAt);
    if (arrayAt == std::string::npos) return media;

    for (size_t p = arrayAt + 1; p < response.size() && media.size() < maxItems;)
    {
        while (p < response.size() && (std::isspace(static_cast<unsigned char>(response[p])) || response[p] == ',')) ++p;
        if (p >= response.size() || response[p] == ']') break;
        if (response[p] != '{') { ++p; continue; }

        const size_t start = p++;
        int depth = 1;
        bool inString = false;
        bool escaped = false;
        for (; p < response.size() && depth > 0; ++p)
        {
            const char c = response[p];
            if (inString)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') ++depth;
            else if (c == '}') --depth;
        }
        if (depth != 0) break;
        SaikouAnime anime = parse_anime_object(response.substr(start, p - start));
        if (anime.id > 0) media.push_back(anime);
    }
    return media;
}

static std::vector<SaikouAnime> fetch_anilist_media(
    const std::string& search, int pageSize, std::string& status, int page = 1)
{
    static const std::string endpoint = "https://graphql.anilist.co";
    const std::string query =
        "query ($page: Int, $perPage: Int, $search: String) { "
        "Page(page: $page, perPage: $perPage) { "
        "media(type: ANIME, sort: TRENDING_DESC, search: $search) { "
        "id title { english romaji native userPreferred } "
        "coverImage { large extraLarge } bannerImage averageScore format status episodes "
        "description(asHtml: false) "
        "} } }";

    std::string body = "{\"query\":" + json_quote(query) +
        ",\"variables\":{\"page\":" + std::to_string(page) + ",\"perPage\":" + std::to_string(pageSize) +
        ",\"search\":" + (search.empty() ? "null" : json_quote(search)) + "}}";
    std::string response;
    if (!http_request(endpoint, &body, response, 12))
    {
        status = "AniList could not be reached. Check the Switch internet connection.";
        log_stage("ANILIST MEDIA REQUEST FAILED");
        return {};
    }

    std::vector<SaikouAnime> result = parse_anilist_media(response);
    char marker[96];
    std::snprintf(marker, sizeof(marker),
        "ANILIST MEDIA PAGE %d FOUND %zu ITEMS", page, result.size());
    log_stage(marker);
    status = result.empty() ? "AniList returned no anime." : "Live AniList data";
    return result;
}

static std::vector<SaikouAnime> fetch_anilist_trending_page(
    int page, int pageSize, std::string& status)
{
    static const std::string endpoint = "https://graphql.anilist.co";
    const std::string query =
        "query ($page: Int, $perPage: Int) { "
        "Page(page: $page, perPage: $perPage) { "
        "media(type: ANIME, sort: TRENDING_DESC) { "
        "id title { english romaji native userPreferred } "
        "coverImage { large extraLarge } bannerImage averageScore format status episodes "
        "description(asHtml: false) "
        "} } }";

    const std::string body = "{\"query\":" + json_quote(query) +
        ",\"variables\":{\"page\":" + std::to_string(page) +
        ",\"perPage\":" + std::to_string(pageSize) + "}}";
    std::string response;
    if (!http_request(endpoint, &body, response, 12))
    {
        status = "AniList could not be reached. Check the Switch internet connection.";
        log_stage("ANILIST TRENDING PAGE REQUEST FAILED");
        return {};
    }

    std::vector<SaikouAnime> result =
        parse_anilist_media(response, static_cast<size_t>(pageSize));
    char marker[128];
    std::snprintf(marker, sizeof(marker),
        "ANILIST TRENDING PAGE %d FOUND %zu ITEMS", page, result.size());
    log_stage(marker);
    status = result.empty() ? "AniList returned no more trending anime." : "Live AniList trending data";
    return result;
}

static std::vector<SaikouAnime> fetch_currently_airing_page(
    int page, int pageSize, std::string& status)
{
    static const std::string endpoint = "https://graphql.anilist.co";
    const std::string query =
        "query ($page: Int, $perPage: Int) { "
        "Page(page: $page, perPage: $perPage) { "
        "media(type: ANIME, status: RELEASING, sort: POPULARITY_DESC) { "
        "id title { english romaji native userPreferred } "
        "coverImage { large extraLarge } bannerImage averageScore format status episodes "
        "description(asHtml: false) "
        "} } }";

    const std::string body = "{\"query\":" + json_quote(query) +
        ",\"variables\":{\"page\":" + std::to_string(page) +
        ",\"perPage\":" + std::to_string(pageSize) + "}}";
    std::string response;
    if (!http_request(endpoint, &body, response, 12))
    {
        status = "AniList could not be reached. Check the Switch internet connection.";
        log_stage("ANILIST AIRING PAGE REQUEST FAILED");
        return {};
    }

    std::vector<SaikouAnime> result =
        parse_anilist_media(response, static_cast<size_t>(pageSize));
    char marker[128];
    std::snprintf(marker, sizeof(marker),
        "ANILIST AIRING PAGE %d FOUND %zu ITEMS", page, result.size());
    log_stage(marker);
    status = result.empty()
        ? "AniList returned no more currently airing anime."
        : "Live AniList currently airing data";
    return result;
}

static std::vector<SaikouAnime> fetch_currently_airing_media(int pageSize, std::string& status)
{
    static const std::string endpoint = "https://graphql.anilist.co";
    const std::string query =
        "query ($page: Int, $perPage: Int) { "
        "Page(page: $page, perPage: $perPage) { "
        "media(type: ANIME, status: RELEASING, sort: POPULARITY_DESC) { "
        "id title { english romaji native userPreferred } "
        "coverImage { large extraLarge } bannerImage averageScore format status episodes "
        "description(asHtml: false) "
        "} } }";

    std::string body = "{\"query\":" + json_quote(query) +
        ",\"variables\":{\"page\":1,\"perPage\":" + std::to_string(pageSize) + "}}";
    std::string response;
    if (!http_request(endpoint, &body, response, 12))
    {
        status = "AniList could not be reached. Check the Switch internet connection.";
        log_stage("ANILIST AIRING REQUEST FAILED");
        return {};
    }

    std::vector<SaikouAnime> result = parse_anilist_media(response);
    char marker[128];
    std::snprintf(marker, sizeof(marker), "ANILIST AIRING FOUND %zu ITEMS", result.size());
    log_stage(marker);
    status = result.empty() ? "AniList returned no currently airing anime." : "Live AniList currently airing data";
    return result;
}

struct AniListEntry
{
    std::string listStatus;
    std::string listName;
    int progress = 0;
    SaikouAnime anime;
};

static std::vector<AniListEntry> parse_anilist_library(const std::string& response)
{
    std::vector<AniListEntry> entries;
    const std::string collection = json_object_field(response, "MediaListCollection");
    const std::string lists = json_array_field(collection, "lists");
    for (const std::string& list : json_object_array(lists))
    {
        const std::string listStatus = json_string_field(list, "status");
        const std::string listName = json_string_field(list, "name");
        const std::string entryArray = json_array_field(list, "entries");
        for (const std::string& entry : json_object_array(entryArray))
        {
            const std::string media = json_object_field(entry, "media");
            SaikouAnime anime = parse_anime_object(media);
            if (anime.id <= 0) continue;
            AniListEntry item;
            item.listStatus = json_string_field(entry, "status");
            if (item.listStatus.empty()) item.listStatus = listStatus;
            item.listName = listName;
            item.progress = json_int_field(entry, "progress");
            item.anime = anime;
            entries.push_back(item);
        }
    }
    return entries;
}

static std::string load_anilist_token()
{
    FILE* file = std::fopen(kAniListTokenPath, "r");
    if (!file) return std::string();
    char buffer[4096] = {};
    const size_t count = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    std::string token(buffer, count);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
        token.pop_back();
    size_t first = 0;
    while (first < token.size() && std::isspace(static_cast<unsigned char>(token[first]))) ++first;
    if (first > 0) token.erase(0, first);
    return token;
}

static bool save_anilist_token(const std::string& token)
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SaikouTV", 0777);
    FILE* file = std::fopen(kAniListTokenPath, "w");
    if (!file) return false;
    const bool ok = std::fwrite(token.data(), 1, token.size(), file) == token.size();
    std::fclose(file);
    return ok;
}

static bool validate_anilist_token(const std::string& token, std::string& username)
{
    const std::string query = "{ Viewer { id name } }";
    const std::string body = "{\"query\":" + json_quote(query) + "}";
    std::string response;
    if (!http_request("https://graphql.anilist.co", &body, response, 12, &token))
        return false;
    if (json_int_field(response, "id") <= 0)
        return false;
    username = json_string_field(response, "name");
    return !username.empty();
}

static std::vector<AniListEntry> fetch_anilist_library(
    const std::string& token, std::string& username, std::string& status)
{
    const std::string viewerQuery = "{ Viewer { id name } }";
    const std::string viewerBody = "{\"query\":" + json_quote(viewerQuery) + "}";
    std::string viewerResponse;
    if (!http_request("https://graphql.anilist.co", &viewerBody, viewerResponse, 12, &token))
    {
        status = "Could not load the AniList account.";
        return {};
    }

    const int userId = json_int_field(viewerResponse, "id");
    username = json_string_field(viewerResponse, "name");
    if (userId <= 0 || username.empty())
    {
        status = "AniList rejected the saved account token. Pair again in Settings.";
        return {};
    }

    const std::string query =
        "query ($userId: Int) { MediaListCollection(userId: $userId, type: ANIME) { "
        "lists { name status entries { status progress media { "
        "id title { english romaji native userPreferred } "
        "coverImage { large extraLarge } bannerImage averageScore format status episodes "
        "description(asHtml: false) "
        "} } } } }";
    const std::string body = "{\"query\":" + json_quote(query) +
        ",\"variables\":{\"userId\":" + std::to_string(userId) + "}}";
    std::string response;
    if (!http_request("https://graphql.anilist.co", &body, response, 12, &token))
    {
        status = "Could not load AniList lists.";
        return {};
    }

    std::vector<AniListEntry> result = parse_anilist_library(response);
    status = result.empty()
        ? (username + " is linked. No anime entries were returned.")
        : (username + " — " + std::to_string(result.size()) + " anime entries");
    return result;
}

static std::string get_switch_local_ip()
{
    if (!ensure_network_ready()) return std::string();
    Result nifmResult = nifmInitialize(NifmServiceType_User);
    const bool nifmOwned = R_SUCCEEDED(nifmResult);
    if (!nifmOwned && nifmResult != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
        return std::string();

    u32 rawAddress = 0;
    const Result ipResult = nifmGetCurrentIpAddress(&rawAddress);
    if (nifmOwned) nifmExit();
    if (R_FAILED(ipResult)) return std::string();

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&rawAddress);
    if (bytes[0] == 0 || bytes[0] == 127)
        return std::string();
    char ip[32];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
        static_cast<unsigned int>(bytes[0]), static_cast<unsigned int>(bytes[1]),
        static_cast<unsigned int>(bytes[2]), static_cast<unsigned int>(bytes[3]));
    return ip;
}

static std::string cached_cover_path(int id)
{
    return std::string(kCacheDir) + "/anilist_" + std::to_string(id) + "_cover.jpg";
}

static std::string cached_banner_path(int id)
{
    return std::string(kCacheDir) + "/anilist_" + std::to_string(id) + "_banner.jpg";
}

static bool download_image(const std::string& url, const std::string& path)
{
    if (url.empty() || !ensure_network_ready() || !ensure_curl_ready())
        return false;

    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > 256)
        return true;

    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string bytes;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouTV-NX/0.3");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_http_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);

    const CURLcode result = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || httpCode < 200 || httpCode >= 300 || bytes.size() <= 256)
        return false;

    mkdir(kCacheDir, 0777);
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

static void initialize_source_settings()
{
    for (size_t i = 0; i < kApiSourceCount; ++i)
        g_providerEnabled[i] = true;

    FILE* file = std::fopen(kSettingsPath, "r");
    if (!file) return;
    char line[128];
    while (std::fgets(line, sizeof(line), file))
    {
        std::string value(line);
        for (size_t i = 0; i < kApiSourceCount; ++i)
        {
            const std::string key = std::string(kApiSources[i].slug) + "=";
            if (value.find(key) == 0)
                g_providerEnabled[i] = value[key.size()] == '1';
        }
        if (value.find("selected=") == 0)
            g_selectedApiSource = std::atoi(value.c_str() + 9);
    }
    std::fclose(file);
    if (!api_source_is_valid(g_selectedApiSource))
        g_selectedApiSource = 0;
}

static void save_source_settings()
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SaikouTV", 0777);
    FILE* file = std::fopen(kSettingsPath, "w");
    if (!file) return;
    for (size_t i = 0; i < kApiSourceCount; ++i)
        std::fprintf(file, "%s=%d\n", kApiSources[i].slug, g_providerEnabled[i] ? 1 : 0);
    std::fprintf(file, "selected=%d\n", g_selectedApiSource);
    std::fclose(file);
}

static void clear_box(brls::Box* box)
{
    if (!box) return;
    const auto children = box->getChildren();
    for (brls::View* child : children)
        box->removeView(child);
}

class AnimeDetailsActivity;
static AnimeDetailsActivity* g_animeDetailsActivity = nullptr;

class AnimeDetailsActivity : public brls::Activity
{
public:
    explicit AnimeDetailsActivity(SaikouAnime anime) : anime(std::move(anime))
    {
        g_animeDetailsActivity = this;
    }

    ~AnimeDetailsActivity() override
    {
        if (m_bannerWorker.joinable())
            m_bannerWorker.join();
        if (g_animeDetailsActivity == this)
            g_animeDetailsActivity = nullptr;
    }

    brls::View* createContentView() override
    {
        log_stage("ACTIVITY OPEN: anime details");
        m_content = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(m_content);
        m_content->setWidthPercentage(100.0f);
        m_content->setHeightPercentage(100.0f);
        m_content->setPadding(30.0f);
        m_content->setBackgroundColor(nvgRGB(16, 20, 29));
        log_stage("DETAIL VIEW BUILT");
        return m_content;
    }

    void onContentAvailable() override
    {
        brls::Box* root = m_content;
        if (!root) return;
        log_stage("DETAIL CONTENT BUILT");

        brls::Label* heading = new brls::Label();
        heading->setText(anime.title);
        heading->setFontSize(30.0f);
        heading->setTextColor(nvgRGB(244, 246, 250));
        root->addView(heading);

        std::string meta;
        if (anime.score > 0)
            meta = "AniList score " + std::to_string(anime.score) + "/100";
        if (!anime.format.empty())
            meta += (meta.empty() ? "" : "    ") + anime.format;
        if (anime.episodes > 0)
            meta += (meta.empty() ? "" : "    ") + std::to_string(anime.episodes) + " episodes";

        brls::Label* metaLabel = new brls::Label();
        metaLabel->setText(meta);
        metaLabel->setFontSize(15.0f);
        metaLabel->setTextColor(nvgRGB(174, 184, 200));
        metaLabel->setMargins(0, 6, 0, 0);
        root->addView(metaLabel);

        if (!anime.bannerUrl.empty())
        {
            m_bannerSlot = new brls::Box();
            m_bannerSlot->setDimensions(1160.0f, 150.0f);
            m_bannerSlot->setMargins(0, 12, 0, 0);
            m_bannerSlot->setBackgroundColor(nvgRGB(27, 34, 48));
            m_bannerSlot->setFocusable(false);
            root->addView(m_bannerSlot);
            m_bannerPath = cached_banner_path(anime.id);
            m_bannerWorker = std::thread([this] {
                m_bannerDownloaded = download_image(anime.bannerUrl, m_bannerPath);
                m_bannerReady.store(true, std::memory_order_release);
            });
        }

        brls::Box* summary = new brls::Box(brls::Axis::ROW);
        summary->setHeight(190.0f);
        summary->setMargins(0, 12, 0, 0);
        if (anime.posterPath.empty())
            anime.posterPath = cached_cover_path(anime.id);
        struct stat posterStat;
        if (stat(anime.posterPath.c_str(), &posterStat) == 0 && posterStat.st_size > 256)
        {
            brls::Image* poster = new brls::Image();
            poster->setDimensions(126.0f, 184.0f);
            poster->setScalingType(brls::ImageScalingType::FIT);
            poster->setImageFromFile(anime.posterPath);
            poster->setFocusable(false);
            summary->addView(poster);
        }

        brls::Box* text = new brls::Box(brls::Axis::COLUMN);
        text->setWidth(1010.0f);
        text->setMargins(18, 0, 0, 0);
        summary->addView(text);

        brls::Label* details = new brls::Label();
        std::string description = anime.description;
        if (description.size() > 840)
        {
            size_t cut = 840;
            while (cut > 0 && (static_cast<unsigned char>(description[cut]) & 0xC0) == 0x80) --cut;
            description.resize(cut);
            description += "...";
        }
        details->setText(description.empty() ? "No description is available from AniList." : description);
        details->setFontSize(16.0f);
        details->setLineHeight(21.0f);
        details->setTextColor(nvgRGB(220, 228, 240));
        details->setFocusable(false);
        text->addView(details);
        root->addView(summary);

        brls::Label* sourcesHeading = new brls::Label();
        sourcesHeading->setText("WATCH SOURCES");
        sourcesHeading->setFontSize(20.0f);
        sourcesHeading->setTextColor(nvgRGB(220, 228, 240));
        sourcesHeading->setMargins(0, 12, 0, 4);
        root->addView(sourcesHeading);

        brls::Box* sources = new brls::Box(brls::Axis::ROW);
        sources->setHeight(46.0f);
        for (size_t i = 0; i < kApiSourceCount; ++i)
        {
            if (!g_providerEnabled[i]) continue;
            const ApiSourceInfo& source = kApiSources[i];
            brls::Box* choice = make_source_choice(source);
            sources->addView(choice);
        }
        if (sources->getChildren().empty())
        {
            brls::Label* empty = new brls::Label();
            empty->setText("Enable a source API in Settings to continue.");
            empty->setFontSize(16.0f);
            empty->setTextColor(nvgRGB(174, 184, 200));
            sources->addView(empty);
        }
        root->addView(sources);

        m_sourceStatus = new brls::Label();
        m_sourceStatus->setFontSize(14.0f);
        m_sourceStatus->setTextColor(nvgRGB(174, 184, 200));
        m_sourceStatus->setMargins(0, 7, 0, 0);
        m_sourceStatus->setText("Choose a source. Episode lookup and playback are the next integration step.");
        root->addView(m_sourceStatus);
    }

    void tick()
    {
        if (!m_bannerReady.load(std::memory_order_acquire))
            return;
        if (m_bannerWorker.joinable())
            m_bannerWorker.join();
        m_bannerReady.store(false, std::memory_order_release);
        if (m_bannerDownloaded && m_bannerSlot)
        {
            brls::Image* banner = new brls::Image();
            banner->setDimensions(1160.0f, 150.0f);
            banner->setScalingType(brls::ImageScalingType::FILL);
            banner->setImageFromFile(m_bannerPath);
            banner->setFocusable(false);
            m_bannerSlot->addView(banner);
            log_stage("DETAIL BANNER READY");
        }
        else
        {
            log_stage("DETAIL BANNER UNAVAILABLE");
        }
    }

private:
    SaikouAnime anime;
    brls::Box* m_content = nullptr;
    brls::Box* m_bannerSlot = nullptr;
    brls::Label* m_sourceStatus = nullptr;
    std::string m_bannerPath;
    std::thread m_bannerWorker;
    std::atomic<bool> m_bannerReady{ false };
    bool m_bannerDownloaded = false;

    brls::Box* make_source_choice(const ApiSourceInfo& source)
    {
        brls::Box* choice = new brls::Box(brls::Axis::COLUMN);
        choice->setWidth(175.0f);
        choice->setHeight(42.0f);
        choice->setMargins(0, 10, 0, 0);
        choice->setPadding(8.0f);
        choice->setBackgroundColor(nvgRGB(27, 34, 48));
        choice->setBorderColor(nvgRGB(48, 57, 74));
        choice->setBorderThickness(1.0f);
        choice->setCornerRadius(8.0f);
        choice->setFocusable(true);

        brls::Label* label = new brls::Label();
        label->setText(source.name);
        label->setFontSize(15.0f);
        choice->addView(label);
        choice->registerAction("Select source", brls::BUTTON_A,
            [this, id = static_cast<int>(source.id), name = std::string(source.name)](brls::View*) {
                g_selectedApiSource = id;
                save_source_settings();
                if (m_sourceStatus)
                    m_sourceStatus->setText("Selected " + name + ". Episode lookup is being connected.");
                log_stage("DETAIL SOURCE SELECTED");
                return true;
            });
        return choice;
    }
};

static void open_anime_details(const SaikouAnime& anime)
{
    char marker[128];
    const auto stack = brls::Application::getActivitiesStack();
    std::snprintf(marker, sizeof(marker),
        "ANIME CARD OPENED: id=%d stack_before=%zu", anime.id, stack.size());
    log_stage(marker);
    brls::Application::pushActivity(
        new AnimeDetailsActivity(anime),
        brls::TransitionAnimation::NONE);
    log_stage("DETAIL ACTIVITY PUSH RETURNED");
}

static std::string compact_card_title(const std::string& title)
{
    if (title.size() <= 34) return title;
    size_t cut = 34;
    while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) --cut;
    return title.substr(0, cut) + "...";
}

static brls::Box* make_anime_card(
    const SaikouAnime& anime,
    const std::string& subtitle = std::string(),
    brls::Image** imageOut = nullptr)
{
    brls::Box* card = new brls::Box(brls::Axis::COLUMN);
    card->setWidth(184.0f);
    card->setHeight(subtitle.empty() ? 244.0f : 260.0f);
    card->setPadding(6.0f);
    card->setMargins(3, 7, 3, 0);
    card->setBackgroundColor(nvgRGB(27, 34, 48));
    card->setBorderColor(nvgRGB(48, 57, 74));
    card->setBorderThickness(1.0f);
    card->setCornerRadius(9.0f);
    card->setFocusable(true);
    card->setHighlightPadding(4.0f);

    const std::string imagePath = anime.posterPath.empty()
        ? cached_cover_path(anime.id)
        : anime.posterPath;
    struct stat st;
    const float posterHeight = subtitle.empty() ? 168.0f : 148.0f;

    brls::Image* poster = new brls::Image();
    poster->setDimensions(168.0f, posterHeight);
    poster->setScalingType(brls::ImageScalingType::FIT);
    poster->setBackgroundColor(nvgRGB(35, 45, 62));
    poster->setFocusable(false);

    if (stat(imagePath.c_str(), &st) == 0 && st.st_size > 256)
        poster->setImageFromFile(imagePath);

    if (imageOut)
        *imageOut = poster;

    card->addView(poster);

    brls::Label* score = new brls::Label();
    score->setText(anime.score > 0 ? "AniList  " + std::to_string(anime.score) + "/100" : "AniList score unavailable");
    score->setFontSize(12.0f);
    score->setTextColor(nvgRGB(97, 207, 226));
    score->setMargins(0, 3, 0, 0);
    score->setFocusable(false);
    card->addView(score);

    brls::Label* title = new brls::Label();
    title->setText(compact_card_title(anime.title));
    title->setFontSize(14.0f);
    title->setSingleLine(false);
    title->setTextColor(nvgRGB(244, 246, 250));
    title->setMargins(0, 2, 0, 0);
    title->setFocusable(false);
    card->addView(title);

    if (!subtitle.empty())
    {
        brls::Label* progress = new brls::Label();
        progress->setText(subtitle);
        progress->setFontSize(11.0f);
        progress->setTextColor(nvgRGB(174, 184, 200));
        progress->setMargins(0, 2, 0, 0);
        progress->setFocusable(false);
        card->addView(progress);
    }

    card->registerAction("Open anime details", brls::BUTTON_A, [anime](brls::View*) {
        open_anime_details(anime);
        return true;
    });
    return card;
}

static void render_horizontal_anime_cards(brls::Box* container, const std::vector<SaikouAnime>& items)
{
    if (!container) return;
    clear_box(container);

    // Keep the HScrollingFrame content view wide enough to contain the complete
    // card footprint, including its horizontal margins, so the final card remains
    // reachable by focus/navigation.
    const float contentWidth = std::max(1160.0f, static_cast<float>(items.size()) * 192.0f);
    container->setWidth(contentWidth);

    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setWidth(contentWidth);
    row->setHeight(252.0f);
    row->setAlignItems(brls::AlignItems::FLEX_START);

    for (const SaikouAnime& anime : items)
        row->addView(make_anime_card(anime));

    container->addView(row);

    if (items.empty())
    {
        brls::Label* empty = new brls::Label();
        empty->setText("No titles to show.");
        empty->setFontSize(16.0f);
        empty->setTextColor(nvgRGB(174, 184, 200));
        row->addView(empty);
    }
}


static brls::Box* make_library_anime_card(
    const SaikouAnime& anime,
    const std::string& subtitle,
    brls::Image** outPoster)
{
    if (outPoster)
        *outPoster = nullptr;

    brls::Box* card = new brls::Box(brls::Axis::COLUMN);
    card->setWidth(184.0f);
    card->setHeight(subtitle.empty() ? 244.0f : 260.0f);
    card->setPadding(6.0f);
    card->setMargins(3, 7, 3, 0);
    card->setBackgroundColor(nvgRGB(27, 34, 48));
    card->setBorderColor(nvgRGB(48, 57, 74));
    card->setBorderThickness(1.0f);
    card->setCornerRadius(9.0f);
    card->setFocusable(true);
    card->setHighlightPadding(4.0f);

    const std::string imagePath =
        anime.posterPath.empty()
            ? cached_cover_path(anime.id)
            : anime.posterPath;

    brls::Image* poster = new brls::Image();
    poster->setDimensions(
        168.0f,
        subtitle.empty() ? 168.0f : 148.0f);
    poster->setScalingType(brls::ImageScalingType::FIT);
    poster->setBackgroundColor(nvgRGB(35, 45, 62));
    poster->setFocusable(false);

    struct stat st;
    if (stat(imagePath.c_str(), &st) == 0 && st.st_size > 256)
        poster->setImageFromFile(imagePath);

    card->addView(poster);

    if (outPoster)
        *outPoster = poster;

    brls::Label* score = new brls::Label();
    score->setText(
        anime.score > 0
            ? "AniList  " + std::to_string(anime.score) + "/100"
            : "AniList score unavailable");
    score->setFontSize(12.0f);
    score->setTextColor(nvgRGB(97, 207, 226));
    score->setMargins(0, 3, 0, 0);
    score->setFocusable(false);
    card->addView(score);

    brls::Label* title = new brls::Label();
    title->setText(compact_card_title(anime.title));
    title->setFontSize(14.0f);
    title->setSingleLine(false);
    title->setTextColor(nvgRGB(244, 246, 250));
    title->setMargins(0, 2, 0, 0);
    title->setFocusable(false);
    card->addView(title);

    if (!subtitle.empty())
    {
        brls::Label* progress = new brls::Label();
        progress->setText(subtitle);
        progress->setFontSize(11.0f);
        progress->setTextColor(nvgRGB(174, 184, 200));
        progress->setMargins(0, 2, 0, 0);
        progress->setFocusable(false);
        card->addView(progress);
    }

    card->registerAction(
        "Open anime details",
        brls::BUTTON_A,
        [anime](brls::View*) {
            open_anime_details(anime);
            return true;
        });

    return card;
}

static void render_anime_cards(brls::Box* container, const std::vector<SaikouAnime>& items)
{
    if (!container) return;
    clear_box(container);

    constexpr size_t perRow = 6;
    for (size_t start = 0; start < items.size(); start += perRow)
    {
        brls::Box* row = new brls::Box(brls::Axis::ROW);
        row->setWidth(1160.0f);
        row->setHeight(252.0f);
        row->setAlignItems(brls::AlignItems::FLEX_START);
        const size_t stop = std::min(items.size(), start + perRow);
        for (size_t i = start; i < stop; ++i)
            row->addView(make_anime_card(items[i]));
        container->addView(row);
    }

    if (items.empty())
    {
        brls::Label* empty = new brls::Label();
        empty->setText("No titles to show.");
        empty->setFontSize(16.0f);
        empty->setTextColor(nvgRGB(174, 184, 200));
        container->addView(empty);
    }
}

// Shared by Search, Trending, Currently Airing, and Continue Watching.
// Keep the implementation below the activity definitions, but declare it here
// so earlier activity classes can safely reference the shared Load More UI.
static brls::Box* make_home_load_more_card(std::function<void()> callback);

class SearchActivity;
static SearchActivity* g_searchActivity = nullptr;

class SearchActivity : public brls::Activity
{
public:
    SearchActivity() { g_searchActivity = this; }

    ~SearchActivity() override
    {
        m_lifetime->store(false, std::memory_order_release);
        if (m_worker.joinable())
            m_worker.join();
        if (m_imageWorker.joinable())
            m_imageWorker.join();
        if (g_searchActivity == this)
            g_searchActivity = nullptr;
    }

    brls::View* createContentView() override
    {
        log_stage("ACTIVITY OPEN: Search");
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(root);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("SEARCH ANIME");
        heading->setFontSize(28.0f);
        root->addView(heading);

        m_searchButton = new brls::Box(brls::Axis::ROW);
        m_searchButton->setWidth(500.0f);
        m_searchButton->setHeight(52.0f);
        m_searchButton->setMargins(0, 15, 0, 0);
        m_searchButton->setPadding(12.0f);
        m_searchButton->setBackgroundColor(nvgRGB(27, 34, 48));
        m_searchButton->setBorderColor(nvgRGB(48, 57, 74));
        m_searchButton->setBorderThickness(1.0f);
        m_searchButton->setCornerRadius(8.0f);
        m_searchButton->setFocusable(true);

        m_queryLabel = new brls::Label();
        m_queryLabel->setText("Press A to enter a title with the Switch keyboard");
        m_queryLabel->setFontSize(16.0f);
        m_searchButton->addView(m_queryLabel);
        m_searchButton->registerAction("Enter search query", brls::BUTTON_A, [this](brls::View*) {
            run_search();
            return true;
        });
        root->addView(m_searchButton);

        m_status = new brls::Label();
        m_status->setText("Search uses live AniList anime data.");
        m_status->setFontSize(15.0f);
        m_status->setTextColor(nvgRGB(174, 184, 200));
        m_status->setMargins(0, 12, 0, 0);
        root->addView(m_status);

        m_scroll = new brls::ScrollingFrame();
        m_scroll->setWidthPercentage(100.0f);
        m_scroll->setHeight(575.0f);
        m_scroll->setMargins(0, 12, 0, 0);
        m_scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);

        m_results = new brls::Box(brls::Axis::COLUMN);
        m_results->setWidth(1160.0f);
        m_results->setHeight(600.0f);
        m_scroll->setContentView(m_results);
        root->addView(m_scroll);

        log_stage("SEARCH VIEW BUILT");
        return root;
    }

    void tick()
    {
        if (!m_resultReady.load(std::memory_order_acquire))
            return;

        if (m_worker.joinable())
            m_worker.join();

        m_loading = false;
        m_resultReady.store(false, std::memory_order_release);

        ++m_renderGeneration;
        render_results();
        perf_log_count("SEARCH FIRST VISIBLE CARDS RENDERED", m_resultItems.size());
        start_progressive_image_load();

        if (m_status)
        {
            std::string text = m_resultStatus;
            text += " — " + std::to_string(m_resultItems.size()) +
                " results";
            if (m_hasMore)
                text += " — select LOAD MORE for another 24.";
            m_status->setText(text);
        }

        log_stage("SEARCH RESULTS ATTACHED");
    }

private:
    brls::Label* m_queryLabel = nullptr;
    brls::Box* m_searchButton = nullptr;
    brls::Label* m_status = nullptr;
    brls::ScrollingFrame* m_scroll = nullptr;
    brls::Box* m_results = nullptr;

    std::string m_pendingQuery;
    std::string m_resultStatus;
    std::vector<SaikouAnime> m_resultItems;
    std::thread m_worker;
    std::thread m_imageWorker;
    std::atomic<bool> m_resultReady{ false };
    std::atomic<uint64_t> m_renderGeneration{ 0 };
    std::shared_ptr<std::atomic<bool>> m_lifetime =
        std::make_shared<std::atomic<bool>>(true);

    bool m_loading = false;
    bool m_hasMore = true;
    int m_page = 0;

    void run_search()
    {
        if (m_loading)
        {
            if (m_status)
                m_status->setText("Search is still loading. Please wait.");
            return;
        }

        log_stage("SEARCH SWITCH KEYBOARD OPEN");

        SwkbdConfig keyboard{};
        Result rc = swkbdCreate(&keyboard, 0);
        if (R_FAILED(rc))
        {
            if (m_status)
                m_status->setText("Could not open the Switch keyboard.");
            log_stage("SEARCH SWITCH KEYBOARD CREATE FAILED");
            return;
        }

        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, "Search anime on AniList");
        swkbdConfigSetGuideText(&keyboard, "Type an anime title");
        swkbdConfigSetSubText(&keyboard, "Press Search to run the query; Cancel closes the keyboard");
        swkbdConfigSetOkButtonText(&keyboard, "Search");

        if (!m_pendingQuery.empty())
            swkbdConfigSetInitialText(&keyboard, m_pendingQuery.c_str());

        brls::Application::setGlobalQuit(false);
        g_restoreGlobalQuitAfterKeyboard = true;
        log_stage("SEARCH KEYBOARD GLOBAL QUIT DISABLED");

        char query[256] = {};
        rc = swkbdShow(&keyboard, query, sizeof(query));
        swkbdClose(&keyboard);

        if (R_FAILED(rc) || query[0] == '\0')
        {
            if (m_status)
                m_status->setText("No title entered. Press A to search, or B to leave Search.");
            log_stage("SEARCH KEYBOARD CANCELED OR EMPTY");
            return;
        }

        if (m_worker.joinable())
            m_worker.join();

        m_pendingQuery = query;
        m_page = 1;
        m_hasMore = true;
        ++m_renderGeneration;
        if (m_imageWorker.joinable())
            m_imageWorker.join();
        m_resultItems.clear();

        if (m_queryLabel)
            m_queryLabel->setText(m_pendingQuery);
        if (m_status)
            m_status->setText("Searching AniList...");
        clear_box(m_results);

        m_loading = true;
        m_resultReady.store(false, std::memory_order_release);
        log_stage("SEARCH REQUEST STARTED");

        start_load_page(1);
    }

    void start_load_page(int page)
    {
        if (m_loading && page != 1)
            return;
        if (page != 1 && !m_hasMore)
            return;

        if (m_worker.joinable())
            m_worker.join();

        m_loading = true;
        m_resultReady.store(false, std::memory_order_release);
        m_page = page;

        if (m_status)
            m_status->setText(page == 1
                ? "Searching AniList..."
                : "Loading more search results...");

        m_worker = std::thread([this, page] {
            char stage[96];
            std::snprintf(stage, sizeof(stage), "SEARCH PAGE %d API START", page);
            perf_log(stage);

            std::string status;
            std::vector<SaikouAnime> newItems =
                fetch_anilist_media(m_pendingQuery, 24, status, page);
            std::snprintf(stage, sizeof(stage), "SEARCH PAGE %d METADATA RECEIVED", page);
            perf_log_count(stage, newItems.size());

            for (SaikouAnime& anime : newItems)
                anime.posterPath = cached_cover_path(anime.id);

            if (newItems.size() < 24)
                m_hasMore = false;

            m_resultItems.insert(m_resultItems.end(), newItems.begin(), newItems.end());
            m_resultReady.store(true, std::memory_order_release);
        });
    }

    void start_progressive_image_load()
    {
        if (m_imageWorker.joinable())
            m_imageWorker.join();

        if (m_resultItems.empty() || !m_results)
            return;

        std::vector<brls::Image*> posters;
        posters.reserve(m_resultItems.size());

        for (brls::View* rowView : m_results->getChildren())
        {
            brls::Box* row = dynamic_cast<brls::Box*>(rowView);
            if (!row) continue;

            for (brls::View* cardView : row->getChildren())
            {
                brls::Box* card = dynamic_cast<brls::Box*>(cardView);
                if (!card) continue;

                const auto children = card->getChildren();
                if (children.empty()) continue;

                brls::Image* poster =
                    dynamic_cast<brls::Image*>(children.front());
                if (poster && posters.size() < m_resultItems.size())
                    posters.push_back(poster);
            }
        }

        // The final row contains the Load More card, which has no poster Image,
        // so only the anime-card image slots should have been collected.
        if (posters.size() != m_resultItems.size())
        {
            perf_log_count("SEARCH PROGRESSIVE IMAGE SLOT MISMATCH", posters.size());
            return;
        }

        const uint64_t generation =
            m_renderGeneration.load(std::memory_order_acquire);
        const auto lifetime = m_lifetime;
        const std::vector<SaikouAnime> items = m_resultItems;

        perf_log_count("SEARCH PROGRESSIVE IMAGE LOAD START", items.size());

        m_imageWorker = std::thread(
            [this, lifetime, generation, items, posters] {
                size_t completed = 0;

                for (size_t index = 0; index < items.size(); ++index)
                {
                    if (!lifetime->load(std::memory_order_acquire))
                        return;
                    if (m_renderGeneration.load(std::memory_order_acquire) != generation)
                        return;

                    const SaikouAnime& anime = items[index];
                    const std::string path = cached_cover_path(anime.id);

                    if (!download_image(anime.coverUrl, path))
                        continue;

                    ++completed;
                    brls::sync([this, lifetime, generation,
                        poster = posters[index], path, completed] {
                        if (!lifetime->load(std::memory_order_acquire))
                            return;
                        if (m_renderGeneration.load(std::memory_order_acquire) != generation)
                            return;

                        poster->setImageFromFile(path);

                        char stage[128];
                        std::snprintf(stage, sizeof(stage),
                            "SEARCH PROGRESSIVE IMAGE READY %zu", completed);
                        perf_log(stage);
                    });
                }

                brls::sync([this, lifetime, generation, completed] {
                    if (!lifetime->load(std::memory_order_acquire))
                        return;
                    if (m_renderGeneration.load(std::memory_order_acquire) != generation)
                        return;

                    perf_log_count(
                        "SEARCH PROGRESSIVE IMAGE LOAD DONE", completed);
                });
            });
    }

    brls::Box* make_search_load_more_card()
    {
        return make_home_load_more_card([this] {
            remove_load_more_row();
            start_load_page(m_page + 1);
        });
    }

    void remove_load_more_row()
    {
        if (!m_results || m_results->getChildren().empty())
            return;

        brls::View* last = m_results->getChildren().back();
        if (dynamic_cast<brls::Box*>(last))
            m_results->removeView(last);
    }

    void render_results()
    {
        clear_box(m_results);

        constexpr size_t perRow = 6;
        constexpr float rowWidth = 1160.0f;
        constexpr float rowHeight = 252.0f;

        size_t index = 0;
        while (index < m_resultItems.size())
        {
            brls::Box* row = new brls::Box(brls::Axis::ROW);
            row->setWidth(rowWidth);
            row->setHeight(rowHeight);
            row->setAlignItems(brls::AlignItems::FLEX_START);

            const size_t stop = std::min(m_resultItems.size(), index + perRow);
            for (; index < stop; ++index)
                row->addView(make_anime_card(m_resultItems[index], std::string(), nullptr));

            m_results->addView(row);
        }

        if (m_hasMore)
        {
            brls::Box* moreRow = new brls::Box(brls::Axis::ROW);
            moreRow->setWidth(rowWidth);
            moreRow->setHeight(rowHeight);
            moreRow->setAlignItems(brls::AlignItems::FLEX_START);
            moreRow->addView(make_search_load_more_card());
            m_results->addView(moreRow);
        }

        const size_t rows = (m_resultItems.size() + perRow - 1) / perRow +
            (m_hasMore ? 1 : 0);
        m_results->setHeight(
            std::max(600.0f, static_cast<float>(rows) * rowHeight + 20.0f));
    }
};


class PairingActivity;
static PairingActivity* g_pairingActivity = nullptr;

class PairingActivity : public brls::Activity
{
public:
    PairingActivity() { g_pairingActivity = this; }

    ~PairingActivity() override
    {
        m_stopping.store(true, std::memory_order_release);
        if (m_listener.joinable())
            m_listener.join();
        if (g_pairingActivity == this)
            g_pairingActivity = nullptr;
    }

    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(root);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(34.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("LINK YOUR ANILIST ACCOUNT");
        heading->setFontSize(28.0f);
        heading->setFocusable(false);
        heading->setTextColor(nvgRGB(244, 246, 250));
        root->addView(heading);

        brls::Label* instructions = new brls::Label();
        instructions->setText(
            "Your requested flow is a 3-digit code from Saikou on your phone, entered on the Switch. "
            "That code exchange is not implemented in this build yet. Keep both devices on the same Wi-Fi.");
        instructions->setFontSize(17.0f);
        instructions->setLineHeight(24.0f);
        instructions->setFocusable(false);
        instructions->setTextColor(nvgRGB(174, 184, 200));
        instructions->setMargins(0, 14, 0, 0);
        root->addView(instructions);

        m_address = get_switch_local_ip();
        brls::Label* address = new brls::Label();
        if (m_address.empty())
            address->setText("Switch IP unavailable. Connect it to Wi-Fi.\nPhone-code pairing is not available yet.");
        else
            address->setText("Switch IP: " + m_address +
                "\nThe 3-digit phone-code exchange is not available in this build.");
        address->setFontSize(25.0f);
        address->setFocusable(false);
        address->setTextColor(nvgRGB(97, 207, 226));
        address->setMargins(0, 24, 0, 0);
        root->addView(address);

        m_statusLabel = new brls::Label();
        m_statusLabel->setText("Starting the phone link listener...");
        m_statusLabel->setFontSize(17.0f);
        m_statusLabel->setFocusable(false);
        m_statusLabel->setTextColor(nvgRGB(220, 228, 240));
        m_statusLabel->setMargins(0, 18, 0, 0);
        root->addView(m_statusLabel);

        brls::Label* back = new brls::Label();
        back->setText("Press B to return to Settings.");
        back->setFontSize(14.0f);
        back->setFocusable(true);
        back->setTextColor(nvgRGB(135, 147, 166));
        back->setMargins(0, 24, 0, 0);
        root->addView(back);
        return root;
    }

    void onContentAvailable() override
    {
        if (!m_listener.joinable())
            m_listener = std::thread([this] { listen_for_phone(); });
    }

    void tick()
    {
        if (!m_statusLabel) return;
        const std::string message = status_snapshot();
        if (message != m_lastDisplayed)
        {
            m_statusLabel->setText(message);
            m_lastDisplayed = message;
        }
    }

private:
    static constexpr int kPairingPort = 2413;
    std::atomic<bool> m_stopping{ false };
    std::thread m_listener;
    std::mutex m_statusMutex;
    std::string m_status = "Starting the phone link listener...";
    std::string m_lastDisplayed;
    std::string m_address;
    brls::Label* m_statusLabel = nullptr;

    void set_status(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status = message;
    }

    std::string status_snapshot()
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_status;
    }

    bool same_local_subnet(const sockaddr_in& peer) const
    {
        if (m_address.empty())
            return true;

        in_addr local{};
        if (inet_pton(AF_INET, m_address.c_str(), &local) != 1)
            return false;
        const uint32_t peerHost = ntohl(peer.sin_addr.s_addr);
        const uint32_t localHost = ntohl(local.s_addr);
        return (peerHost & 0xFFFFFF00u) == (localHost & 0xFFFFFF00u);
    }

    bool receive_token(int client, std::string& token)
    {
        char buffer[256];
        while (!m_stopping.load(std::memory_order_acquire) && token.size() < 4096)
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(client, &readSet);
            timeval timeout{ 1, 0 };
            const int ready = select(client + 1, &readSet, nullptr, nullptr, &timeout);
            if (ready == 0)
                continue;
            if (ready < 0)
                return false;

            const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0)
                return !token.empty();

            for (ssize_t i = 0; i < received; ++i)
            {
                if (buffer[i] == '\n')
                    return !token.empty();
                if (buffer[i] != '\r')
                    token.push_back(buffer[i]);
            }
        }
        return !token.empty();
    }

    void listen_for_phone()
    {
        const int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0)
        {
            set_status("Could not create the local pairing socket.");
            return;
        }

        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kPairingPort);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            listen(server, 1) < 0)
        {
            close(server);
            set_status("Could not listen on port 2413. Close another Saikou TV session and retry.");
            return;
        }

        set_status(m_address.empty()
            ? "Listener on port 2413; no Switch IP is available. Phone-code pairing is not ready."
            : "Local token listener on port 2413; phone-code validation is not connected.");
        while (!m_stopping.load(std::memory_order_acquire))
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(server, &readSet);
            timeval timeout{ 0, 500000 };
            const int ready = select(server + 1, &readSet, nullptr, nullptr, &timeout);
            if (ready == 0)
                continue;
            if (ready < 0)
                break;

            sockaddr_in peer{};
            socklen_t peerLength = sizeof(peer);
            const int client = accept(server, reinterpret_cast<sockaddr*>(&peer), &peerLength);
            if (client < 0)
                continue;
            if (!same_local_subnet(peer))
            {
                close(client);
                set_status("Pairing request was outside the Switch's local /24 network. Still waiting...");
                continue;
            }

            std::string token;
            if (!receive_token(client, token))
            {
                close(client);
                if (!m_stopping.load(std::memory_order_acquire))
                    set_status("No token was received. Keep this screen open and try again from the phone.");
                continue;
            }
            close(client);

            set_status("Phone connected. Verifying the AniList account...");
            std::string username;
            if (!validate_anilist_token(token, username))
            {
                if (!m_stopping.load(std::memory_order_acquire))
                    set_status("AniList rejected that token. Check the phone login and try again.");
                continue;
            }
            if (!save_anilist_token(token))
            {
                set_status("Account verified, but the token could not be saved to the SD card.");
                continue;
            }

            g_anilistAccountRevision.fetch_add(1, std::memory_order_acq_rel);
            log_stage("ANILIST PHONE PAIRING SUCCEEDED");
            set_status("Linked to @" + username + ". Return to Settings or open Library.");
            break;
        }
        close(server);
    }
};

class LibraryActivity;
static LibraryActivity* g_libraryActivity = nullptr;


static constexpr const char* kLibraryStatusNames[6] = {
    "WATCHING", "PLANNING", "COMPLETED", "PAUSED", "DROPPED", "REWATCHING"
};

static std::vector<AniListEntry> filter_library_entries(
    const std::vector<AniListEntry>& entries, size_t category)
{
    std::vector<AniListEntry> result;
    if (category >= 6)
        return result;

    for (const AniListEntry& entry : entries)
    {
        if (entry.listStatus == kLibraryStatusNames[category])
        {
            AniListEntry item = entry;
            item.anime.posterPath = cached_cover_path(item.anime.id);
            result.push_back(std::move(item));
        }
    }

    return result;
}

class LibraryCategoryActivity : public brls::Activity
{
public:
    LibraryCategoryActivity(
        size_t category,
        const std::vector<AniListEntry>& entries)
        : m_category(category),
          m_items(filter_library_entries(entries, category))
    {
    }

    ~LibraryCategoryActivity() override
    {
        m_coverLifetime->store(false, std::memory_order_release);

        if (m_coverWorker.joinable())
            m_coverWorker.join();
    }

    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(root);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText(
            m_category < 6 ? kLibraryStatusNames[m_category] : "LIBRARY");
        heading->setFontSize(30.0f);
        heading->setTextColor(nvgRGB(244, 246, 250));
        root->addView(heading);

        m_status = new brls::Label();
        m_status->setText(
            "Loaded " + std::to_string(m_items.size()) + " entries");
        m_status->setFontSize(14.0f);
        m_status->setTextColor(nvgRGB(174, 184, 200));
        m_status->setMargins(0, 7, 0, 0);
        root->addView(m_status);

        m_scroll = new brls::ScrollingFrame();
        m_scroll->setWidthPercentage(100.0f);
        m_scroll->setHeight(600.0f);
        m_scroll->setMargins(0, 12, 0, 0);
        m_scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);

        m_grid = new brls::Box(brls::Axis::COLUMN);
        m_grid->setWidth(1160.0f);
        m_grid->setHeight(900.0f);
        m_scroll->setContentView(m_grid);
        root->addView(m_scroll);

        append_batch();
        return root;
    }

private:
    struct CoverJob
    {
        brls::Image* image = nullptr;
        std::string url;
        std::string path;
    };

    size_t m_category = 0;
    std::vector<AniListEntry> m_items;
    size_t m_renderedCount = 0;

    brls::Label* m_status = nullptr;
    brls::ScrollingFrame* m_scroll = nullptr;
    brls::Box* m_grid = nullptr;

    std::vector<CoverJob> m_pendingCoverJobs;
    std::thread m_coverWorker;
    bool m_coverWorkerDone = true;
    std::shared_ptr<std::atomic<bool>> m_coverLifetime =
        std::make_shared<std::atomic<bool>>(true);

    static constexpr size_t kBatchSize = 24;

    void append_batch()
    {
        if (!m_grid)
            return;

        constexpr size_t perRow = 6;
        constexpr float rowWidth = 1160.0f;
        constexpr float rowHeight = 252.0f;

        // Remove only the old Load More row. Existing anime rows/cards remain
        // intact, so focus/navigation stays stable.
        if (!m_grid->getChildren().empty() && m_renderedCount > 0)
        {
            brls::View* last = m_grid->getChildren().back();
            brls::Box* moreRow = dynamic_cast<brls::Box*>(last);

            if (moreRow && moreRow->getChildren().size() == 1)
            {
                brls::View* moreCard = moreRow->getChildren().front();

                if (brls::Application::getCurrentFocus() == moreCard)
                {
                    const auto& rows = m_grid->getChildren();
                    if (rows.size() >= 2)
                    {
                        brls::Box* previousRow =
                            dynamic_cast<brls::Box*>(rows[rows.size() - 2]);

                        if (previousRow && !previousRow->getChildren().empty())
                        {
                            brls::Application::giveFocus(
                                previousRow->getChildren().back());
                        }
                    }
                }

                m_grid->removeView(moreRow);
            }
        }

        const size_t start = m_renderedCount;
        const size_t end =
            std::min(m_items.size(), start + kBatchSize);

        size_t index = start;

        while (index < end)
        {
            brls::Box* row = nullptr;

            if (!m_grid->getChildren().empty())
            {
                brls::View* last = m_grid->getChildren().back();
                row = dynamic_cast<brls::Box*>(last);

                if (row && row->getChildren().size() >= perRow)
                    row = nullptr;
            }

            if (!row)
            {
                row = new brls::Box(brls::Axis::ROW);
                row->setWidth(rowWidth);
                row->setHeight(rowHeight);
                row->setAlignItems(brls::AlignItems::FLEX_START);
                m_grid->addView(row);
            }

            while (index < end && row->getChildren().size() < perRow)
            {
                const AniListEntry& entry = m_items[index];

                std::string subtitle;
                if (entry.listStatus == "CURRENT" ||
                    entry.listStatus == "REPEATING")
                {
                    subtitle = "Episode " +
                        std::to_string(entry.progress);

                    if (entry.anime.episodes > 0)
                        subtitle += " / " +
                            std::to_string(entry.anime.episodes);
                }
                else
                {
                    subtitle = entry.listName.empty()
                        ? entry.listStatus
                        : entry.listName;
                }

                brls::Image* image = nullptr;
                row->addView(
                    make_anime_card(entry.anime, subtitle, &image));

                struct stat st;
                const std::string path =
                    cached_cover_path(entry.anime.id);

                if (image && !entry.anime.coverUrl.empty() &&
                    (stat(path.c_str(), &st) != 0 ||
                     st.st_size <= 256))
                {
                    m_pendingCoverJobs.push_back({
                        image,
                        entry.anime.coverUrl,
                        path
                    });
                }

                ++index;
            }
        }

        m_renderedCount = end;

        const bool hasMore = m_renderedCount < m_items.size();

        if (hasMore)
        {
            brls::Box* moreRow = new brls::Box(brls::Axis::ROW);
            moreRow->setWidth(rowWidth);
            moreRow->setHeight(rowHeight);
            moreRow->setAlignItems(brls::AlignItems::FLEX_START);

            moreRow->addView(make_home_load_more_card([this] {
                append_batch();
            }));
            m_grid->addView(moreRow);
        }

        const size_t dataRows =
            (m_renderedCount + perRow - 1) / perRow;
        const size_t totalRows = dataRows + (hasMore ? 1 : 0);

        m_grid->setHeight(
            std::max(600.0f,
                static_cast<float>(totalRows) * rowHeight + 20.0f));

        update_status();
        start_cover_worker();
    }

    void update_status()
    {
        if (!m_status)
            return;

        std::string text =
            "Loaded " + std::to_string(m_renderedCount) +
            " / " + std::to_string(m_items.size());

        if (m_renderedCount < m_items.size())
            text += "  |  Select LOAD MORE for another 24.";

        m_status->setText(text);
    }

    void start_cover_worker()
    {
        if (m_pendingCoverJobs.empty())
            return;

        if (m_coverWorker.joinable())
        {
            if (!m_coverWorkerDone)
                return;

            m_coverWorker.join();
        }

        std::vector<CoverJob> jobs;
        jobs.swap(m_pendingCoverJobs);

        m_coverWorkerDone = false;
        const auto lifetime = m_coverLifetime;
        const size_t total = jobs.size();

        m_coverWorker = std::thread(
            [this, lifetime, jobs = std::move(jobs), total] {
                size_t completed = 0;

                for (const CoverJob& job : jobs)
                {
                    if (!lifetime->load(std::memory_order_acquire))
                        return;

                    if (!download_image(job.url, job.path))
                        continue;

                    ++completed;

                    brls::sync([this, lifetime,
                        image = job.image, path = job.path,
                        completed, total] {
                        if (!lifetime->load(std::memory_order_acquire))
                            return;

                        image->setImageFromFile(path);

                        if (m_status)
                        {
                            m_status->setText(
                                "Loading posters: " +
                                std::to_string(completed) +
                                " / " +
                                std::to_string(total));
                        }
                    });
                }

                brls::sync([this, lifetime, completed, total] {
                    if (!lifetime->load(std::memory_order_acquire))
                        return;

                    m_coverWorkerDone = true;

                    std::string text =
                        "Loaded " +
                        std::to_string(m_renderedCount) +
                        " / " +
                        std::to_string(m_items.size());

                    if (m_renderedCount < m_items.size())
                        text += "  |  Select LOAD MORE for another 24.";

                    m_status->setText(text);

                    perf_log_count(
                        "LIBRARY CATEGORY PROGRESSIVE COVERS DONE",
                        completed);
                });
            });
    }
};

class LibraryActivity : public brls::Activity
{
public:
    LibraryActivity() { g_libraryActivity = this; }

    ~LibraryActivity() override
    {
        m_coverLifetime->store(false, std::memory_order_release);

        if (m_coverWorker.joinable())
            m_coverWorker.join();

        if (m_worker.joinable())
            m_worker.join();

        if (g_libraryActivity == this)
            g_libraryActivity = nullptr;
    }

    brls::View* createContentView() override
    {
        log_stage("LIBRARY VIEW CREATE START");

        m_content = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(m_content);
        m_content->setWidthPercentage(100.0f);
        m_content->setHeightPercentage(100.0f);
        m_content->setPadding(30.0f);
        m_content->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("ANILIST LIBRARY");
        heading->setFontSize(28.0f);
        heading->setTextColor(nvgRGB(244, 246, 250));
        m_content->addView(heading);

        m_statusLabel = new brls::Label();
        m_statusLabel->setText("Loading your AniList account...");
        m_statusLabel->setFontSize(15.0f);
        m_statusLabel->setTextColor(nvgRGB(174, 184, 200));
        m_statusLabel->setMargins(0, 6, 0, 0);
        m_content->addView(m_statusLabel);

        brls::Label* libraryHint = new brls::Label();
        libraryHint->setText(
            "AniList library is managed from Settings. "
            "Local library lists are planned for a future update.");
        libraryHint->setFontSize(14.0f);
        libraryHint->setTextColor(nvgRGB(135, 147, 166));
        libraryHint->setMargins(0, 8, 0, 0);
        libraryHint->setFocusable(false);
        m_content->addView(libraryHint);

        m_scroll = new brls::ScrollingFrame();
        m_scroll->setWidthPercentage(100.0f);
        m_scroll->setHeight(570.0f);
        m_scroll->setMargins(0, 14, 0, 0);
        m_scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        m_sections = new brls::Box(brls::Axis::COLUMN);
        m_sections->setWidth(1160.0f);
        m_sections->setHeight(1900.0f);
        m_scroll->setContentView(m_sections);
        m_content->addView(m_scroll);

        log_stage("LIBRARY VIEW BUILT");
        return m_content;
    }

    void onContentAvailable() override
    {
        log_stage("ACTIVITY OPEN: Library");

        m_token = load_anilist_token();

        if (m_token.empty())
        {
            m_loaded = true;
            m_loadStatus =
                "No AniList account is linked. Use Settings to connect AniList. "
                "Local library lists are planned for a future update.";

            if (m_statusLabel)
                m_statusLabel->setText(m_loadStatus);

            build_sections();
            return;
        }

        m_loading = true;
        log_stage("LIBRARY REQUEST STARTED");

        m_worker = std::thread([this] {
            m_entries =
                fetch_anilist_library(m_token, m_username, m_loadStatus);

            m_ready.store(true, std::memory_order_release);
        });
    }

    void tick()
    {
        // Match Home: when a horizontal scrolling frame itself receives focus,
        // immediately transfer focus to its first actual anime card.
        brls::View* currentFocus = brls::Application::getCurrentFocus();

        for (brls::HScrollingFrame* sectionScroll : m_categoryScrolls)
        {
            if (!sectionScroll || currentFocus != sectionScroll)
                continue;

            brls::View* card = sectionScroll->getDefaultFocus();
            if (card)
                brls::Application::giveFocus(card);
            break;
        }

        if (!m_ready.load(std::memory_order_acquire))
            return;

        if (m_worker.joinable())
            m_worker.join();

        m_ready.store(false, std::memory_order_release);
        m_loading = false;
        m_loaded = true;

        build_sections();
        log_stage("LIBRARY SECTIONS ATTACHED");
    }

private:
    static constexpr size_t kCategoryCount = 6;
    static constexpr size_t kHomeBatchSize = 12;

    brls::Box* m_content = nullptr;
    brls::Label* m_statusLabel = nullptr;
    brls::ScrollingFrame* m_scroll = nullptr;
    brls::Box* m_sections = nullptr;

    std::string m_token;
    std::string m_username;
    std::string m_loadStatus;
    std::vector<AniListEntry> m_entries;

    std::thread m_worker;
    std::atomic<bool> m_ready{ false };
    bool m_loading = false;
    bool m_loaded = false;

    struct CoverJob
    {
        brls::Image* image = nullptr;
        std::string url;
        std::string path;
    };

    std::vector<brls::HScrollingFrame*> m_categoryScrolls;
    std::vector<CoverJob> m_pendingCoverJobs;
    std::thread m_coverWorker;
    bool m_coverWorkerDone = true;
    std::shared_ptr<std::atomic<bool>> m_coverLifetime =
        std::make_shared<std::atomic<bool>>(true);

    void build_sections()
    {
        if (!m_sections)
            return;

        clear_box(m_sections);
        m_categoryScrolls.clear();

        for (size_t category = 0;
             category < kCategoryCount;
             ++category)
        {
            const std::vector<AniListEntry> matching =
                filter_library_entries(m_entries, category);

            brls::Box* header =
                new brls::Box(brls::Axis::ROW);
            header->setWidth(1160.0f);
            header->setHeight(28.0f);
            header->setAlignItems(brls::AlignItems::CENTER);

            brls::Label* title = new brls::Label();
            title->setText(kLibraryStatusNames[category]);
            title->setFontSize(19.0f);
            title->setTextColor(nvgRGB(220, 228, 240));
            header->addView(title);

            brls::Label* count = new brls::Label();
            count->setText(
                "  " + std::to_string(matching.size()));
            count->setFontSize(13.0f);
            count->setTextColor(nvgRGB(135, 147, 166));
            header->addView(count);

            m_sections->addView(header);

            if (matching.empty())
            {
                brls::Label* empty = new brls::Label();
                empty->setText("No titles in this list.");
                empty->setFontSize(15.0f);
                empty->setTextColor(nvgRGB(135, 147, 166));
                empty->setFocusable(false);
                empty->setMargins(0, 5, 0, 9);
                m_sections->addView(empty);
                continue;
            }

            brls::HScrollingFrame* scroll =
                new brls::HScrollingFrame();
            scroll->setWidth(1160.0f);
            scroll->setHeight(260.0f);
            scroll->setMargins(0, 5, 0, 0);
            scroll->setScrollingBehavior(
                brls::ScrollingBehavior::CENTERED);

            brls::Box* row =
                new brls::Box(brls::Axis::ROW);

            const size_t visibleCount =
                std::min(kHomeBatchSize, matching.size());

            const bool hasMore =
                matching.size() > kHomeBatchSize;

            const size_t totalCards =
                visibleCount + (hasMore ? 1 : 0);

            row->setWidth(
                std::max(
                    1160.0f,
                    static_cast<float>(totalCards) * 192.0f));
            row->setHeight(252.0f);
            row->setAlignItems(brls::AlignItems::FLEX_START);

            for (size_t index = 0;
                 index < visibleCount;
                 ++index)
            {
                const AniListEntry& entry = matching[index];

                std::string subtitle;

                if (entry.listStatus == "CURRENT" ||
                    entry.listStatus == "REPEATING")
                {
                    subtitle =
                        "Episode " +
                        std::to_string(entry.progress);

                    if (entry.anime.episodes > 0)
                        subtitle +=
                            " / " +
                            std::to_string(entry.anime.episodes);
                }
                else
                {
                    subtitle =
                        entry.listName.empty()
                            ? entry.listStatus
                            : entry.listName;
                }

                brls::Image* poster = nullptr;

                row->addView(
                    make_library_anime_card(
                        entry.anime,
                        subtitle,
                        &poster));

                struct stat st;
                const std::string coverPath =
                    cached_cover_path(entry.anime.id);

                if (poster &&
                    !entry.anime.coverUrl.empty() &&
                    (stat(coverPath.c_str(), &st) != 0 ||
                     st.st_size <= 256))
                {
                    m_pendingCoverJobs.push_back({
                        poster,
                        entry.anime.coverUrl,
                        coverPath
                    });
                }
            }

            if (hasMore)
            {
                const size_t categoryCopy = category;

                row->addView(
                    make_home_load_more_card(
                        [this, categoryCopy] {
                            brls::Application::pushActivity(
                                new LibraryCategoryActivity(
                                    categoryCopy,
                                    m_entries),
                                brls::TransitionAnimation::NONE);
                        }));
            }

            scroll->setContentView(row);
            m_sections->addView(scroll);
            m_categoryScrolls.push_back(scroll);

            brls::Padding* spacer = new brls::Padding();
            spacer->setHeight(14.0f);
            m_sections->addView(spacer);
        }

        m_sections->setHeight(
            std::max(
                700.0f,
                static_cast<float>(
                    kCategoryCount * 304 + 100)));

        if (m_statusLabel)
        {
            std::string status = m_loadStatus;

            if (!m_username.empty())
                status += "  |  @" + m_username;

            status +=
                "  |  " +
                std::to_string(m_entries.size()) +
                " library entries";

            m_statusLabel->setText(status);
        }

        start_cover_worker();

        log_stage("LIBRARY HOME ROWS BUILT");
    }

    void start_cover_worker()
    {
        if (m_pendingCoverJobs.empty())
        {
            log_stage(
                "LIBRARY PROGRESSIVE COVERS: no uncached covers");
            return;
        }

        if (m_coverWorker.joinable())
        {
            if (!m_coverWorkerDone)
                return;

            m_coverWorker.join();
        }

        std::vector<CoverJob> jobs;
        jobs.swap(m_pendingCoverJobs);

        m_coverWorkerDone = false;
        const auto lifetime = m_coverLifetime;
        const size_t total = jobs.size();

        char startMarker[128];
        std::snprintf(
            startMarker,
            sizeof(startMarker),
            "LIBRARY PROGRESSIVE COVERS START count=%zu",
            total);
        log_stage(startMarker);

        m_coverWorker = std::thread(
            [this, lifetime, jobs = std::move(jobs), total] {
                size_t completed = 0;

                for (const CoverJob& job : jobs)
                {
                    if (!lifetime->load(
                            std::memory_order_acquire))
                        return;

                    if (!download_image(job.url, job.path))
                        continue;

                    ++completed;

                    brls::sync(
                        [this,
                         lifetime,
                         image = job.image,
                         path = job.path,
                         completed,
                         total] {
                            if (!lifetime->load(
                                    std::memory_order_acquire))
                                return;

                            image->setImageFromFile(path);

                            char marker[128];
                            std::snprintf(
                                marker,
                                sizeof(marker),
                                "LIBRARY PROGRESSIVE COVER READY %zu/%zu",
                                completed,
                                total);
                            log_stage(marker);
                        });
                }

                brls::sync(
                    [this, lifetime, completed, total] {
                        if (!lifetime->load(
                                std::memory_order_acquire))
                            return;

                        m_coverWorkerDone = true;

                        std::string status = m_loadStatus;
                        if (!m_username.empty())
                            status += "  |  @" + m_username;

                        if (m_statusLabel)
                            m_statusLabel->setText(status);

                        char marker[128];
                        std::snprintf(
                            marker,
                            sizeof(marker),
                            "LIBRARY PROGRESSIVE COVERS DONE %zu/%zu",
                            completed,
                            total);
                        log_stage(marker);
                    });
            });
    }
};


};


class SettingsActivity : public brls::Activity
{
public:
    brls::View* createContentView() override
    {
        log_stage("ACTIVITY OPEN: Settings");
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(root);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("SETTINGS");
        heading->setFontSize(28.0f);
        root->addView(heading);

        brls::Label* account = new brls::Label();
        account->setText(load_anilist_token().empty()
            ? "No AniList account linked."
            : "AniList account token is saved on this Switch.");
        account->setFontSize(16.0f);
        account->setTextColor(nvgRGB(174, 184, 200));
        account->setMargins(0, 7, 0, 0);
        root->addView(account);

        brls::Label* pair = new brls::Label();
        pair->setText(load_anilist_token().empty()
            ? "LINK ANILIST FROM PHONE"
            : "PAIR AGAIN / CHANGE ANILIST ACCOUNT");
        pair->setFontSize(17.0f);
        pair->setTextColor(nvgRGB(97, 207, 226));
        pair->setMargins(0, 12, 0, 0);
        pair->setFocusable(true);
        pair->registerAction("Link AniList account", brls::BUTTON_A, [](brls::View*) {
            log_stage("SETTINGS OPEN PAIRING");
            brls::Application::pushActivity(new PairingActivity(), brls::TransitionAnimation::NONE);
            return true;
        });
        root->addView(pair);

        brls::Label* sourceHeading = new brls::Label();
        sourceHeading->setText("EPISODE SOURCES");
        sourceHeading->setFontSize(20.0f);
        sourceHeading->setMargins(0, 18, 0, 10);
        root->addView(sourceHeading);

        for (size_t i = 0; i < kApiSourceCount; ++i)
            root->addView(make_toggle(i));
        log_stage("SETTINGS VIEW BUILT");
        return root;
    }

private:
    brls::Box* make_toggle(size_t index)
    {
        brls::Box* toggle = new brls::Box(brls::Axis::ROW);
        toggle->setWidthPercentage(100.0f);
        toggle->setHeight(46.0f);
        toggle->setMargins(0, 9, 0, 0);
        toggle->setPadding(10.0f);
        toggle->setAlignItems(brls::AlignItems::CENTER);
        toggle->setBackgroundColor(nvgRGB(27, 34, 48));
        toggle->setBorderColor(nvgRGB(48, 57, 74));
        toggle->setBorderThickness(1.0f);
        toggle->setCornerRadius(6.0f);
        toggle->setFocusable(true);

        brls::Label* label = new brls::Label();
        label->setText(toggle_text(index));
        label->setFontSize(17.0f);
        label->setTextColor(nvgRGB(214, 222, 235));
        toggle->addView(label);
        toggle->registerAction("Toggle source API", brls::BUTTON_A, [this, label, index](brls::View*) {
            log_stage("SETTINGS SOURCE TOGGLE");
            g_providerEnabled[index] = !g_providerEnabled[index];
            save_source_settings();
            label->setText(toggle_text(index));
            return true;
        });
        return toggle;
    }

    std::string toggle_text(size_t index) const
    {
        return std::string(g_providerEnabled[index] ? "[ON]  " : "[OFF] ") +
            kApiSources[index].name + "    (press A to toggle)";
    }
};

struct ContinueWatchItem
{
    SaikouAnime anime;
    int progress = 0;
};

static std::vector<ContinueWatchItem> fetch_anilist_continue_watching(
    const std::string& token, std::string& message, size_t maxItems = 24)
{
    std::vector<ContinueWatchItem> items;
    if (token.empty())
    {
        message = "Link an AniList account to sync your watching list.";
        return items;
    }

    std::string username;
    std::string libraryStatus;
    const std::vector<AniListEntry> library = fetch_anilist_library(token, username, libraryStatus);

    for (const AniListEntry& entry : library)
    {
        if (entry.listStatus != "CURRENT")
            continue;

        ContinueWatchItem item;
        item.anime = entry.anime;
        item.progress = entry.progress;
        item.anime.posterPath = cached_cover_path(item.anime.id);
        items.push_back(item);

        if (maxItems > 0 && items.size() >= maxItems)
            break;
    }

    if (!items.empty())
        message = "Watching on AniList  |  @" + username;
    else
        message = "@" + username + " is linked. No Watching entries yet.";

    return items;
}

static std::vector<ContinueWatchItem> load_local_continue_watching(size_t maxItems = 24)
{
    std::vector<ContinueWatchItem> items;
    FILE* file = std::fopen(kLocalContinuePath, "r");
    if (!file)
        return items;

    ContinueWatchItem legacy;
    bool sawLegacy = false;
    std::map<int, ContinueWatchItem> byIndex;
    char line[2048] = {};

    while (std::fgets(line, sizeof(line), file))
    {
        std::string value(line);
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.pop_back();

        const size_t split = value.find('=');
        if (split == std::string::npos)
            continue;

        const std::string key = value.substr(0, split);
        const std::string data = value.substr(split + 1);

        auto parseIndexed = [&](const std::string& prefix, int& index, std::string& field) -> bool {
            if (key.rfind(prefix, 0) != 0)
                return false;
            const size_t dot = key.find('.', prefix.size());
            if (dot == std::string::npos)
                return false;
            index = std::atoi(key.substr(prefix.size(), dot - prefix.size()).c_str());
            field = key.substr(dot + 1);
            return index >= 0 && index < static_cast<int>(kLocalContinueCapacity);
        };

        int index = 0;
        std::string field;
        if (parseIndexed("item", index, field))
        {
            ContinueWatchItem& item = byIndex[index];
            if (field == "id")
                item.anime.id = std::atoi(data.c_str());
            else if (field == "title")
                item.anime.title = data;
            else if (field == "coverUrl")
                item.anime.coverUrl = data;
            else if (field == "episodes")
                item.anime.episodes = std::atoi(data.c_str());
            else if (field == "progress")
                item.progress = std::atoi(data.c_str());
            continue;
        }

        // Backward compatibility with the original single-entry file format.
        sawLegacy = true;
        if (key == "id")
            legacy.anime.id = std::atoi(data.c_str());
        else if (key == "title")
            legacy.anime.title = data;
        else if (key == "coverUrl")
            legacy.anime.coverUrl = data;
        else if (key == "episodes")
            legacy.anime.episodes = std::atoi(data.c_str());
        else if (key == "progress")
            legacy.progress = std::atoi(data.c_str());
    }

    std::fclose(file);

    if (sawLegacy && legacy.anime.id > 0 && !legacy.anime.title.empty())
    {
        legacy.anime.posterPath = cached_cover_path(legacy.anime.id);
        items.push_back(legacy);
    }

    for (const auto& entry : byIndex)
    {
        if (entry.second.anime.id <= 0 || entry.second.anime.title.empty())
            continue;

        ContinueWatchItem item = entry.second;
        item.anime.posterPath = cached_cover_path(item.anime.id);
        items.push_back(item);

        if (maxItems > 0 && items.size() >= maxItems)
            break;
    }

    return items;
}

// Called by the future video-player path whenever playback actually starts/resumes.
// Upserts the title, keeps the newest progress, and caps local history at 24 titles.
static bool save_local_continue_watching(const ContinueWatchItem& item)
{
    if (item.anime.id <= 0 || item.anime.title.empty())
        return false;

    std::vector<ContinueWatchItem> items = load_local_continue_watching(0);
    bool replaced = false;

    for (ContinueWatchItem& existing : items)
    {
        if (existing.anime.id != item.anime.id)
            continue;

        existing = item;
        replaced = true;
        break;
    }

    if (!replaced)
        items.insert(items.begin(), item);

    if (items.size() > kLocalContinueCapacity)
        items.resize(kLocalContinueCapacity);

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SaikouTV", 0777);

    FILE* file = std::fopen(kLocalContinuePath, "w");
    if (!file)
        return false;

    for (size_t i = 0; i < items.size() && i < kLocalContinueCapacity; ++i)
    {
        const ContinueWatchItem& current = items[i];
        std::fprintf(file, "item%zu.id=%d\n", i, current.anime.id);
        std::fprintf(file, "item%zu.title=%s\n", i, current.anime.title.c_str());
        std::fprintf(file, "item%zu.coverUrl=%s\n", i, current.anime.coverUrl.c_str());
        std::fprintf(file, "item%zu.episodes=%d\n", i, current.anime.episodes);
        std::fprintf(file, "item%zu.progress=%d\n", i, current.progress);
    }

    std::fclose(file);
    return true;
}


class TrendingCatalogActivity;
static TrendingCatalogActivity* g_trendingCatalogActivity = nullptr;

static brls::Box* make_home_load_more_card(std::function<void()> callback)
{
    brls::Box* card = new brls::Box(brls::Axis::COLUMN);
    card->setWidth(184.0f);
    card->setHeight(244.0f);
    card->setPadding(10.0f);
    card->setMargins(3, 7, 3, 0);
    card->setBackgroundColor(nvgRGB(27, 34, 48));
    card->setBorderColor(nvgRGB(48, 57, 74));
    card->setBorderThickness(1.0f);
    card->setCornerRadius(9.0f);
    card->setFocusable(true);
    card->setJustifyContent(brls::JustifyContent::CENTER);
    card->setAlignItems(brls::AlignItems::CENTER);

    brls::Label* label = new brls::Label();
    label->setText("LOAD MORE");
    label->setFontSize(16.0f);
    label->setTextColor(nvgRGB(244, 246, 250));
    label->setSingleLine(false);
    label->setFocusable(false);
    card->addView(label);

    card->registerAction("Load more trending anime", brls::BUTTON_A, [callback](brls::View*) {
        callback();
        return true;
    });
    return card;
}

static void render_home_trending_cards(
    brls::Box* container,
    const std::vector<SaikouAnime>& items,
    std::function<void()> loadMoreCallback)
{
    if (!container) return;
    clear_box(container);

    const float itemWidth = 192.0f;
    const float contentWidth =
        std::max(1160.0f, static_cast<float>(items.size() + 1) * itemWidth);
    container->setWidth(contentWidth);

    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setWidth(contentWidth);
    row->setHeight(252.0f);
    row->setAlignItems(brls::AlignItems::FLEX_START);

    for (const SaikouAnime& anime : items)
        row->addView(make_anime_card(anime));

    row->addView(make_home_load_more_card(std::move(loadMoreCallback)));
    container->addView(row);
}

class TrendingCatalogActivity : public brls::Activity
{
public:
    TrendingCatalogActivity()
    {
        g_trendingCatalogActivity = this;
    }

    ~TrendingCatalogActivity() override
    {
        m_coverLifetime->store(false, std::memory_order_release);

        if (m_worker.joinable())
            m_worker.join();
        if (m_coverWorker.joinable())
            m_coverWorker.join();

        if (g_trendingCatalogActivity == this)
            g_trendingCatalogActivity = nullptr;
    }

    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        register_page_back_action(root);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("TRENDING ANIME");
        heading->setFontSize(30.0f);
        heading->setTextColor(nvgRGB(244, 246, 250));
        root->addView(heading);

        m_status = new brls::Label();
        m_status->setText("Loading trending anime...");
        m_status->setFontSize(14.0f);
        m_status->setTextColor(nvgRGB(174, 184, 200));
        m_status->setMargins(0, 7, 0, 0);
        root->addView(m_status);

        m_scroll = new brls::ScrollingFrame();
        m_scroll->setWidthPercentage(100.0f);
        m_scroll->setHeight(600.0f);
        m_scroll->setMargins(0, 12, 0, 0);
        m_scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);

        m_grid = new brls::Box(brls::Axis::COLUMN);
        m_grid->setWidth(1160.0f);
        m_grid->setHeight(900.0f);
        m_scroll->setContentView(m_grid);
        root->addView(m_scroll);
        return root;
    }

    void onContentAvailable() override
    {
        start_load(1);
    }

    void tick()
    {
        if (m_ready.load(std::memory_order_acquire))
        {
            if (m_worker.joinable())
                m_worker.join();

            append_loaded_items();
            m_ready.store(false, std::memory_order_release);
            m_loading = false;

            if (m_status)
            {
                std::string label = "Loaded " +
                    std::to_string(m_items.size()) + " trending anime";
                if (m_hasMore)
                    label += " — select LOAD MORE for another 30";
                else
                    label += " — end of results";
                m_status->setText(label);
            }
        }

        start_pending_cover_worker();
    }

private:
    struct CoverJob
    {
        brls::Image* image = nullptr;
        std::string url;
        std::string path;
    };

    brls::Label* m_status = nullptr;
    brls::ScrollingFrame* m_scroll = nullptr;
    brls::Box* m_grid = nullptr;

    std::vector<SaikouAnime> m_items;
    std::thread m_worker;
    std::thread m_coverWorker;
    std::atomic<bool> m_ready{ false };
    std::shared_ptr<std::atomic<bool>> m_coverLifetime =
        std::make_shared<std::atomic<bool>>(true);

    std::vector<CoverJob> m_pendingCoverJobs;