from pathlib import Path

path = Path("switch/source/main.cpp")
source = path.read_text()

# The carousel currently gives focus while its cards are still detached from
# TabFrame. That leaves Borealis' focus tracker pointing at an unattached view.
# Remove that premature focus operation; focus is restored only after the
# complete Home view has been attached to TabFrame.
old_focus = '''    if (cardCount > 0)\n    {\n        const auto& children = row->getChildren();\n        if (!children.empty())\n        {\n            brls::Application::giveFocus(children[0]);\n            log_stage("TRENDING FIRST CARD FOCUS RESTORED");\n        }\n    }\n\n'''
if old_focus in source:
    source = source.replace(old_focus, '    log_stage("TRENDING CARDS READY FOR POST-ATTACH FOCUS");\n\n', 1)
elif "TRENDING CARDS READY FOR POST-ATTACH FOCUS" not in source:
    raise SystemExit("Could not locate premature Home card focus block")

# Walk the known Home layout: Home Box -> carousel viewport -> row -> cards.
# This intentionally avoids any TabFrame getter that is not part of the pinned
# Borealis API used by this project.
if "static void focus_first_home_card" not in source:
    marker = 'static void refresh_home_content(brls::TabFrame* tabFrame)\n'
    helper = '''static void focus_first_home_card(brls::View* homeContent)\n{\n    brls::Box* homeBox = dynamic_cast<brls::Box*>(homeContent);\n    if (!homeBox)\n    {\n        log_stage("HOME POST-ATTACH FOCUS: HOME BOX LOOKUP FAILED");\n        return;\n    }\n\n    const auto& homeChildren = homeBox->getChildren();\n    if (homeChildren.empty())\n    {\n        log_stage("HOME POST-ATTACH FOCUS: HOME HAS NO CHILDREN");\n        return;\n    }\n\n    brls::Box* viewport = dynamic_cast<brls::Box*>(homeChildren.back());\n    if (!viewport)\n    {\n        log_stage("HOME POST-ATTACH FOCUS: VIEWPORT LOOKUP FAILED");\n        return;\n    }\n\n    const auto& viewportChildren = viewport->getChildren();\n    if (viewportChildren.empty())\n    {\n        log_stage("HOME POST-ATTACH FOCUS: VIEWPORT HAS NO CHILDREN");\n        return;\n    }\n\n    brls::Box* row = dynamic_cast<brls::Box*>(viewportChildren.front());\n    if (!row)\n    {\n        log_stage("HOME POST-ATTACH FOCUS: CARD ROW LOOKUP FAILED");\n        return;\n    }\n\n    const auto& cards = row->getChildren();\n    if (cards.empty())\n    {\n        log_stage("HOME POST-ATTACH FOCUS: NO CARDS");\n        return;\n    }\n\n    brls::Application::giveFocus(cards.front());\n    log_stage("HOME POST-ATTACH FIRST CARD FOCUS RESTORED");\n}\n\n'''
    if source.count(marker) != 1:
        raise SystemExit("Could not locate Home refresh helper boundary")
    source = source.replace(marker, helper + marker, 1)

# Refresh path: attach first, then focus the newly attached card.
refresh_marker = '''    tabFrame->setTabContent(homeContent);\n    g_homeContentView = homeContent;\n    log_stage("AFTER REFRESH TABFRAME CONTENT SET");'''
refresh_replacement = '''    tabFrame->setTabContent(homeContent);\n    g_homeContentView = homeContent;\n    log_stage("AFTER REFRESH TABFRAME CONTENT SET");\n    focus_first_home_card(homeContent);'''
if refresh_marker in source:
    source = source.replace(refresh_marker, refresh_replacement, 1)
elif "focus_first_home_card(homeContent);" not in source:
    raise SystemExit("Could not locate Home refresh attachment point")

# Initial path: fix_home_persistence has already recorded the installed view;
# focus it only after setTabContent has completed.
initial_marker = '''                        g_homeContentView = homeContent;\n                        g_homeContentInstalled = true;\n                        homeContent = nullptr;'''
initial_replacement = '''                        g_homeContentView = homeContent;\n                        g_homeContentInstalled = true;\n                        focus_first_home_card(homeContent);\n                        homeContent = nullptr;'''
if initial_marker in source:
    source = source.replace(initial_marker, initial_replacement, 1)
elif "focus_first_home_card(homeContent);" not in source:
    raise SystemExit("Could not locate initial Home ownership handoff")

path.write_text(source)
print("Home card focus is now restored only after TabFrame owns the attached Home view")