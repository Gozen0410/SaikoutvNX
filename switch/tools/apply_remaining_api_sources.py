from pathlib import Path

source_path = Path("switch/source/main.cpp")
xml_path = Path("switch/romfs/xml/activity/main.xml")
source = source_path.read_text()
xml = xml_path.read_text()

# Expand the existing stable numeric provider IDs without renumbering them.
source = source.replace("value <= 2", "value <= 4")
source = source.replace("value >= 0 && value <= 2", "value >= 0 && value <= 4")
source = source.replace('case 1: return "AnimePahe";\n        case 2: return "Gogoanime";', 'case 1: return "AnimePahe";\n        case 2: return "Gogoanime";\n        case 3: return "Aniwatch";\n        case 4: return "HiAnime";')

# Add the two missing provider controls to the existing Settings XML.
if 'id="api-source-aniwatch"' not in xml:
    needle = '<brls:Button id="api-source-gogoanime" width="auto" height="auto" text="Gogoanime" />'
    replacement = needle + '''\n            <brls:Button id="api-source-aniwatch" width="auto" height="auto" text="Aniwatch" />\n            <brls:Button id="api-source-hianime" width="auto" height="auto" text="HiAnime" />'''
    if xml.count(needle) != 1:
        raise SystemExit("Could not locate Gogoanime selector button")
    xml = xml.replace(needle, replacement, 1)

# Bind the new buttons alongside the existing three.
if 'api-source-aniwatch' not in source:
    old = '''    brls::Button* gogoanime = dynamic_cast<brls::Button*>(settingsTab->getView("api-source-gogoanime"));'''
    new = old + '''\n    brls::Button* aniwatch = dynamic_cast<brls::Button*>(settingsTab->getView("api-source-aniwatch"));\n    brls::Button* hianime = dynamic_cast<brls::Button*>(settingsTab->getView("api-source-hianime"));'''
    if source.count(old) != 1:
        raise SystemExit("Could not locate Gogoanime settings binding")
    source = source.replace(old, new, 1)

    source = source.replace(
        'if (!current || !miruro || !animepahe || !gogoanime)',
        'if (!current || !miruro || !animepahe || !gogoanime || !aniwatch || !hianime)',
        1,
    )

    old_action = '''    gogoanime->registerClickAction([current](brls::View*) {\n        g_apiSource = 2;\n        current->setText("Anime API: Gogoanime");\n        save_api_source();\n        return true;\n    });'''
    new_action = old_action + '''\n\n    aniwatch->registerClickAction([current](brls::View*) {\n        g_apiSource = 3;\n        current->setText("Anime API: Aniwatch");\n        save_api_source();\n        return true;\n    });\n\n    hianime->registerClickAction([current](brls::View*) {\n        g_apiSource = 4;\n        current->setText("Anime API: HiAnime");\n        save_api_source();\n        return true;\n    });'''
    if source.count(old_action) != 1:
        raise SystemExit("Could not locate Gogoanime click action")
    source = source.replace(old_action, new_action, 1)

    old_routes = '''        gogoanime->setCustomNavigationRoute(brls::FocusDirection::LEFT, g_activeSidebarItem);'''
    new_routes = old_routes + '''\n        aniwatch->setCustomNavigationRoute(brls::FocusDirection::LEFT, g_activeSidebarItem);\n        hianime->setCustomNavigationRoute(brls::FocusDirection::LEFT, g_activeSidebarItem);'''
    if source.count(old_routes) != 1:
        raise SystemExit("Could not locate Gogoanime navigation route")
    source = source.replace(old_routes, new_routes, 1)

source_path.write_text(source)
xml_path.write_text(xml)
print("Added Aniwatch and HiAnime to the existing selector")
