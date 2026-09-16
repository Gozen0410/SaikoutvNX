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

static const AnimeList& getTrending()
{
    if (!g_trendingAttempted)
    {
        g_trendingAttempted = true;
        g_trending = fetchAniListTrending();
    }
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
