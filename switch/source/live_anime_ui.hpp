#pragma once

#include <borealis.hpp>
#include <curl/curl.h>
#include <switch/applets/swkbd.h>
#include <switch.h>
#include "api_sources.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

static void log_stage(const char* stage);

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
static constexpr const char* kSettingsPath = "sdmc:/switch/SaikouTV/settings.ini";
static constexpr const char* kCacheDir = "sdmc:/switch/SaikouTV/cache";

static bool ensure_network_ready()
{
    static std::once_flag once;
    static Result socketResult = MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    static bool socketOwned = false;
    std::call_once(once, [] {
        socketResult = socketInitializeDefault();
        socketOwned = R_SUCCEEDED(socketResult);
    });
    return R_SUCCEEDED(socketResult) ||
        socketResult == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
}

static bool ensure_curl_ready()
{
    static std::once_flag once;
    static CURLcode result = CURLE_FAILED_INIT;
    std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return result == CURLE_OK;
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
    std::string& response, long timeoutSeconds = 10)
{
    if (!ensure_network_ready() || !ensure_curl_ready())
        return false;

    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    if (postBody)
        headers = curl_slist_append(headers, "Content-Type: application/json");

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
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return requestResult == CURLE_OK && httpCode >= 200 && httpCode < 300;
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

static std::vector<SaikouAnime> parse_anilist_media(const std::string& response)
{
    std::vector<SaikouAnime> media;
    size_t arrayAt = response.find("\"media\"");
    if (arrayAt == std::string::npos) return media;
    arrayAt = response.find('[', arrayAt);
    if (arrayAt == std::string::npos) return media;

    for (size_t p = arrayAt + 1; p < response.size() && media.size() < 24;)
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

static std::vector<SaikouAnime> fetch_anilist_media(const std::string& search, int pageSize, std::string& status)
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
        ",\"variables\":{\"page\":1,\"perPage\":" + std::to_string(pageSize) +
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
    std::snprintf(marker, sizeof(marker), "ANILIST MEDIA FOUND %zu ITEMS", result.size());
    log_stage(marker);
    status = result.empty() ? "AniList returned no anime." : "Live AniList data";
    return result;
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

class AnimeDetailsActivity : public brls::Activity
{
public:
    explicit AnimeDetailsActivity(SaikouAnime anime) : anime(std::move(anime)) {}

    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));
        return root;
    }

    void onContentAvailable() override
    {
        brls::Box* root = dynamic_cast<brls::Box*>(getContentView());
        if (!root) return;

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

        const std::string bannerPath = cached_banner_path(anime.id);
        if (!anime.bannerUrl.empty() && download_image(anime.bannerUrl, bannerPath))
        {
            brls::Image* banner = new brls::Image();
            banner->setDimensions(1160.0f, 150.0f);
            banner->setMargins(0, 12, 0, 0);
            banner->setScalingType(brls::ImageScalingType::FILL);
            banner->setImageFromFile(bannerPath);
            banner->setFocusable(false);
            root->addView(banner);
        }

        brls::Box* summary = new brls::Box(brls::Axis::ROW);
        summary->setHeight(190.0f);
        summary->setMargins(0, 12, 0, 0);
        if (anime.posterPath.empty())
        {
            anime.posterPath = cached_cover_path(anime.id);
            download_image(anime.coverUrl, anime.posterPath);
        }
        struct stat posterStat;
        if (stat(anime.posterPath.c_str(), &posterStat) == 0)
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

private:
    SaikouAnime anime;
    brls::Label* m_sourceStatus = nullptr;

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
    brls::Application::pushActivity(
        new AnimeDetailsActivity(anime),
        brls::TransitionAnimation::SLIDE_LEFT);
}

static std::string compact_card_title(const std::string& title)
{
    if (title.size() <= 34) return title;
    size_t cut = 34;
    while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) --cut;
    return title.substr(0, cut) + "...";
}

static brls::Box* make_anime_card(const SaikouAnime& anime)
{
    brls::Box* card = new brls::Box(brls::Axis::COLUMN);
    card->setWidth(184.0f);
    card->setHeight(244.0f);
    card->setPadding(6.0f);
    card->setMargins(3, 7, 3, 0);
    card->setBackgroundColor(nvgRGB(27, 34, 48));
    card->setBorderColor(nvgRGB(48, 57, 74));
    card->setBorderThickness(1.0f);
    card->setCornerRadius(9.0f);
    card->setFocusable(true);
    card->setHighlightPadding(4.0f);

    const std::string imagePath = anime.posterPath.empty() ? cached_cover_path(anime.id) : anime.posterPath;
    struct stat st;
    if (stat(imagePath.c_str(), &st) == 0 && st.st_size > 256)
    {
        brls::Image* poster = new brls::Image();
        poster->setDimensions(168.0f, 168.0f);
        poster->setScalingType(brls::ImageScalingType::FIT);
        poster->setImageFromFile(imagePath);
        poster->setFocusable(false);
        card->addView(poster);
    }
    else
    {
        brls::Box* placeholder = new brls::Box();
        placeholder->setDimensions(168.0f, 168.0f);
        placeholder->setBackgroundColor(nvgRGB(35, 45, 62));
        placeholder->setFocusable(false);
        card->addView(placeholder);
    }

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

    card->registerAction("Open anime details", brls::BUTTON_A, [anime](brls::View*) {
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

class SearchActivity : public brls::Activity
{
public:
    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("SEARCH ANIME");
        heading->setFontSize(28.0f);
        root->addView(heading);

        brls::Box* searchButton = new brls::Box(brls::Axis::ROW);
        searchButton->setWidth(500.0f);
        searchButton->setHeight(52.0f);
        searchButton->setMargins(0, 15, 0, 0);
        searchButton->setPadding(12.0f);
        searchButton->setBackgroundColor(nvgRGB(27, 34, 48));
        searchButton->setBorderColor(nvgRGB(48, 57, 74));
        searchButton->setBorderThickness(1.0f);
        searchButton->setCornerRadius(8.0f);
        searchButton->setFocusable(true);

        m_queryLabel = new brls::Label();
        m_queryLabel->setText("Press A to enter a title with the Switch keyboard");
        m_queryLabel->setFontSize(16.0f);
        searchButton->addView(m_queryLabel);
        searchButton->registerAction("Enter search query", brls::BUTTON_A, [this](brls::View*) {
            run_search();
            return true;
        });
        root->addView(searchButton);

        m_status = new brls::Label();
        m_status->setText("Search uses live AniList anime data.");
        m_status->setFontSize(15.0f);
        m_status->setTextColor(nvgRGB(174, 184, 200));
        m_status->setMargins(0, 12, 0, 0);
        root->addView(m_status);

        m_results = new brls::Box(brls::Axis::COLUMN);
        m_results->setWidthPercentage(100.0f);
        m_results->setMargins(0, 12, 0, 0);
        root->addView(m_results);
        return root;
    }

private:
    brls::Label* m_queryLabel = nullptr;
    brls::Label* m_status = nullptr;
    brls::Box* m_results = nullptr;

    void run_search()
    {
        SwkbdConfig keyboard{};
        Result rc = swkbdCreate(&keyboard, 0);
        if (R_FAILED(rc))
        {
            if (m_status) m_status->setText("Could not open the Switch keyboard.");
            return;
        }
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, "Search anime on AniList");
        swkbdConfigSetGuideText(&keyboard, "Type an anime title");
        char query[256] = {};
        rc = swkbdShow(&keyboard, query, sizeof(query));
        swkbdClose(&keyboard);
        if (R_FAILED(rc) || query[0] == '\0')
            return;

        if (m_queryLabel) m_queryLabel->setText(query);
        if (m_status) m_status->setText("Searching AniList...");
        clear_box(m_results);

        std::string status;
        std::vector<SaikouAnime> results = fetch_anilist_media(query, 12, status);
        for (SaikouAnime& anime : results)
        {
            anime.posterPath = cached_cover_path(anime.id);
            if (!download_image(anime.coverUrl, anime.posterPath))
                anime.posterPath.clear();
        }
        render_anime_cards(m_results, results);
        if (m_status)
            m_status->setText(status + " — press A on a title for details.");
    }
};

class LibraryActivity : public brls::Activity
{
public:
    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(32.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("ANILIST LIBRARY");
        heading->setFontSize(28.0f);
        root->addView(heading);

        brls::Label* note = new brls::Label();
        note->setText("Your Planning, Watching, Completed, Paused, Dropped, and Rewatching lists will appear here after account linking.");
        note->setFontSize(17.0f);
        note->setLineHeight(23.0f);
        note->setMargins(0, 12, 0, 0);
        note->setTextColor(nvgRGB(174, 184, 200));
        root->addView(note);

        brls::Label* status = new brls::Label();
        status->setText("AniList phone pairing is the next account integration.");
        status->setFontSize(16.0f);
        status->setMargins(0, 20, 0, 0);
        root->addView(status);
        return root;
    }
};

class SettingsActivity : public brls::Activity
{
public:
    brls::View* createContentView() override
    {
        brls::Box* root = new brls::Box(brls::Axis::COLUMN);
        root->setWidthPercentage(100.0f);
        root->setHeightPercentage(100.0f);
        root->setPadding(30.0f);
        root->setBackgroundColor(nvgRGB(16, 20, 29));

        brls::Label* heading = new brls::Label();
        heading->setText("SETTINGS");
        heading->setFontSize(28.0f);
        root->addView(heading);

        brls::Label* account = new brls::Label();
        account->setText("AniList account pairing with the phone app is being built.");
        account->setFontSize(16.0f);
        account->setTextColor(nvgRGB(174, 184, 200));
        account->setMargins(0, 7, 0, 0);
        root->addView(account);

        brls::Label* sourceHeading = new brls::Label();
        sourceHeading->setText("EPISODE SOURCES");
        sourceHeading->setFontSize(20.0f);
        sourceHeading->setMargins(0, 18, 0, 6);
        root->addView(sourceHeading);

        for (size_t i = 0; i < kApiSourceCount; ++i)
            root->addView(make_toggle(i));
        return root;
    }

private:
    brls::Label* make_toggle(size_t index)
    {
        brls::Label* toggle = new brls::Label();
        toggle->setText(toggle_text(index));
        toggle->setFontSize(17.0f);
        toggle->setMargins(0, 7, 0, 0);
        toggle->setBackgroundColor(nvgRGB(27, 34, 48));
        toggle->setFocusable(true);
        toggle->registerAction("Toggle source API", brls::BUTTON_A, [this, toggle, index](brls::View*) {
            g_providerEnabled[index] = !g_providerEnabled[index];
            save_source_settings();
            toggle->setText(toggle_text(index));
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

class HomeActivity : public brls::Activity
{
public:
    ~HomeActivity() override
    {
        if (m_loader.joinable())
            m_loader.join();
    }

    brls::View* createContentView() override
    {
        log_stage("HomeActivity createContentView START");
        brls::View* view = brls::View::createFromXMLResource("activity/main.xml");
        log_stage(view ? "Home XML returned a view" : "Home XML returned NULL");
        if (!view)
            return create_xml_failure_view();

        view->setDimensions(brls::Application::contentWidth, brls::Application::contentHeight);
        view->setBackgroundColor(nvgRGB(16, 20, 29));
        g_homeView = view;
        return view;
    }

    void onContentAvailable() override
    {
        m_status = dynamic_cast<brls::Label*>(getView("home/status"));
        m_cards = dynamic_cast<brls::Box*>(getView("home/trending/cards"));
        connect_navigation("nav/search", "Open Search", [] {
            brls::Application::pushActivity(new SearchActivity(), brls::TransitionAnimation::SLIDE_LEFT);
        });
        connect_navigation("nav/library", "Open Library", [] {
            brls::Application::pushActivity(new LibraryActivity(), brls::TransitionAnimation::SLIDE_LEFT);
        });
        connect_navigation("nav/settings", "Open Settings", [] {
            brls::Application::pushActivity(new SettingsActivity(), brls::TransitionAnimation::SLIDE_LEFT);
        });
        connect_navigation("nav/home", "Home", [] {});
        if (m_status) m_status->setText("Loading live AniList trending titles...");
        if (!m_loader.joinable())
        {
            m_loader = std::thread([this] {
                m_items = fetch_anilist_media("", 6, m_loadStatus);
                for (SaikouAnime& anime : m_items)
                {
                    anime.posterPath = cached_cover_path(anime.id);
                    if (!download_image(anime.coverUrl, anime.posterPath))
                        anime.posterPath.clear();
                }
                m_ready.store(true, std::memory_order_release);
            });
        }
    }

    void tick()
    {
        if (m_attached || !m_ready.load(std::memory_order_acquire))
            return;
        if (m_loader.joinable())
            m_loader.join();

        render_anime_cards(m_cards, m_items);
        if (m_status)
            m_status->setText(m_loadStatus + " — select a poster for details.");
        m_attached = true;
        log_stage("ANIList HOME CARDS ATTACHED");
    }

private:
    std::thread m_loader;
    std::atomic<bool> m_ready{ false };
    bool m_attached = false;
    std::vector<SaikouAnime> m_items;
    std::string m_loadStatus;
    brls::Label* m_status = nullptr;
    brls::Box* m_cards = nullptr;

    void connect_navigation(const char* id, const char* name, std::function<void()> callback)
    {
        brls::View* view = getView(id);
        if (!view) return;
        view->registerAction(name, brls::BUTTON_A, [callback](brls::View*) {
            callback();
            return true;
        });
    }
};
