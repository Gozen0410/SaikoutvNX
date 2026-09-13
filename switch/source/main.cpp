#include <borealis.hpp>
#include <borealis/views/tab_frame.hpp>
#include <borealis/views/image.hpp>
#include <switch.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
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
    // Use Borealis' normal focus resolution now that the TabFrame lazy
    // creator lifetime is fixed. This makes the sidebar receive initial focus.
    brls::View* getDefaultFocus() override { return brls::Activity::getDefaultFocus(); }
    brls::View* createContentView() override
    {
        return brls::View::createFromXMLResource("activity/main.xml");
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

    CURLcode globalRc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (globalRc != CURLE_OK)
    {
        log_stage("CURL GLOBAL INIT FAILED");
        if (socketOwned) socketExit();
        result.status = "HTTP init failed";
        return result;
    }
    log_stage("CURL GLOBAL INIT OK");

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        log_stage("CURL EASY INIT FAILED");
        curl_global_cleanup();
        if (socketOwned) socketExit();
        result.status = "HTTP client init failed";
        return result;
    }

    std::string response;
    const char* url = "https://miruro.zenos.my.id/trending?per_page=6";
    log_stage("BEFORE API REQUEST");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.2");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    bool primaryOk = false;
    CURLcode requestRc = CURLE_OK;
    long httpCode = 0;
    for (int attempt = 1; attempt <= 3 && !primaryOk; ++attempt)
    {
        char marker[64];
        std::snprintf(marker, sizeof(marker), "API REQUEST ATTEMPT %d", attempt);
        log_stage(marker);
        response.clear();
        requestRc = curl_easy_perform(curl);
        httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        primaryOk = api_response_is_valid(requestRc, httpCode, response, "MIRURO REQUEST");
    }

    if (primaryOk)
    {
        result.status = "API online - trending data received";
        result.response = response;
    }
    else
    {
        log_stage("MIRURO FAILED - STARTING ANILIST FALLBACK");
        curl_easy_reset(curl);
        response.clear();
        const char* anilistUrl = "https://graphql.anilist.co";
        const char* anilistBody = "{\"query\":\"query { Page(page: 1, perPage: 6) { results: media(type: ANIME, sort: TRENDING_DESC) { title { english romaji native } coverImage { large } format averageScore } } }\"}";
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        log_stage("BEFORE ANILIST FALLBACK REQUEST");
        curl_easy_setopt(curl, CURLOPT_URL, anilistUrl);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, anilistBody);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(anilistBody)));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.2");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        requestRc = curl_easy_perform(curl);
        httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        const bool fallbackOk = api_response_is_valid(requestRc, httpCode, response, "ANILIST FALLBACK REQUEST");
        if (fallbackOk)
        {
            result.status = "AniList online - trending data received";
            result.response = response;
        }
        else
            result.status = "API request failed - UI still running";
        curl_slist_free_all(headers);
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();
    log_stage("API PROBE CLEANUP COMPLETE");
    return result;
}

static bool download_image(const std::string& url, const std::string& path)
{
    if (url.empty()) return false;
    log_stage("BEFORE COVER IMAGE DOWNLOAD");
    Result socketRc = socketInitializeDefault();
    bool socketOwned = false;
    if (R_SUCCEEDED(socketRc)) socketOwned = true;
    else if (socketRc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
    {
        log_stage("COVER IMAGE SOCKET INITIALIZE FAILED");
        return false;
    }
    CURLcode globalRc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (globalRc != CURLE_OK)
    {
        log_stage("COVER IMAGE CURL GLOBAL INIT FAILED");
        if (socketOwned) socketExit();
        return false;
    }
    CURL* curl = curl_easy_init();
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!curl || !file)
    {
        log_stage("COVER IMAGE CURL OR FILE INIT FAILED");
        if (file) std::fclose(file);
        if (curl) curl_easy_cleanup(curl);
        curl_global_cleanup();
        if (socketOwned) socketExit();
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.2");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    CURLcode requestRc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    std::fclose(file);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();
    if (requestRc != CURLE_OK || httpCode < 200 || httpCode >= 300)
    {
        std::remove(path.c_str());
        log_stage("COVER IMAGE DOWNLOAD FAILED");
        return false;
    }
    log_stage("COVER IMAGE DOWNLOAD OK");
    return true;
}

static std::string json_string_after(const std::string& text, size_t from, const char* key, size_t limit)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t keyPos = text.find(needle, from);
    if (keyPos == std::string::npos || keyPos >= limit) return std::string();
    size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos || colon >= limit) return std::string();
    size_t quote = text.find('"', colon + 1);
    if (quote == std::string::npos || quote >= limit) return std::string();
    std::string value;
    for (size_t i = quote + 1; i < limit; ++i)
    {
        if (text[i] == '\\' && i + 1 < limit)
        {
            const char escaped = text[i + 1];
            if (escaped == '"' || escaped == '\\' || escaped == '/') value.push_back(escaped);
            else if (escaped == 'n' || escaped == 't') value.push_back(' ');
            else value.push_back(escaped);
            ++i;
            continue;
        }
        if (text[i] == '"') break;
        value.push_back(text[i]);
    }
    return value;
}

static std::string json_value_after(const std::string& text, size_t from, const char* key, size_t limit)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t keyPos = text.find(needle, from);
    if (keyPos == std::string::npos || keyPos >= limit) return std::string();
    size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos || colon >= limit) return std::string();
    size_t start = colon + 1;
    while (start < limit && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) ++start;
    size_t end = start;
    while (end < limit && text[end] != ',' && text[end] != '}' && text[end] != '\n') ++end;
    return text.substr(start, end - start);
}

static std::string json_nested_string_after(const std::string& text, size_t from, const char* parentKey, const char* childKey, size_t limit)
{
    const std::string parent = std::string("\"") + parentKey + "\"";
    size_t parentPos = text.find(parent, from);
    if (parentPos == std::string::npos || parentPos >= limit) return std::string();
    return json_string_after(text, parentPos + parent.size(), childKey, limit);
}

static std::vector<std::string> extract_trending_titles(const std::string& response)
{
    std::vector<std::string> titles;
    size_t resultsPos = response.find("\"results\"");
    if (resultsPos == std::string::npos) return titles;
    size_t cursor = resultsPos;
    while (titles.size() < 6)
    {
        size_t titlePos = response.find("\"title\"", cursor);
        if (titlePos == std::string::npos) break;
        size_t objectStart = response.find('{', titlePos);
        if (objectStart == std::string::npos) break;
        size_t objectEnd = response.find('}', objectStart + 1);
        if (objectEnd == std::string::npos) break;
        std::string title = json_string_after(response, objectStart, "english", objectEnd);
        if (title.empty()) title = json_string_after(response, objectStart, "romaji", objectEnd);
        if (title.empty()) title = json_string_after(response, objectStart, "native", objectEnd);
        if (!title.empty()) titles.push_back(title);
        cursor = objectEnd + 1;
    }
    return titles;
}

static std::vector<std::string> extract_trending_details(const std::string& response)
{
    std::vector<std::string> details;
    size_t resultsPos = response.find("\"results\"");
    if (resultsPos == std::string::npos) return details;
    size_t cursor = resultsPos;
    while (details.size() < 6)
    {
        size_t titlePos = response.find("\"title\"", cursor);
        if (titlePos == std::string::npos) break;
        size_t objectStart = response.find('{', titlePos);
        if (objectStart == std::string::npos) break;
        size_t objectEnd = response.find('}', objectStart + 1);
        if (objectEnd == std::string::npos) break;
        size_t itemEnd = response.find("\"title\"", objectEnd + 1);
        if (itemEnd == std::string::npos) itemEnd = response.size();
        std::string format = json_string_after(response, objectEnd, "format", itemEnd);
        if (format.empty()) format = json_string_after(response, objectEnd, "type", itemEnd);
        std::string score = json_value_after(response, objectEnd, "averageScore", itemEnd);
        std::string detail;
        if (!format.empty()) detail += format;
        if (!score.empty() && score != "null")
        {
            if (!detail.empty()) detail += "  •  ";
            detail += "Score: " + score;
        }
        details.push_back(detail);
        cursor = objectEnd + 1;
    }
    return details;
}

static std::vector<std::string> extract_trending_covers(const std::string& response)
{
    std::vector<std::string> covers;
    size_t resultsPos = response.find("\"results\"");
    if (resultsPos == std::string::npos) return covers;
    size_t cursor = resultsPos;
    while (covers.size() < 6)
    {
        size_t titlePos = response.find("\"title\"", cursor);
        if (titlePos == std::string::npos) break;
        size_t itemEnd = response.find("\"title\"", titlePos + 8);
        if (itemEnd == std::string::npos) itemEnd = response.size();
        std::string cover = json_nested_string_after(response, titlePos, "coverImage", "large", itemEnd);
        covers.push_back(cover);
        cursor = itemEnd;
    }
    return covers;
}

static std::string compact_title(const std::string& title)
{
    constexpr size_t kMaxLineChars = 18;
    constexpr size_t kMaxTotalChars = 36;
    if (title.size() <= kMaxLineChars) return title;
    std::string compact = title;
    if (compact.size() > kMaxTotalChars)
    {
        compact.resize(kMaxTotalChars - 3);
        const size_t lastSpace = compact.find_last_of(' ');
        if (lastSpace != std::string::npos && lastSpace >= 10) compact.resize(lastSpace);
        compact += "...";
    }
    size_t split = compact.find_last_of(' ', kMaxLineChars);
    if (split == std::string::npos || split < 8) split = kMaxLineChars;
    std::string first = compact.substr(0, split);
    std::string second = compact.substr(split);
    while (!second.empty() && second.front() == ' ') second.erase(second.begin());
    if (second.size() > kMaxLineChars)
    {
        second.resize(kMaxLineChars - 3);
        const size_t lastSpace = second.find_last_of(' ');
        if (lastSpace != std::string::npos && lastSpace >= 6) second.resize(lastSpace);
        second += "...";
    }
    return first + "\n" + second;
}

static void render_trending(brls::Box* homeBox, const std::string& response)
{
    if (!homeBox || response.empty()) return;
    log_stage("BEFORE TRENDING PARSE");
    std::vector<std::string> titles = extract_trending_titles(response);
    std::vector<std::string> details = extract_trending_details(response);
    std::vector<std::string> covers = extract_trending_covers(response);
    char marker[64];
    std::snprintf(marker, sizeof(marker), "TRENDING PARSE FOUND %zu TITLES", titles.size());
    log_stage(marker);
    if (titles.empty())
    {
        log_stage("TRENDING PARSE FOUND NO TITLES");
        return;
    }
    brls::Label* heading = new brls::Label();
    heading->setText("Trending Now");
    heading->setFontSize(27);
    heading->setMargins(0, 10, 0, 0);
    homeBox->addView(heading);
    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setGrow(0.0f);
    row->setAlignItems(brls::AlignItems::FLEX_START);
    row->setMargins(0, 7, 0, 0);
    homeBox->addView(row);
    const size_t cardCount = std::min<size_t>(titles.size(), 6);
    for (size_t i = 0; i < cardCount; ++i)
    {
        brls::Box* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(124);
        card->setMargins(2, 3, 2, 0);
        card->setFocusable(true);
        card->setHighlightPadding(5.0f);
        card->setCornerRadius(5.0f);
        card->setFocusSound(brls::SOUND_FOCUS_CHANGE);
        bool imageAttached = false;
        if (i < covers.size() && !covers[i].empty())
        {
            char pathBuffer[128];
            std::snprintf(pathBuffer, sizeof(pathBuffer), "%s/trending_%zu.jpg", kCacheDir, i);
            const std::string imagePath = pathBuffer;
            log_stage("BEFORE TRENDING CARD IMAGE DOWNLOAD");
            if (download_image(covers[i], imagePath))
            {
                brls::Image* image = new brls::Image();
                image->setDimensions(116, 174);
                image->setScalingType(brls::ImageScalingType::CROP);
                image->setImageFromFile(imagePath);
                image->setFocusable(false);
                card->addView(image);
                imageAttached = true;
                log_stage("TRENDING CARD IMAGE ATTACHED");
            }
        }
        if (!imageAttached)
        {
            brls::Label* missing = new brls::Label();
            missing->setText("No image");
            missing->setFontSize(13);
            missing->setSingleLine(true);
            card->addView(missing);
        }
        brls::Label* title = new brls::Label();
        title->setText(compact_title(titles[i]));
        title->setFontSize(14);
        title->setLineHeight(17);
        title->setMaxWidth(116);
        title->setMargins(2, 4, 2, 0);
        title->setFocusable(false);
        card->addView(title);
        if (i < details.size() && !details[i].empty())
        {
            brls::Label* detail = new brls::Label();
            detail->setText(details[i]);
            detail->setFontSize(11);
            detail->setLineHeight(14);
            detail->setMaxWidth(116);
            detail->setSingleLine(true);
            detail->setMargins(2, 1, 2, 0);
            detail->setFocusable(false);
            card->addView(detail);
        }
        card->registerAction("Open anime", brls::BUTTON_A, [i](brls::View*) {
            char marker[64];
            std::snprintf(marker, sizeof(marker), "TRENDING CARD SELECTED %zu", i);
            log_stage(marker);
            return true;
        });
        row->addView(card);
    }
    log_stage("TRENDING UI ATTACHED");
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    fsdevMountSdmc();
    ensure_app_dirs();
    g_log = std::fopen(kLogPath, "w");
    log_stage("entered main");
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    log_stage("logger configured");
    Result romfsRc = romfsInit();
    log_stage(R_SUCCEEDED(romfsRc) ? "romfsInit OK" : "romfsInit FAILED");
    log_stage("closing Saikou log before Borealis init");
    if (g_log) { std::fclose(g_log); g_log = nullptr; }
    if (!brls::Application::init()) return EXIT_FAILURE;
    log_stage("Application::init OK");
    brls::Application::createWindow("Saikou Switch");
    log_stage("Borealis window created");
    brls::Application::setGlobalQuit(false);
    log_stage("BEFORE HomeActivity construction");
    HomeActivity* activity = new HomeActivity();
    log_stage("AFTER HomeActivity construction");
    log_stage("BEFORE pushActivity(home) WITH NORMAL FOCUS");
    brls::Application::pushActivity(activity);
    log_stage("AFTER pushActivity(home) WITH NORMAL FOCUS");
    brls::View* root = activity->getContentView();
    log_stage(root ? "ROOT VIEW VALID AFTER PUSH" : "ROOT VIEW NULL AFTER PUSH");
    brls::TabFrame* tabFrame = dynamic_cast<brls::TabFrame*>(root);
    log_stage(tabFrame ? "TABFRAME PUBLIC API TARGET VALID" : "TABFRAME PUBLIC API TARGET NULL");
    if (tabFrame)
    {
        const char* homePath = "romfs:/xml/activity/home.xml";
        log_stage("BEFORE HOME RESOURCE PREFLIGHT");
        FILE* homeFile = std::fopen(homePath, "rb");
        if (!homeFile)
            log_stage("HOME RESOURCE PREFLIGHT OPEN FAILED");
        else
        {
            log_stage("HOME RESOURCE PREFLIGHT OPEN OK");
            std::fseek(homeFile, 0, SEEK_END);
            long fileSize = std::ftell(homeFile);
            std::fseek(homeFile, 0, SEEK_SET);
            if (fileSize <= 0 || fileSize > 1024 * 1024)
            {
                std::fclose(homeFile);
                log_stage("HOME RESOURCE PREFLIGHT INVALID SIZE");
            }
            else
            {
                std::string xml(static_cast<size_t>(fileSize), '\0');
                size_t readSize = std::fread(xml.data(), 1, xml.size(), homeFile);
                std::fclose(homeFile);
                if (readSize != xml.size())
                    log_stage("HOME RESOURCE PREFLIGHT READ FAILED");
                else
                {
                    log_stage("HOME RESOURCE PREFLIGHT READ OK");
                    log_stage("BEFORE HOME XML STRING INFLATION");
                    brls::View* homeContent = brls::View::createFromXMLString(xml);
                    log_stage(homeContent ? "HOME XML STRING RETURNED VIEW" : "HOME XML STRING RETURNED NULL");
                    if (homeContent)
                    {
                        log_stage("BEFORE API PROBE");
                        ApiResult api = run_api_probe();
                        log_stage("AFTER API PROBE");
                        brls::Box* homeBox = dynamic_cast<brls::Box*>(homeContent);
                        if (homeBox)
                        {
                            // Keep the whole content panel as a valid fallback focus target
                            // when the network is unavailable and no cards exist.
                            homeBox->setFocusable(true);
                            brls::Label* status = new brls::Label();
                            status->setText(api.status);
                            status->setFontSize(16);
                            homeBox->addView(status);
                            log_stage("API STATUS LABEL ATTACHED");
                            if (!api.response.empty()) render_trending(homeBox, api.response);
                        }
                        else
                            log_stage("HOME ROOT IS NOT BOX");
                        log_stage("BEFORE PUBLIC TABFRAME CONTENT SET");
                        tabFrame->setTabContent(homeContent);
                        log_stage("AFTER PUBLIC TABFRAME CONTENT SET");
                        homeContent = nullptr;
                    }
                }
            }
        }
    }
    log_stage("AFTER HOME CONTENT ATTACHMENT PATH");
    int loopCount = 0;
    while (brls::Application::mainLoop())
    {
        ++loopCount;
        if (loopCount <= 5)
        {
            char marker[64];
            std::snprintf(marker, sizeof(marker), "mainLoop returned true #%d", loopCount);
            log_stage(marker);
        }
    }
    log_stage("mainLoop returned false");
    romfsExit();
    return EXIT_SUCCESS;
}