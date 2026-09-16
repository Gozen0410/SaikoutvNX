#include <borealis.hpp>
#include <borealis/views/tab_frame.hpp>
#include <borealis/views/image.hpp>
#include <switch.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static constexpr const char* kAppDir = "sdmc:/switch/SaikouTV";
static constexpr const char* kCacheDir = "sdmc:/switch/SaikouTV/cache";
static constexpr const char* kLogPath = "sdmc:/switch/SaikouTV/saikou_debug.log";

static FILE* g_log = nullptr;

static void ensure_app_dirs()
{
    mkdir("sdmc:/switch", 0777);
    mkdir(kAppDir, 0777);
    mkdir(kCacheDir, 0777);
}

static void log_stage(const char* stage)
{
    if (!g_log) g_log = std::fopen(kLogPath, "a");
    if (!g_log) return;
    std::fprintf(g_log, "[Saikou] %s\n", stage);
    std::fflush(g_log);
}

static size_t api_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* output = static_cast<std::string*>(userdata);
    const size_t bytes = size * nmemb;
    constexpr size_t kMaxResponse = 512 * 1024;
    if (output->size() < kMaxResponse)
    {
        const size_t remaining = kMaxResponse - output->size();
        output->append(ptr, bytes < remaining ? bytes : remaining);
    }
    return bytes;
}

static size_t file_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    FILE* file = static_cast<FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, file);
}

class HomeActivity : public brls::Activity
{
public:
    brls::View* getDefaultFocus() override { return brls::Activity::getDefaultFocus(); }
    brls::View* createContentView() override
    {
        log_stage("BEFORE XML createContentView");
        brls::View* view = brls::View::createFromXMLResource("activity/main.xml");
        log_stage(view ? "AFTER XML createContentView OK" : "AFTER XML createContentView NULL");
        return view;
    }
};

struct ApiResult
{
    std::string status;
    std::string response;
};

static bool api_response_is_valid(CURLcode requestRc, long httpCode, const std::string& response, const char* markerPrefix)
{
    char marker[128];
    if (requestRc == CURLE_OK && httpCode >= 200 && httpCode < 300 && response.find("results") != std::string::npos)
    {
        std::snprintf(marker, sizeof(marker), "%s OK HTTP %ld BYTES %zu", markerPrefix, httpCode, response.size());
        log_stage(marker);
        return true;
    }

    std::snprintf(marker, sizeof(marker), "%s FAILED CURL %d HTTP %ld BYTES %zu", markerPrefix, static_cast<int>(requestRc), httpCode, response.size());
    log_stage(marker);
    return false;
}

static ApiResult run_api_probe()
{
    ApiResult result;
    log_stage("BEFORE SOCKET INITIALIZE");
    Result socketRc = socketInitializeDefault();
    bool socketOwned = false;
    if (R_SUCCEEDED(socketRc))
    {
        socketOwned = true;
        log_stage("SOCKET INITIALIZE OK");
    }
    else if (socketRc == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
        log_stage("SOCKET ALREADY INITIALIZED - REUSING EXISTING SOCKET DEVICE");
    else
    {
        char marker[128];
        std::snprintf(marker, sizeof(marker), "SOCKET INITIALIZE FAILED RC 0x%08X LAST 0x%08X", static_cast<unsigned int>(socketRc), static_cast<unsigned int>(socketGetLastResult()));
        log_stage(marker);
        SocketInitConfig config = *socketGetDefaultInitConfig();
        config.bsd_service_type = BsdServiceType_Auto;
        log_stage("BEFORE SOCKET AUTO INITIALIZE");
        socketRc = socketInitialize(&config);
        if (R_SUCCEEDED(socketRc))
        {
            socketOwned = true;
            log_stage("SOCKET AUTO INITIALIZE OK");
        }
        else
        {
            std::snprintf(marker, sizeof(marker), "SOCKET AUTO INITIALIZE FAILED RC 0x%08X LAST 0x%08X", static_cast<unsigned int>(socketRc), static_cast<unsigned int>(socketGetLastResult()));
            log_stage(marker);
            result.status = "Network init failed";
            return result;
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result.status = "CURL init failed";
        if (socketOwned) socketExit();
        return result;
    }

    const char* url = "https://graphql.anilist.co";
    const char* query = "query{Page(perPage:6){media(sort:TRENDING_DESC,type:ANIME){id title{romaji english native}coverImage{large}format averageScore}}}";
    std::string post = std::string("{\"query\":\"") + query + "\"}";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouTVNX/0.2");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (api_response_is_valid(rc, code, response, "ANILIST HOME REQUEST"))
    {
        result.status = "AniList online - trending data received";
        result.response = response;
    }
    else
        result.status = "AniList request failed";

    if (socketOwned) socketExit();
    return result;
}
