from pathlib import Path

path = Path("switch/source/main.cpp")
source = path.read_text()

if "SAIKOU_ANIME_DETAILS_V1" in source:
    print("Anime details patch already installed")
    raise SystemExit(0)

include_marker = '#include <borealis/views/image.hpp>\n'
if include_marker not in source:
    raise SystemExit("Could not locate Borealis image include")
source = source.replace(include_marker, include_marker + '#include <borealis/views/scrolling_frame.hpp>\n', 1)

anchor = 'class TrendingCarouselViewport : public brls::Box\n'
pos = source.find(anchor)
if pos < 0:
    raise SystemExit("Could not locate trending carousel class")

code = r'''
// SAIKOU_ANIME_DETAILS_V1: Home card -> anime details screen.
static std::string g_selectedAnimeTitle;
static std::string g_selectedAnimeCover;

static std::string url_encode_query(const std::string& value)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(static_cast<char>(c));
        else if (c == ' ')
            out.push_back('+');
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

static bool json_read_string(const std::string& json, const std::string& key, std::string& value, size_t from = 0)
{
    const std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle, from);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) ++p;
    if (p >= json.size() || json[p] != '"') return false;
    ++p;
    std::string out;
    bool escaped = false;
    for (; p < json.size(); ++p)
    {
        const char c = json[p];
        if (escaped)
        {
            switch (c)
            {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(c); break;
            }
            escaped = false;
        }
        else if (c == '\\') escaped = true;
        else if (c == '"') { value = out; return true; }
        else out.push_back(c);
    }
    return false;
}

static bool json_read_int(const std::string& json, const std::string& key, int& value, size_t from = 0)
{
    const std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle, from);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) ++p;
    if (p >= json.size()) return false;
    value = std::atoi(json.c_str() + p);
    return true;
}

static ApiResult fetch_anime_details_by_title(const std::string& title)
{
    ApiResult result;
    Result socketRc = socketInitializeDefault();
    bool socketOwned = R_SUCCEEDED(socketRc);
    if (!socketOwned && socketRc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized))
    {
        result.status = "Network init failed";
        return result;
    }

    CURLcode globalRc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (globalRc != CURLE_OK)
    {
        if (socketOwned) socketExit();
        result.status = "HTTP init failed";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        curl_global_cleanup();
        if (socketOwned) socketExit();
        result.status = "HTTP client init failed";
        return result;
    }

    const std::string url = std::string("https://miruro.zenos.my.id/search?query=") + url_encode_query(title) + "&per_page=1";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SaikouSwitch/0.2");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    const CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (rc == CURLE_OK && httpCode >= 200 && httpCode < 300 && response.find("results") != std::string::npos)
    {
        result.status = "Details loaded";
        result.response = response;
    }
    else
    {
        char marker[128];
        std::snprintf(marker, sizeof(marker), "DETAIL REQUEST FAILED CURL %d HTTP %ld BYTES %zu", static_cast<int>(rc), httpCode, response.size());
        log_stage(marker);
        result.status = "Details request failed";
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    if (socketOwned) socketExit();
    return result;
}

class AnimeDetailsActivity : public brls::Activity
{
public:
    AnimeDetailsActivity(std::string title, std::string cover)
        : title(std::move(title)), cover(std::move(cover)) {}

    brls::View* createContentView() override
    {
        return new brls::ScrollingFrame();
    }

    void onContentAvailable() override
    {
        brls::ScrollingFrame* frame = dynamic_cast<brls::ScrollingFrame*>(getContentView());
        if (!frame) return;
        frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
        frame->setPadding(30, 45, 40, 45);

        brls::Box* content = new brls::Box(brls::Axis::COLUMN);
        content->setWidth(780);
        content->setGrow(1.0f);
        content->setShrink(0.0f);
        frame->setContentView(content);

        brls::Label* heading = new brls::Label();
        heading->setText(title);
        heading->setFontSize(32);
        heading->setSingleLine(false);
        content->addView(heading);

        brls::Box* header = new brls::Box(brls::Axis::ROW);
        header->setWidth(780);
        header->setHeight(250);
        header->setGrow(0.0f);
        header->setShrink(0.0f);
        header->setMargins(0, 20, 0, 0);
        content->addView(header);

        if (!cover.empty())
        {
            const std::string imagePath = std::string(kCacheDir) + "/details_poster.jpg";
            if (download_image(cover, imagePath))
            {
                brls::Image* poster = new brls::Image();
                poster->setDimensions(165, 245);
                poster->setScalingType(brls::ImageScalingType::CROP);
                poster->setImageFromFile(imagePath);
                poster->setFocusable(false);
                header->addView(poster);
            }
        }

        brls::Box* info = new brls::Box(brls::Axis::COLUMN);
        info->setWidth(560);
        info->setHeight(245);
        info->setGrow(0.0f);
        info->setShrink(0.0f);
        info->setMargins(20, 0, 0, 0);
        header->addView(info);

        brls::Label* status = new brls::Label();
        status->setText("Loading anime details...");
        status->setFontSize(17);
        status->setFocusable(false);
        info->addView(status);

        ApiResult result = fetch_anime_details_by_title(title);
        int episodeCount = 0;
        std::string description;
        std::string format;
        std::string animeStatus;
        if (!result.response.empty())
        {
            json_read_string(result.response, "description", description);
            json_read_string(result.response, "format", format);
            json_read_string(result.response, "status", animeStatus);
            json_read_int(result.response, "episodes", episodeCount);
        }

        status->setText(format.empty() && animeStatus.empty()
            ? result.status
            : (format + (format.empty() || animeStatus.empty() ? "" : "  •  ") + animeStatus));

        if (episodeCount > 0)
        {
            brls::Label* count = new brls::Label();
            count->setText(std::string("Episodes: ") + std::to_string(episodeCount));
            count->setFontSize(16);
            count->setMargins(0, 12, 0, 0);
            count->setFocusable(false);
            info->addView(count);
        }

        if (!description.empty())
        {
            brls::Label* desc = new brls::Label();
            desc->setText(description);
            desc->setFontSize(15);
            desc->setLineHeight(21);
            desc->setMargins(0, 14, 0, 0);
            desc->setFocusable(false);
            info->addView(desc);
        }

        brls::Label* episodesHeading = new brls::Label();
        episodesHeading->setText("Episodes");
        episodesHeading->setFontSize(26);
        episodesHeading->setMargins(0, 30, 0, 0);
        episodesHeading->setFocusable(false);
        content->addView(episodesHeading);

        if (episodeCount <= 0)
        {
            brls::Label* unavailable = new brls::Label();
            unavailable->setText("Episode information unavailable.");
            unavailable->setFontSize(16);
            unavailable->setMargins(0, 12, 0, 0);
            unavailable->setFocusable(false);
            content->addView(unavailable);
        }
        else
        {
            const int displayCount = std::min(episodeCount, 500);
            for (int i = 1; i <= displayCount; ++i)
            {
                brls::Label* episode = new brls::Label();
                episode->setText(std::string("Episode ") + std::to_string(i));
                episode->setFontSize(17);
                episode->setMargins(0, 5, 0, 0);
                episode->setFocusable(false);
                content->addView(episode);
            }
        }

        log_stage("ANIME DETAILS UI ATTACHED");
    }

private:
    std::string title;
    std::string cover;
};

'''
source = source[:pos] + code + source[pos:]

old = '''        card->registerAction("Open anime", brls::BUTTON_A, [i](brls::View*) {
            char marker[64];
            std::snprintf(marker, sizeof(marker), "TRENDING CARD SELECTED %zu", i);
            log_stage(marker);
            return true;
        });'''
new = '''        card->registerAction("Open anime", brls::BUTTON_A, [i, titles, covers](brls::View*) {
            if (i >= titles.size())
                return false;

            g_selectedAnimeTitle = titles[i];
            g_selectedAnimeCover = i < covers.size() ? covers[i] : std::string();

            char marker[128];
            std::snprintf(marker, sizeof(marker), "TRENDING CARD OPEN DETAILS %zu", i);
            log_stage(marker);

            brls::Application::pushActivity(
                new AnimeDetailsActivity(g_selectedAnimeTitle, g_selectedAnimeCover),
                brls::TransitionAnimation::SLIDE_LEFT);
            return true;
        });'''
if old not in source:
    raise SystemExit("Could not locate trending card A action")
source = source.replace(old, new, 1)

path.write_text(source)
print("Anime details activity installed with direct Home card A action")