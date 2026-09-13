from pathlib import Path
import re

path = Path("switch/source/main.cpp")
source = path.read_text()

if "g_activeSidebarItem" not in source:
    marker = 'static FILE* g_log = nullptr;\n'
    addition = marker + 'static brls::View* g_activeSidebarItem = nullptr;\nstatic brls::View* g_homeContentView = nullptr;\nstatic bool g_refreshRequested = false;\nstatic bool g_homeActivationSeen = false;\n'
    if source.count(marker) != 1:
        raise SystemExit("Could not locate log global")
    source = source.replace(marker, addition, 1)
elif "g_homeContentView" not in source:
    marker = 'static bool g_refreshRequested = false;\n'
    if source.count(marker) != 1:
        raise SystemExit("Could not locate refresh global")
    source = source.replace(marker, marker + 'static brls::View* g_homeContentView = nullptr;\n', 1)

source = source.replace('            homeBox->setFocusable(true);\n', '', 1)
source = source.replace('        homeBox->setFocusable(true);\n', '', 1)
source = source.replace('        if (i == 0) brls::Application::giveFocus(card);\n', '', 1)

if '"Refresh", brls::BUTTON_X' not in source:
    marker = '    void registerCardAction(brls::View* card, size_t index)\n    {\n'
    addition = '''    void registerCardAction(brls::View* card, size_t index)
    {
        (void)index;
        if (this->getActions().empty())
        {
            this->registerAction("Refresh", brls::BUTTON_X, [](brls::View*) {
                g_refreshRequested = true;
                return true;
            });
            this->registerAction("Refresh", brls::BUTTON_Y, [](brls::View*) {
                g_refreshRequested = true;
                return true;
            });
        }
'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate TrendingCarouselViewport::registerCardAction")
    source = source.replace(marker, addition, 1)

if "SIDEBAR ACTIVE ITEM TRACKING INSTALLED" not in source:
    marker = '    if (tabFrame)\n    {\n'
    addition = '''    if (tabFrame)
    {
        brls::View* sidebarView = tabFrame->getView("brls/tab_frame/sidebar");
        tabFrame->registerAction("Back to sidebar", brls::BUTTON_B, [](brls::View*) {
            brls::View* current = brls::Application::getCurrentFocus();
            if (!current || !g_activeSidebarItem)
                return false;
            for (brls::View* node = current; node; node = node->getParent())
            {
                if (node == g_activeSidebarItem)
                    return false;
            }
            brls::Application::giveFocus(g_activeSidebarItem);
            return true;
        });
        log_stage("TABFRAME BACK-TO-ACTIVE-SIDEBAR ACTION REGISTERED");

        if (sidebarView)
        {
            brls::Box* sidebarBox = dynamic_cast<brls::Box*>(sidebarView);
            if (sidebarBox)
            {
                g_activeSidebarItem = sidebarBox->getDefaultFocus();
                auto& sidebarChildren = sidebarBox->getChildren();
                if (!sidebarChildren.empty())
                {
                    brls::Box* sidebarContent = dynamic_cast<brls::Box*>(sidebarChildren.front());
                    if (sidebarContent)
                    {
                        for (brls::View* child : sidebarContent->getChildren())
                        {
                            brls::SidebarItem* item = dynamic_cast<brls::SidebarItem*>(child);
                            if (item)
                            {
                                if (!g_homeSidebarItem)
                                    g_homeSidebarItem = item;
                                item->getActiveEvent()->subscribe([](brls::View* active) {
                                    const bool enteringHome = (active == g_homeSidebarItem);
                                    g_activeSidebarItem = active;
                                    if (enteringHome)
                                    {
                                        if (g_homeActivationSeen)
                                        {
                                            g_refreshRequested = true;
                                            log_stage("HOME REACTIVATED - REFRESH REQUESTED");
                                        }
                                        else
                                        {
                                            g_homeActivationSeen = true;
                                            log_stage("HOME INITIAL ACTIVATION - NO REFRESH");
                                        }
                                    }
                                });
                            }
                        }
                        log_stage("SIDEBAR ACTIVE ITEM TRACKING INSTALLED");
                    }
                }
            }
        }
        else
            log_stage("SIDEBAR ACTIVE ITEM TRACKING LOOKUP FAILED");
'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate TabFrame content block")
    source = source.replace(marker, addition, 1)

if "static brls::View* load_home_content_from_xml()" not in source:
    main_marker = '\nint main(int argc, char* argv[])\n'
    helper = r'''
static brls::View* load_home_content_from_xml()
{
    const char* homePath = "romfs:/xml/activity/home.xml";
    FILE* homeFile = std::fopen(homePath, "rb");
    if (!homeFile) return nullptr;
    std::fseek(homeFile, 0, SEEK_END);
    long fileSize = std::ftell(homeFile);
    std::fseek(homeFile, 0, SEEK_SET);
    if (fileSize <= 0 || fileSize > 1024 * 1024) { std::fclose(homeFile); return nullptr; }
    std::string xml(static_cast<size_t>(fileSize), '\0');
    const size_t readSize = std::fread(xml.data(), 1, xml.size(), homeFile);
    std::fclose(homeFile);
    if (readSize != xml.size()) return nullptr;
    return brls::View::createFromXMLString(xml);
}

static void refresh_home_content(brls::TabFrame* tabFrame)
{
    if (!tabFrame) return;
    log_stage("CONTROLLER REFRESH START");
    brls::View* homeContent = load_home_content_from_xml();
    if (!homeContent) { log_stage("HOME XML REFRESH FAILED"); return; }
    ApiResult api = run_api_probe();
    brls::Box* homeBox = dynamic_cast<brls::Box*>(homeContent);
    if (homeBox)
    {
        brls::Label* status = new brls::Label();
        status->setText(api.status.empty() ? "Refresh complete" : api.status);
        status->setFontSize(16);
        homeBox->addView(status);
        if (!api.response.empty()) render_trending(homeBox, api.response);
    }
    tabFrame->setTabContent(homeContent);
    g_homeContentView = homeContent;
    log_stage("AFTER REFRESH TABFRAME CONTENT SET");
}
'''
    if source.count(main_marker) != 1:
        raise SystemExit("Could not locate main() boundary")
    source = source.replace(main_marker, helper + main_marker, 1)

source = source.replace('brls::Application::setGlobalQuit(false);', 'brls::Application::setGlobalQuit(true);', 1)

if "HOME FOCUS REFRESH CHECK INSTALLED" not in source:
    marker = '    while (brls::Application::mainLoop())\n    {\n        ++loopCount;\n'
    replacement = '''    while (brls::Application::mainLoop())
    {
        if (g_apiSourceRefreshPending && g_activeSidebarItem == g_homeSidebarItem)
        {
            g_apiSourceRefreshPending = false;
            g_refreshRequested = true;
            log_stage("HOME ACTIVE - CONSUMING API SOURCE REFRESH");
        }
        if (g_refreshRequested && g_activeSidebarItem == g_homeSidebarItem)
        {
            g_refreshRequested = false;
            refresh_home_content(tabFrame);
        }

        ++loopCount;
'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate main loop")
    source = source.replace(marker, replacement, 1)
    source = source.replace('    log_stage("AFTER HOME CONTENT ATTACHMENT PATH");\n', '    g_homeContentView = tabFrame ? tabFrame->getTabContent() : nullptr;\n    log_stage("HOME FOCUS REFRESH CHECK INSTALLED");\n    log_stage("AFTER HOME CONTENT ATTACHMENT PATH");\n', 1)

path.write_text(source)
print("Controller now refreshes Home automatically when the Home sidebar tab is reactivated")
