#include "anilist.hpp"
#include "home.hpp"

#include <borealis.hpp>
#include <sys/stat.h>
#include <cstdio>

namespace
{
static AnimeList g_trending;
static bool g_trendingAttempted = false;

static void ensureAppDirs()
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SaikouTV", 0777);
    mkdir("sdmc:/switch/SaikouTV/cache", 0777);
}

static void logStage(const char* message)
{
    FILE* log = std::fopen("sdmc:/switch/SaikouTV/saikou_debug.log", "a");
    if (!log) return;
    std::fprintf(log, "[Saikou] %s\n", message);
    std::fflush(log);
    std::fclose(log);
}

static const AnimeList& getTrending()
{
    if (!g_trendingAttempted)
    {
        g_trendingAttempted = true;
        logStage("HOME TRENDING FIRST LOAD - REQUESTING ANILIST");
        g_trending = fetchAniListTrending();
        if (g_trending.size() == 6)
            logStage("HOME TRENDING SESSION CACHE STORED 6 CARDS");
        else
            logStage("HOME TRENDING LOAD FAILED - SESSION CACHE EMPTY");
    }
    else
        logStage("HOME TRENDING SESSION CACHE HIT - SKIPPING ANILIST REQUEST");
    return g_trending;
}

class RootActivity : public brls::Activity
{
public:
    brls::View* getDefaultFocus() override
    {
        return brls::Activity::getDefaultFocus();
    }

    brls::View* createContentView() override
    {
        brls::TabFrame* tabs = new brls::TabFrame();

        tabs->addTab("Home", [] {
            return createHomeView(getTrending());
        });

        tabs->addTab("Search", [] {
            return createPlaceholderView("Search Anime", "Search and browse AniList will be added after Home is stable.");
        });

        tabs->addTab("Library", [] {
            return createPlaceholderView("Library", "Favorites and watch history will be added later.");
        });

        tabs->addSeparator();

        tabs->addTab("Settings", [] {
            return createPlaceholderView("Settings", "Application settings and cache controls will be added later.");
        });

        return tabs;
    }
};
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    ensureAppDirs();

    if (!brls::Application::init())
        return EXIT_FAILURE;

    brls::Application::getPlatform()->exitToHomeMode(true);
    brls::Application::createWindow("Saikou Switch");
    brls::Application::pushActivity(new RootActivity());

    while (brls::Application::mainLoop())
    {
    }

    return EXIT_SUCCESS;
}
