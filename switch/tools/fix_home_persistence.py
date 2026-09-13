from pathlib import Path
import re

path = Path("switch/source/main.cpp")
source = path.read_text()

# Controller-controls installs these globals first.
if "static brls::View* g_homeSidebarItem" not in source:
    marker = 'static bool g_refreshRequested = false;\n'
    addition = marker + 'static brls::View* g_homeSidebarItem = nullptr;\nstatic bool g_homeContentInstalled = false;\nstatic bool g_homeRefreshInProgress = false;\n'
    if source.count(marker) != 1:
        raise SystemExit("Could not locate controller refresh global")
    source = source.replace(marker, addition, 1)
elif "g_homeContentView" not in source:
    marker = 'static bool g_refreshRequested = false;\n'
    if source.count(marker) != 1:
        raise SystemExit("Could not locate refresh global")
    source = source.replace(marker, marker + 'static brls::View* g_homeContentView = nullptr;\n', 1)

source = source.replace('            homeBox->setFocusable(true);\n', '', 1)
source = source.replace('        homeBox->setFocusable(true);\n', '', 1)
source = source.replace('        if (i == 0) brls::Application::giveFocus(card);\n', '', 1)

# Update the sidebar active-event subscription without depending on the exact
# formatting of the controller patch. A pending API-source refresh is consumed
# only when Home actually becomes the active sidebar item.
if "g_apiSourceRefreshPending = false;" not in source:
    pattern = re.compile(
        r'item->getActiveEvent\(\)->subscribe\(\[\]\(brls::View\* active\) \{\s*'
        r'g_activeSidebarItem = active;\s*\}\);'
    )
    replacement = '''item->getActiveEvent()->subscribe([](brls::View* active) {
                                    g_activeSidebarItem = active;
                                });'''
    source, count = pattern.subn(replacement, source, count=1)
    if count != 1:
        raise SystemExit("Could not locate sidebar active-event subscription")

# Mark the dynamically installed Home content and keep a direct pointer to it.
if "g_homeContentView = homeContent;" not in source:
    marker = '                        homeContent = nullptr;\n'
    if source.count(marker) != 1:
        raise SystemExit("Could not locate initial Home ownership handoff")
    addition = '''                        g_homeContentView = homeContent;
                        g_homeContentInstalled = true;
                        homeContent = nullptr;
'''
    source = source.replace(marker, addition, 1)

# Offline Home remains a single focus target.
if 'if (api.response.empty()) homeBox->setFocusable(true);' not in source:
    marker = '                        if (homeBox)\n                        {\n'
    if source.count(marker) >= 1:
        source = source.replace(marker, marker + '                            if (api.response.empty()) homeBox->setFocusable(true);\n', 1)

# The refresh helper replaces Home safely. The controller owns the in-progress
# guard and updates g_homeContentView after a successful replacement.

# This Borealis revision has no public getTabContent(). The pointer is captured
# directly at the initial ownership handoff above and on each refresh.
source = source.replace('    g_homeContentView = tabFrame ? tabFrame->getTabContent() : nullptr;\n', '', 1)
source = source.replace('    log_stage("HOME FOCUS REFRESH CHECK INSTALLED");\n', '', 1)

# The carousel currently focuses its first card before TabFrame owns the Home
# view. Remove that focus operation and restore it only after attachment.
old_focus = '''    if (cardCount > 0)\n    {\n        const auto& children = row->getChildren();\n        if (!children.empty())\n        {\n            brls::Application::giveFocus(children[0]);\n            log_stage("TRENDING FIRST CARD FOCUS RESTORED");\n        }\n    }\n\n'''
if old_focus in source:
    source = source.replace(old_focus, '    log_stage("TRENDING CARDS READY FOR POST-ATTACH FOCUS");\n\n', 1)
elif "TRENDING CARDS READY FOR POST-ATTACH FOCUS" not in source:
    raise SystemExit("Could not locate premature Home card focus block")

# Restore focus after TabFrame has taken ownership of the new Home content.
# Layout is deterministic: Home Box -> carousel viewport -> card row -> cards.
if "static void focus_first_home_card" not in source:
    marker = 'static void refresh_home_content(brls::TabFrame* tabFrame)\n'
    helper = '''static void focus_first_home_card(brls::View* homeContent)\n{\n    brls::Box* homeBox = dynamic_cast<brls::Box*>(homeContent);\n    if (!homeBox)\n    {\n        log_stage("HOME POST-ATTACH FOCUS: HOME BOX LOOKUP FAILED");\n        return;\n    }\n    const auto& homeChildren = homeBox->getChildren();\n    if (homeChildren.empty())\n    {\n        log_stage("HOME POST-ATTACH FOCUS: HOME HAS NO CHILDREN");\n        return;\n    }\n    brls::Box* viewport = dynamic_cast<brls::Box*>(homeChildren.back());\n    if (!viewport)\n    {\n        log_stage("HOME POST-ATTACH FOCUS: VIEWPORT LOOKUP FAILED");\n        return;\n    }\n    const auto& viewportChildren = viewport->getChildren();\n    if (viewportChildren.empty())\n    {\n        log_stage("HOME POST-ATTACH FOCUS: VIEWPORT HAS NO CHILDREN");\n        return;\n    }\n    brls::Box* row = dynamic_cast<brls::Box*>(viewportChildren.front());\n    if (!row)\n    {\n        log_stage("HOME POST-ATTACH FOCUS: CARD ROW LOOKUP FAILED");\n        return;\n    }\n    const auto& cards = row->getChildren();\n    if (cards.empty())\n    {\n        log_stage("HOME POST-ATTACH FOCUS: NO CARDS");\n        return;\n    }\n    brls::Application::giveFocus(cards.front());\n    log_stage("HOME POST-ATTACH FIRST CARD FOCUS RESTORED");\n}\n\n'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate Home refresh helper boundary")
    source = source.replace(marker, helper + marker, 1)

refresh_marker = '''    tabFrame->setTabContent(homeContent);\n    g_homeContentView = homeContent;\n    log_stage("AFTER REFRESH TABFRAME CONTENT SET");'''
refresh_replacement = '''    tabFrame->setTabContent(homeContent);\n    g_homeContentView = homeContent;\n    log_stage("AFTER REFRESH TABFRAME CONTENT SET");\n    focus_first_home_card(homeContent);'''
if refresh_marker in source:
    source = source.replace(refresh_marker, refresh_replacement, 1)
elif 'focus_first_home_card(homeContent);' not in source:
    raise SystemExit("Could not locate Home refresh attachment point")

initial_marker = '''                        g_homeContentView = homeContent;\n                        g_homeContentInstalled = true;\n                        homeContent = nullptr;'''
initial_replacement = '''                        g_homeContentView = homeContent;\n                        g_homeContentInstalled = true;\n                        focus_first_home_card(homeContent);\n                        homeContent = nullptr;'''
if initial_marker in source:
    source = source.replace(initial_marker, initial_replacement, 1)
elif 'focus_first_home_card(homeContent);' not in source:
    raise SystemExit("Could not locate initial Home ownership handoff")

path.write_text(source)
print("Home persistence and focus now restore the first card only after TabFrame owns the Home view")