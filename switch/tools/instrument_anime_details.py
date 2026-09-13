from pathlib import Path

path = Path("switch/source/main.cpp")
source = path.read_text()

if "ANIME_DETAILS_DIAGNOSTICS_V1" in source:
    print("Anime details diagnostics already installed")
    raise SystemExit(0)

source = source.replace(
    "// SAIKOU_ANIME_DETAILS_V1: Home card -> anime details screen.",
    "// SAIKOU_ANIME_DETAILS_V1: Home card -> anime details screen.\n// ANIME_DETAILS_DIAGNOSTICS_V1: activation breadcrumbs only.",
    1,
)

old = '''    brls::View* createContentView() override\n    {\n        return new brls::ScrollingFrame();\n    }'''
new = '''    brls::View* createContentView() override\n    {\n        log_stage("DETAILS BEFORE CREATE CONTENT VIEW");\n        brls::View* view = new brls::ScrollingFrame();\n        log_stage(view ? "DETAILS AFTER CREATE CONTENT VIEW" : "DETAILS CREATE CONTENT RETURNED NULL");\n        return view;\n    }'''
if old not in source:
    raise SystemExit("Could not locate AnimeDetailsActivity::createContentView")
source = source.replace(old, new, 1)

source = source.replace(
    '''    void onContentAvailable() override\n    {\n        brls::ScrollingFrame* frame = dynamic_cast<brls::ScrollingFrame*>(getContentView());''',
    '''    void onContentAvailable() override\n    {\n        log_stage("DETAILS BEFORE CONTENT AVAILABLE");\n        brls::ScrollingFrame* frame = dynamic_cast<brls::ScrollingFrame*>(getContentView());\n        log_stage(frame ? "DETAILS AFTER CONTENT FRAME CAST" : "DETAILS CONTENT FRAME CAST FAILED");''',
    1,
)

source = source.replace(
    '''        ApiResult result = fetch_anime_details_by_title(title);''',
    '''        log_stage("DETAILS BEFORE NETWORK REQUEST");\n        ApiResult result = fetch_anime_details_by_title(title);\n        log_stage("DETAILS AFTER NETWORK REQUEST");''',
    1,
)

source = source.replace(
    '''            brls::Application::pushActivity(\n                new AnimeDetailsActivity(g_selectedAnimeTitle, g_selectedAnimeCover),\n                brls::TransitionAnimation::SLIDE_LEFT);''',
    '''            log_stage("DETAILS BEFORE ACTIVITY ALLOCATION");\n            AnimeDetailsActivity* details = new AnimeDetailsActivity(g_selectedAnimeTitle, g_selectedAnimeCover);\n            log_stage(details ? "DETAILS AFTER ACTIVITY ALLOCATION" : "DETAILS ACTIVITY ALLOCATION FAILED");\n            log_stage("DETAILS BEFORE PUSH ACTIVITY");\n            brls::Application::pushActivity(details, brls::TransitionAnimation::SLIDE_LEFT);\n            log_stage("DETAILS AFTER PUSH ACTIVITY");''',
    1,
)

path.write_text(source)
print("Anime details activation breadcrumbs installed")
