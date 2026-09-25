#include <borealis.hpp>
#include <switch.h>
#include <cstdlib>
#include <string>

class HomeActivity : public brls::Activity
{
public:
    brls::View* createContentView() override
    {
        return brls::View::createFromXMLResource("activity/main.xml");
    }

    void onContentAvailable() override
    {
        status = dynamic_cast<brls::Label*>(getView("home/status"));
        if (!status)
            return;

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
    }

private:
    brls::Label* status = nullptr;

    void bindAction(const char* viewId, const char* hint, const char* message)
    {
        brls::View* view = getView(viewId);
        if (!view)
            return;

        view->setFocusable(true);
        view->registerAction(hint, brls::BUTTON_A, [label = status, message = std::string(message)](brls::View*) {
            label->setText(message);
            return true;
        });
    }
};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    Result romfsResult = romfsInit();
    if (R_FAILED(romfsResult))
        return EXIT_FAILURE;

    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    if (!brls::Application::init())
    {
        romfsExit();
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("SaikouTV NX");
    brls::Application::setGlobalQuit(true);
    brls::Application::pushActivity(new HomeActivity());

    while (brls::Application::mainLoop())
    {
    }

    romfsExit();
    return EXIT_SUCCESS;
}
