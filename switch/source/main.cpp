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
#include <cstring>

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

static ApiResult run_api_probe()
{
    ApiResult result;
    log_stage("BEFORE ANILIST HOME REQUEST");

    Result socketRc = socketInitializeDefault();
    bool socketOwned = false;
    if (R_SUCCEEDED(socketRc))
        socketOwned = true;
    else if (socketRc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
    {
        log_stage("ANILIST SOCKET INITIALIZE FAILED");
        result.status = "Network init failed";
        return result;
    }

    CURLcode globalRc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (globalRc != CURLE_OK)
    {
        log_stage("ANILIST CURL GLOBAL INIT FAILED");
        if (socketOwned) socketExit();
        result.status = "HTTP init failed";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        log_stage("ANILIST CURL EASY INIT FAILED");
        curl_global_cleanup();
        if (socketOwned) socketExit();
        result.status = "HTTP client init failed";
        return result;
    }

    const char* url = "https://graphql.anilist.co";
    const char* body = "{\"query\":\"query { Page(page: 1, perPage: 6) { pageInfo { hasNextPage } media(type: ANIME, sort: TRENDING_DESC) { id title { english romaji native } coverImage { large } format averageScore } } }\"}";
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(body)));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.4");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    log_stage("ANILIST HOME REQUEST");
    CURLcode requestRc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (requestRc == CURLE_OK && httpCode >= 200 && httpCode < 300 && response.find("\"media\"") != std::string::npos)
    {
        char marker[128];
        std::snprintf(marker, sizeof(marker), "ANILIST HOME REQUEST OK HTTP %ld BYTES %zu", httpCode, response.size());
        log_stage(marker);
        result.status = "AniList online - trending data received";
        result.response = response;
    }
    else
    {
        char marker[160];
        std::snprintf(marker, sizeof(marker), "ANILIST HOME REQUEST FAILED CURL %d HTTP %ld BYTES %zu", static_cast<int>(requestRc), httpCode, response.size());
        log_stage(marker);
        result.status = "AniList request failed - UI still running";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();
    log_stage("ANILIST HOME REQUEST CLEANUP COMPLETE");
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.4");
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

static size_t find_balanced_object_end(const std::string& text, size_t objectStart, size_t limit)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = objectStart; i < limit; ++i)
    {
        const char c = text[i];
        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i;
    }
    return std::string::npos;
}

struct TrendingCardData
{
    int id = 0;
    std::string title;
    std::string cover;
    std::string format;
    std::string score;
};

static std::vector<TrendingCardData> extract_trending_cards(const std::string& response)
{
    std::vector<TrendingCardData> cards;
    const size_t mediaArrayPos = response.find("\"media\"");
    if (mediaArrayPos == std::string::npos) return cards;

    size_t cursor = response.find('[', mediaArrayPos);
    if (cursor == std::string::npos) return cards;
    ++cursor;

    while (cards.size() < 6 && cursor < response.size())
    {
        const size_t objectStart = response.find('{', cursor);
        if (objectStart == std::string::npos) break;
        const size_t objectEnd = find_balanced_object_end(response, objectStart, response.size());
        if (objectEnd == std::string::npos) break;

        TrendingCardData card;
        const std::string idValue = json_value_after(response, objectStart, "id", objectEnd + 1);
        if (!idValue.empty()) card.id = std::atoi(idValue.c_str());

        const size_t titlePos = response.find("\"title\"", objectStart);
        if (titlePos != std::string::npos && titlePos < objectEnd)
        {
            const size_t titleEnd = find_balanced_object_end(response, response.find('{', titlePos), objectEnd + 1);
            if (titleEnd != std::string::npos)
            {
                card.title = json_string_after(response, titlePos, "english", titleEnd + 1);
                if (card.title.empty()) card.title = json_string_after(response, titlePos, "romaji", titleEnd + 1);
                if (card.title.empty()) card.title = json_string_after(response, titlePos, "native", titleEnd + 1);
            }
        }

        const size_t coverPos = response.find("\"coverImage\"", objectStart);
        if (coverPos != std::string::npos && coverPos < objectEnd)
        {
            const size_t coverStart = response.find('{', coverPos);
            const size_t coverEnd = coverStart == std::string::npos ? std::string::npos : find_balanced_object_end(response, coverStart, objectEnd + 1);
            if (coverEnd != std::string::npos)
                card.cover = json_string_after(response, coverStart, "large", coverEnd + 1);
        }

        card.format = json_string_after(response, objectStart, "format", objectEnd + 1);
        card.score = json_value_after(response, objectStart, "averageScore", objectEnd + 1);

        if (!card.title.empty()) cards.push_back(card);
        cursor = objectEnd + 1;
    }
    return cards;
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

    const std::vector<TrendingCardData> cards = extract_trending_cards(response);
    char marker[96];
    std::snprintf(marker, sizeof(marker), "TRENDING PARSE FOUND %zu CARDS", cards.size());
    log_stage(marker);
    if (cards.empty())
    {
        log_stage("TRENDING PARSE FOUND NO CARDS");
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

    for (size_t i = 0; i < cards.size(); ++i)
    {
        const auto& data = cards[i];
        brls::Box* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(124);
        card->setMargins(2, 3, 2, 0);
        card->setFocusable(true);
        card->setHighlightPadding(5.0f);
        card->setCornerRadius(5.0f);
        card->setFocusSound(brls::SOUND_FOCUS_CHANGE);

        bool imageAttached = false;
        if (!data.cover.empty())
        {
            char pathBuffer[128];
            std::snprintf(pathBuffer, sizeof(pathBuffer), "%s/trending_%zu.jpg", kCacheDir, i);
            const std::string imagePath = pathBuffer;
            log_stage("BEFORE TRENDING CARD IMAGE DOWNLOAD");
            if (download_image(data.cover, imagePath))
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
        title->setText(compact_title(data.title));
        title->setFontSize(14);
        title->setLineHeight(17);
        title->setMaxWidth(116);
        title->setMargins(2, 4, 2, 0);
        title->setFocusable(false);
        card->addView(title);

        std::string detail = data.format;
        if (!data.score.empty() && data.score != "null")
        {
            if (!detail.empty()) detail += "  •  ";
            detail += "Score: " + data.score;
        }
        if (!detail.empty())
        {
            brls::Label* detailLabel = new brls::Label();
            detailLabel->setText(detail);
            detailLabel->setFontSize(11);
            detailLabel->setLineHeight(14);
            detailLabel->setMaxWidth(116);
            detailLabel->setSingleLine(true);
            detailLabel->setMargins(2, 1, 2, 0);
            detailLabel->setFocusable(false);
            card->addView(detailLabel);
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
    brls::Application::setGlobalQuit(true);
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
            if (fileSize > 0 && fileSize < 128 * 1024)
            {
                std::string homeXml(static_cast<size_t>(fileSize), '\0');
                const size_t bytesRead = std::fread(homeXml.data(), 1, homeXml.size(), homeFile);
                if (bytesRead == homeXml.size())
                {
                    log_stage("HOME RESOURCE PREFLIGHT READ OK");
                    log_stage("BEFORE HOME XML STRING INFLATION");
                    brls::View* homeView = brls::View::createFromXMLString(homeXml);
                    if (homeView)
                    {
                        log_stage("HOME XML STRING RETURNED VIEW");
                        brls::Box* homeBox = dynamic_cast<brls::Box*>(homeView);
                        ApiResult api = run_api_probe();
                        if (homeBox)
                        {
                            brls::Label* status = new brls::Label();
                            status->setText(api.status);
                            status->setFontSize(14);
                            status->setMargins(0, 6, 0, 0);
                            homeBox->addView(status);
                            log_stage("API STATUS LABEL ATTACHED");
                            if (!api.response.empty())
                                render_trending(homeBox, api.response);
                        }
                        log_stage("BEFORE PUBLIC TABFRAME CONTENT SET");
                        tabFrame->setTabContent(homeView);
                        log_stage("AFTER PUBLIC TABFRAME CONTENT SET");
                        log_stage("AFTER HOME CONTENT ATTACHMENT PATH");
                    }
                    else
                        log_stage("HOME XML STRING RETURNED NULL");
                }
                else
                    log_stage("HOME RESOURCE PREFLIGHT READ FAILED");
            }
            else
                log_stage("HOME RESOURCE PREFLIGHT SIZE INVALID");
            std::fclose(homeFile);
        }
    }
    int loops = 0;
    while (brls::Application::mainLoop())
    {
        ++loops;
        if (loops <= 5)
        {
            char marker[64];
            std::snprintf(marker, sizeof(marker), "mainLoop returned true #%d", loops);
            log_stage(marker);
        }
    }
    log_stage("mainLoop returned false");
    brls::Application::exit();
    return EXIT_SUCCESS;
}
