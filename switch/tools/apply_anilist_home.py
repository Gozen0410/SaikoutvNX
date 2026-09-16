from pathlib import Path
import re

main = Path("switch/source/main.cpp")
source = main.read_text()

if "https://graphql.anilist.co" not in source:
    raise SystemExit("AniList endpoint is missing from main.cpp")

parser_start = source.find("static std::string json_string_after")
compact_marker = "static std::string compact_title"
parser_end = source.find(compact_marker, parser_start)
if parser_start < 0 or parser_end < 0 or parser_end <= parser_start:
    raise SystemExit("Could not locate AniList parser block")

parser_block = r'''static std::string json_string_in_range(const std::string& text, size_t from, const char* key, size_t limit)
{
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = text.find(needle, from);
    if (keyPos == std::string::npos || keyPos >= limit) return std::string();

    const size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos || colon >= limit) return std::string();

    size_t value = colon + 1;
    while (value < limit && (text[value] == ' ' || text[value] == '\n' || text[value] == '\r' || text[value] == '\t')) ++value;
    if (value >= limit || text[value] != '\"') return std::string();

    std::string result;
    bool escaped = false;
    for (size_t i = value + 1; i < limit; ++i)
    {
        const char c = text[i];
        if (escaped)
        {
            if (c == '"' || c == '\\' || c == '/') result.push_back(c);
            else if (c == 'n' || c == 'r' || c == 't') result.push_back(' ');
            else result.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\')
        {
            escaped = true;
            continue;
        }
        if (c == '"') break;
        result.push_back(c);
    }
    return result;
}

static std::string json_scalar_in_range(const std::string& text, size_t from, const char* key, size_t limit)
{
    const std::string needle = std::string("\"") + key + "\"";
    const size_t keyPos = text.find(needle, from);
    if (keyPos == std::string::npos || keyPos >= limit) return std::string();
    const size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos || colon >= limit) return std::string();

    size_t value = colon + 1;
    while (value < limit && (text[value] == ' ' || text[value] == '\n' || text[value] == '\r' || text[value] == '\t')) ++value;
    size_t end = value;
    while (end < limit && text[end] != ',' && text[end] != '}' && text[end] != ']') ++end;
    while (end > value && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == '\t')) --end;
    return text.substr(value, end - value);
}

static size_t find_balanced_object_end(const std::string& text, size_t objectStart, size_t limit)
{
    if (objectStart == std::string::npos || objectStart >= limit || text[objectStart] != '{') return std::string::npos;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = objectStart; i < limit; ++i)
    {
        const char c = text[i];
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}' && depth > 0)
        {
            --depth;
            if (depth == 0) return i;
        }
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

static std::vector<TrendingCardData> g_trendingCache;
static bool g_trendingCacheLoaded = false;

static std::vector<TrendingCardData> extract_trending_cards(const std::string& response)
{
    std::vector<TrendingCardData> cards;
    const size_t mediaKey = response.find("\"media\"");
    if (mediaKey == std::string::npos)
    {
        log_stage("TRENDING PARSE: MEDIA ARRAY KEY NOT FOUND");
        return cards;
    }

    const size_t arrayStart = response.find('[', mediaKey);
    if (arrayStart == std::string::npos)
    {
        log_stage("TRENDING PARSE: MEDIA ARRAY START NOT FOUND");
        return cards;
    }

    size_t cursor = arrayStart + 1;
    while (cards.size() < 6 && cursor < response.size())
    {
        const size_t objectStart = response.find('{', cursor);
        if (objectStart == std::string::npos) break;
        const size_t objectEnd = find_balanced_object_end(response, objectStart, response.size());
        if (objectEnd == std::string::npos) break;

        TrendingCardData card;
        const std::string id = json_scalar_in_range(response, objectStart, "id", objectEnd + 1);
        if (!id.empty()) card.id = std::atoi(id.c_str());

        const size_t titleKey = response.find("\"title\"", objectStart);
        if (titleKey != std::string::npos && titleKey < objectEnd)
        {
            const size_t titleStart = response.find('{', titleKey);
            const size_t titleEnd = find_balanced_object_end(response, titleStart, objectEnd + 1);
            if (titleStart != std::string::npos && titleEnd != std::string::npos && titleStart < titleEnd)
            {
                card.title = json_string_in_range(response, titleStart, "english", titleEnd + 1);
                if (card.title.empty()) card.title = json_string_in_range(response, titleStart, "romaji", titleEnd + 1);
                if (card.title.empty()) card.title = json_string_in_range(response, titleStart, "native", titleEnd + 1);
            }
        }

        const size_t coverKey = response.find("\"coverImage\"", objectStart);
        if (coverKey != std::string::npos && coverKey < objectEnd)
        {
            const size_t coverStart = response.find('{', coverKey);
            const size_t coverEnd = find_balanced_object_end(response, coverStart, objectEnd + 1);
            if (coverStart != std::string::npos && coverEnd != std::string::npos)
                card.cover = json_string_in_range(response, coverStart, "large", coverEnd + 1);
        }

        card.format = json_string_in_range(response, objectStart, "format", objectEnd + 1);
        card.score = json_scalar_in_range(response, objectStart, "averageScore", objectEnd + 1);

        if (!card.title.empty()) cards.push_back(card);
        cursor = objectEnd + 1;
    }

    char marker[96];
    std::snprintf(marker, sizeof(marker), "TRENDING PARSE FOUND %zu CARDS", cards.size());
    log_stage(marker);
    return cards;
}

'''
source = source[:parser_start] + parser_block + source[parser_end:]

render_start = source.find("static void render_trending")
main_marker = "int main(int argc, char* argv[])")
if render_start < 0 or main_marker < 0 or main_marker <= render_start:
    raise SystemExit("Could not locate trending render/main boundary")

render_block = r'''static void render_trending(brls::Box* homeBox, const std::vector<TrendingCardData>& cards)
{
    if (!homeBox || cards.empty()) return;

    log_stage("BEFORE TRENDING UI RENDER");
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

    const size_t cardCount = std::min<size_t>(cards.size(), 6);
    for (size_t i = 0; i < cardCount; ++i)
    {
        const TrendingCardData& data = cards[i];
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
            char pathBuffer[160];
            std::snprintf(pathBuffer, sizeof(pathBuffer), "%s/trending_%d.jpg", kCacheDir, data.id);
            const std::string imagePath = pathBuffer;

            struct stat imageStat{};
            if (stat(imagePath.c_str(), &imageStat) == 0 && imageStat.st_size > 0)
            {
                log_stage("TRENDING CARD IMAGE CACHE HIT");
            }
            else
            {
                log_stage("BEFORE TRENDING CARD IMAGE DOWNLOAD");
                if (!download_image(data.cover, imagePath))
                    std::remove(imagePath.c_str());
            }

            if (stat(imagePath.c_str(), &imageStat) == 0 && imageStat.st_size > 0)
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

        if (!data.format.empty() || (!data.score.empty() && data.score != "null"))
        {
            std::string detail = data.format;
            if (!data.score.empty() && data.score != "null")
            {
                if (!detail.empty()) detail += "  •  ";
                detail += "Score: " + data.score;
            }
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

'''
source = source[:render_start] + render_block + source[main_marker:]

old_api = '''                        ApiResult api = run_api_probe();
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
'''
new_api = '''                        ApiResult api;
                        if (!g_trendingCacheLoaded)
                        {
                            api = run_api_probe();
                            if (!api.response.empty())
                            {
                                std::vector<TrendingCardData> parsed = extract_trending_cards(api.response);
                                if (parsed.size() == 6)
                                {
                                    g_trendingCache = parsed;
                                    g_trendingCacheLoaded = true;
                                    log_stage("TRENDING SESSION CACHE STORED 6 CARDS");
                                }
                                else
                                    log_stage("TRENDING SESSION CACHE NOT STORED - EXPECTED 6 CARDS");
                            }
                        }
                        else
                        {
                            api.status = "AniList cached - 6 trending cards";
                            log_stage("TRENDING SESSION CACHE HIT - SKIPPING ANILIST REQUEST");
                        }

                        if (homeBox)
                        {
                            brls::Label* status = new brls::Label();
                            status->setText(g_trendingCacheLoaded ? "AniList online - 6 trending cards" : api.status);
                            status->setFontSize(14);
                            status->setMargins(0, 6, 0, 0);
                            homeBox->addView(status);
                            log_stage("API STATUS LABEL ATTACHED");
                            if (g_trendingCacheLoaded)
                                render_trending(homeBox, g_trendingCache);
                        }
'''
if old_api not in source:
    raise SystemExit("Could not locate Home AniList render block")
source = source.replace(old_api, new_api, 1)

factory_marker = '''    if (tabFrame)
    {
        const char* homePath = "romfs:/xml/activity/home.xml";'''
factory_replacement = '''    if (tabFrame)
    {
        tabFrame->setTabContentFactory([](const std::string& label, brls::TabViewCreator creator) {
            brls::View* view = creator();
            if (label == "Home" && view && g_trendingCacheLoaded)
            {
                brls::Box* box = dynamic_cast<brls::Box*>(view);
                if (box)
                {
                    render_trending(box, g_trendingCache);
                    log_stage("HOME TAB RESTORED FROM SESSION CACHE");
                }
            }
            return view;
        });
        log_stage("HOME TAB CACHE FACTORY INSTALLED");

        const char* homePath = "romfs:/xml/activity/home.xml";'''
if factory_marker not in source:
    raise SystemExit("Could not locate TabFrame Home setup")
source = source.replace(factory_marker, factory_replacement, 1)

source = source.replace('    brls::Application::setGlobalQuit(true);\n', '    brls::Application::setGlobalQuit(false);\n', 1)
source = source.replace('    brls::Application::exit();\n', '', 1)

main.write_text(source)
print("AniList Home now has robust six-card parsing, session caching, disk image reuse, and Home tab restoration")
