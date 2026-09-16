#include "anilist.hpp"
#include "home.hpp"

#include <borealis.hpp>
#include <switch.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>

namespace
{
static AnimeList g_trending;
static bool g_trendingAttempted = false;
static FILE* g_log = nullptr;
static constexpr const char* kAppDir = "sdmc:/switch/SaikouTV";
static constexpr const char* kCacheDir = "sdmc:/switch/SaikouTV/cache";
static constexpr const char* kLogPath = "sdmc:/switch/SaikouTV/saikou_debug.log";

static void ensureAppDirs()
{
    mkdir("sdmc:/switch", 0777);
    mkdir(kAppDir, 0777);
    mkdir(kCacheDir, 0777);
}

static void logStage(const char* message)
{
    if (!g_log)
        g_log = std::fopen(kLogPath, "a");
    if (!g_log)
        return;
    std::fprintf(g_log, "[Saikou] %s\n", message);
    std::fflush(g_log);
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
    {
        logStage("HOME TRENDING SESSION CACHE HIT - SKIPPING ANILIST REQUEST");
    }
    return g_trending;
}

class RootActivity : public brls::Activity
{
public:
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

    // Keep the exact platform/bootstrap ordering proven by the working #50 build.
    fsdevMountSdmc();
    ensureAppDirs();

    g_log = std::fopen(kLogPath, "w");
    logStage("entered main");

    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    logStage("logger configured");

    Result romfsRc = romfsInit();
    logStage(R_SUCCEEDED(romfsRc) ? "romfsInit OK" : "romfsInit FAILED");

    // Borealis performs its own initialization and resource access after this point.
    logStage("closing Saikou log before Borealis init");
    if (g_log)
    {
        std::fclose(g_log);
        g_log = nullptr;
    }

    if (!brls::Application::init())
        return EXIT_FAILURE;
    logStage("Application::init OK");

    brls::Application::createWindow("Saikou Switch");
    logStage("Borealis window created");

    // Preserve the working baseline's global-quit behavior until we have a dedicated
    // lifecycle test for the layered entry point.
    brls::Application::setGlobalQuit(false);

    brls::Application::pushActivity(new RootActivity());
    logStage("RootActivity pushed");

    while (brls::Application::mainLoop())
    {
    }

    logStage("mainLoop returned false");
    if (g_log)
    {
        std::fclose(g_log);
        g_log = nullptr;
    }

    return EXIT_SUCCESS;
}
