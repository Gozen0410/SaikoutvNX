from pathlib import Path

path = Path("switch/source/main.cpp")
xml_path = Path("switch/romfs/xml/activity/api_source.xml")
source = path.read_text()

include = '#include "api_sources.hpp"\n'
if include not in source:
    first_include_end = source.find("\n", source.find("#include"))
    if first_include_end < 0:
        raise SystemExit("Could not locate main.cpp include boundary")
    source = source[:first_include_end + 1] + include + source[first_include_end + 1:]

old_name = '''static const char* api_source_name(int source)\n{\n    switch (source)\n    {\n        case 1: return "AnimePahe";\n        case 2: return "Gogoanime";\n        default: return "Miruro";\n    }\n}\n\n'''
if old_name in source:
    source = source.replace(old_name, '', 1)
source = source.replace('value >= 0 && value <= 2', 'api_source_is_valid(value)', 1)

if "static bool g_apiSourceRefreshPending" not in source:
    anchors = [
        'static bool g_homeRefreshInProgress = false;\n',
        'static bool g_refreshRequested = false;\n',
    ]
    for marker in anchors:
        if source.count(marker) == 1:
            source = source.replace(marker, marker + 'static bool g_apiSourceRefreshPending = false;\n', 1)
            break
    else:
        raise SystemExit("Could not locate refresh globals")

if "static void clear_api_cache()" not in source:
    marker = 'class ApiSourceActivity : public brls::Activity\n'
    helper = '''static void clear_api_cache()\n{\n    int removed = 0;\n    for (int i = 0; i < 6; ++i)\n    {\n        char path[128];\n        std::snprintf(path, sizeof(path), "%s/trending_%d.jpg", kCacheDir, i);\n        if (std::remove(path) == 0)\n            ++removed;\n    }\n\n    char marker[96];\n    std::snprintf(marker, sizeof(marker), "API CACHE CLEARED FILES %d", removed);\n    log_stage(marker);\n}\n\n'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate ApiSourceActivity boundary")
    source = source.replace(marker, helper + marker, 1)

start = source.find("class ApiSourceActivity : public brls::Activity")
if start < 0:
    raise SystemExit("Could not locate ApiSourceActivity")
end = source.find("static void bind_api_settings_actions", start)
if end < 0:
    raise SystemExit("Could not locate API settings binder")

xml_path.write_text('''<brls:Box width="auto" height="auto" axis="column" paddingTop="40" paddingLeft="50" paddingRight="50">\n    <brls:Label width="auto" height="auto" text="API Source" fontSize="36" />\n    <brls:Label id="api-source-current" width="auto" height="auto" text="Anime API: Miruro" marginTop="16" />\n    <brls:Label width="auto" height="auto" text="Choose the anime metadata provider" marginTop="8" />\n</brls:Box>''')

replacement = '''class ApiSourceActivity : public brls::Activity\n{\npublic:\n    brls::View* getDefaultFocus() override\n    {\n        return defaultFocus;\n    }\n\n    brls::View* createContentView() override\n    {\n        brls::View* rootView = brls::View::createFromXMLResource("activity/api_source.xml");\n        brls::Box* root = dynamic_cast<brls::Box*>(rootView);\n        if (!root)\n        {\n            delete rootView;\n            return nullptr;\n        }\n\n        brls::Label* current = dynamic_cast<brls::Label*>(root->getView("api-source-current"));\n        if (current)\n            current->setText(std::string("Anime API: ") + api_source_name(g_apiSource));\n\n        brls::Button* firstButton = nullptr;\n        for (std::size_t i = 0; i < kApiSourceCount; ++i)\n        {\n            const ApiSourceInfo& provider = kApiSources[i];\n            if (!provider.enabled)\n                continue;\n\n            brls::Button* button = new brls::Button();\n            button->setText(provider.name);\n            button->setWidth(760);\n            button->setMargins(0, firstButton ? 24 : 28, 0, 0);\n            const int sourceId = static_cast<int>(provider.id);\n            button->registerClickAction([current, sourceId](brls::View*) {\n                if (g_apiSource == sourceId)\n                {\n                    log_stage("API SOURCE SELECTION UNCHANGED");\n                    return true;\n                }\n\n                g_apiSource = sourceId;\n                save_api_source();\n                if (current)\n                    current->setText(std::string("Anime API: ") + api_source_name(g_apiSource));\n                g_apiSourceRefreshPending = true;\n                log_stage("API SOURCE CHANGED - REFRESH PENDING UNTIL HOME");\n                return true;\n            });\n            root->addView(button);\n            if (!firstButton)\n                firstButton = button;\n        }\n        defaultFocus = firstButton;\n\n        root->hide([] {}, false, 0);\n\n        // Use a non-fade return transition. Borealis keeps the previous\n        // activity on the focus stack and reveals it after the popped activity\n        // finishes hiding; a fade return can leave this dynamically-created\n        // selector hidden/alpha-zero when its root was initially hidden.\n        root->registerAction("Back", brls::BUTTON_B, [](brls::View*) {\n            log_stage("API SOURCE ACTIVITY BACK");\n            brls::Application::popActivity(brls::TransitionAnimation::SLIDE_RIGHT);\n            return true;\n        });\n\n        return root;\n    }\n\nprivate:\n    brls::View* defaultFocus = nullptr;\n};\n\n'''
source = source[:start] + replacement + source[end:]

path.write_text(source)
print("API source activity now uses a non-fade return transition to restore Settings reliably")