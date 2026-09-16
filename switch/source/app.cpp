#include "anilist.hpp"
#include "home.hpp"

#include <borealis.hpp>
#include <switch.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static void logResult(const char* prefix, Result rc)
{
    char marker[128];
    std::snprintf(marker, sizeof(marker), "%s RC=0x%08X MODULE=%u DESCRIPTION=%u",
                  prefix,
                  static_cast<unsigned int>(rc),
                  static_cast<unsigned int>(R_MODULE(rc)),
                  static_cast<unsigned int>(R_DESCRIPTION(rc)));
    logStage(marker);
}

static void logFileProbe(const char* path)
{
    FILE* file = std::fopen(path, "rb");
    if (!file)
    {
        logStage("ROMFS PREFLIGHT OPEN FAILED");
        return;
    }

    logStage("ROMFS PREFLIGHT OPEN OK");
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        logStage("ROMFS PREFLIGHT SEEK FAILED");
        return;
    }

    long size = std::ftell(file);
    std::rewind(file);
    char marker[128];
    std::snprintf(marker, sizeof(marker), "ROMFS PREFLIGHT SIZE %ld", size);
    logStage(marker);

    if (size <= 0 || size > 1024 * 1024)
    {
        std::fclose(file);
        logStage("ROMFS PREFLIGHT INVALID SIZE");
        return;
    }

    char firstByte = '\0';
    size_t read = std::fread(&firstByte, 1, 1, file);
    std::fclose(file);
    logStage(read == 1 ? "ROMFS PREFLIGHT READ OK" : "ROMFS PREFLIGHT READ FAILED");
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

    fsdevMountSdmc();
    ensureAppDirs();

    g_log = std::fopen(kLogPath, "w");
    logStage("entered main");

    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    logStage("logger configured");

    logStage("BEFORE ROMFS INIT");
    Result romfsRc = romfsInit();
    logResult("ROMFS INIT RESULT", romfsRc);
    logStage(R_SUCCEEDED(romfsRc) ? "ROMFS INIT OK" : "ROMFS INIT FAILED");

    // Keep the Saikou log open throughout Borealis initialization so a crash keeps
    // a continuous application-side timeline in addition to Borealis/Switch logs.
    logStage("BEFORE ROMFS PREFLIGHT");
    logFileProbe("romfs:/xml/activity/main.xml");
    logStage("AFTER ROMFS PREFLIGHT");

    logStage("BEFORE APPLICATION INIT");
    const bool appInitOk = brls::Application::init();
    logStage(appInitOk ? "APPLICATION INIT OK" : "APPLICATION INIT FAILED");
    if (!appInitOk)
    {
        logStage("EXITING AFTER APPLICATION INIT FAILURE");
        if (g_log)
        {
            std::fclose(g_log);
            g_log = nullptr;
        }
        return EXIT_FAILURE;
    }

    logStage("BEFORE CREATE WINDOW");
    brls::Application::createWindow("Saikou Switch");
    logStage("AFTER CREATE WINDOW");

    brls::Application::setGlobalQuit(false);

    logStage("BEFORE ROOT ACTIVITY CONSTRUCTION");
    RootActivity* root = new RootActivity();
    logStage("AFTER ROOT ACTIVITY CONSTRUCTION");

    logStage("BEFORE PUSH ROOT ACTIVITY");
    brls::Application::pushActivity(root);
    logStage("AFTER PUSH ROOT ACTIVITY");

    logStage("ENTERING MAIN LOOP");
    while (brls::Application::mainLoop())
    {
    }

    logStage("MAIN LOOP RETURNED FALSE");
    if (g_log)
    {
        std::fclose(g_log);
        g_log = nullptr;
    }

    return EXIT_SUCCESS;
}
