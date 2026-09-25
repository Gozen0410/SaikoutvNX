#include <borealis.hpp>
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

static constexpr const char* kAppDir = "sdmc:/switch/SaikouTV";
static constexpr const char* kAppLogPath = "sdmc:/switch/SaikouTV/saikou_debug.log";
static constexpr const char* kSwitchLogPath = "sdmc:/switch/saikou_debug.log";
static FILE* g_logs[2] = {nullptr, nullptr};

static void log_stage(const char* stage)
{
    for (FILE* log : g_logs)
    {
        if (!log)
            continue;
        std::fprintf(log, "[Saikou] %s\n", stage);
        std::fflush(log);
    }
}

static void open_debug_logs()
{
    fsdevMountSdmc();
    mkdir("sdmc:/switch", 0777);
    mkdir(kAppDir, 0777);

    g_logs[0] = std::fopen(kAppLogPath, "w");
    g_logs[1] = std::fopen(kSwitchLogPath, "w");
    log_stage("entered main");
    log_stage(g_logs[0] ? "app-folder log opened" : "app-folder log could not be opened");
    log_stage(g_logs[1] ? "switch-folder log opened" : "switch-folder log could not be opened");
}

static void close_debug_logs()
{
    for (FILE*& log : g_logs)
    {
        if (log)
        {
            std::fclose(log);
            log = nullptr;
        }
    }
}

static brls::View* create_xml_failure_view()
{
    log_stage("creating visible XML-failure fallback");
    brls::Box* root = new brls::Box(brls::Axis::COLUMN);
    root->setWidthPercentage(100.0f);
    root->setHeightPercentage(100.0f);
    root->setPadding(48.0f);
    root->setBackgroundColor(nvgRGB(16, 20, 29));

    brls::Label* title = new brls::Label();
    title->setText("SAIKOU TV");
    title->setFontSize(36.0f);
    title->setTextColor(nvgRGB(244, 246, 250));
    root->addView(title);

    brls::Label* message = new brls::Label();
    message->setText("The Home XML could not be loaded. Check saikou_debug.log on the SD card.");
    message->setFontSize(18.0f);
    message->setTextColor(nvgRGB(174, 184, 200));
    message->setMarginTop(14.0f);
    root->addView(message);

    return root;
}

class HomeActivity : public brls::Activity
{
public:
    brls::View* createContentView() override
    {
        log_stage("HomeActivity createContentView START");
        brls::View* view = brls::View::createFromXMLResource("activity/main.xml");
        log_stage(view ? "Home XML returned a view" : "Home XML returned NULL");
        if (!view)
            view = create_xml_failure_view();
        return view;
    }

    void onContentAvailable() override
    {
        log_stage("HomeActivity onContentAvailable START");
        status = dynamic_cast<brls::Label*>(getView("home/status"));
        if (!status)
        {
            log_stage("home/status view ID missing");
            return;
        }
        log_stage("home/status view found");

        bindAction("nav/home", "Open Home", "Home is ready. Choose a card with A.");
        bindAction("nav/search", "Open Search", "Search is the next screen to build.");
        bindAction("nav/library", "Open Library", "Your library screen is the next milestone.");
        bindAction("nav/settings", "Open Settings", "Settings will be added after the Home shell.");

        bindAction("home/card/continue", "Select", "Continue Watching is ready for saved progress.");
        bindAction("home/card/frieren", "Select", "Selected preview: Frieren: Beyond Journey's End.");
        bindAction("home/card/apothecary", "Select", "Selected preview: The Apothecary Diaries.");
        bindAction("home/card/solo", "Select", "Selected preview: Solo Leveling.");
        bindAction("home/card/one-piece", "Select", "Selected preview: One Piece.");
        bindAction("home/episode/frieren", "Select", "Selected preview episode: Frieren, episode 28.");
        bindAction("home/episode/one-piece", "Select", "Selected preview episode: One Piece, episode 1106.");
        bindAction("home/episode/solo", "Select", "Selected preview episode: Solo Leveling, episode 12.");
        log_stage("HomeActivity onContentAvailable COMPLETE");
    }

private:
    brls::Label* status = nullptr;

    void bindAction(const char* viewId, const char* hint, const char* message)
    {
        brls::View* view = getView(viewId);
        if (!view)
        {
            char marker[128];
            std::snprintf(marker, sizeof(marker), "view ID missing: %s", viewId);
            log_stage(marker);
            return;
        }

        view->setFocusable(true);
        view->registerAction(hint, brls::BUTTON_A, [label = status, message = std::string(message), viewId](brls::View*) {
            label->setText(message);
            char marker[128];
            std::snprintf(marker, sizeof(marker), "A action selected: %s", viewId);
            log_stage(marker);
            return true;
        });
    }
};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    open_debug_logs();

    log_stage("before romfsInit");
    Result romfsResult = romfsInit();
    if (R_FAILED(romfsResult))
    {
        char marker[96];
        std::snprintf(marker, sizeof(marker), "romfsInit FAILED rc=0x%08X", static_cast<unsigned int>(romfsResult));
        log_stage(marker);
        close_debug_logs();
        return EXIT_FAILURE;
    }
    log_stage("romfsInit OK");

    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    log_stage("before Borealis Application::init");
    if (!brls::Application::init())
    {
        log_stage("Borealis Application::init FAILED");
        romfsExit();
        close_debug_logs();
        return EXIT_FAILURE;
    }
    log_stage("Borealis Application::init OK");

    log_stage("before createWindow");
    brls::Application::createWindow("SaikouTV NX");
    log_stage("createWindow returned");
    brls::Application::setGlobalQuit(true);

    log_stage("before pushActivity");
    brls::Application::pushActivity(new HomeActivity());
    log_stage("pushActivity returned");

    log_stage("before first mainLoop frame");
    int frameCount = 0;
    while (brls::Application::mainLoop())
    {
        ++frameCount;
        if (frameCount <= 10)
        {
            char marker[96];
            std::snprintf(marker, sizeof(marker), "mainLoop returned true #%d", frameCount);
            log_stage(marker);
        }
    }

    log_stage("mainLoop returned false");
    romfsExit();
    log_stage("romfsExit returned");
    close_debug_logs();
    return EXIT_SUCCESS;
}
