from pathlib import Path

path = Path("switch/source/main.cpp")
source = path.read_text()

if "SAIKOU_ANIME_DETAILS_V1" not in source or "class AnimeDetailsActivity" not in source:
    raise SystemExit("Anime details implementation is not installed")

old = '''        card->registerAction("Open anime", brls::BUTTON_A, [i](brls::View*) {
            char marker[64];
            std::snprintf(marker, sizeof(marker), "TRENDING CARD SELECTED %zu", i);
            log_stage(marker);
            return true;
        });'''

new = '''        card->registerAction("Open anime", brls::BUTTON_A, [i, titles, covers](brls::View*) {
            if (i >= titles.size())
                return false;

            g_selectedAnimeTitle = titles[i];
            g_selectedAnimeCover = i < covers.size() ? covers[i] : std::string();

            char marker[128];
            std::snprintf(marker, sizeof(marker), "TRENDING CARD OPEN DETAILS %zu", i);
            log_stage(marker);

            brls::Application::pushActivity(
                new AnimeDetailsActivity(g_selectedAnimeTitle, g_selectedAnimeCover),
                brls::TransitionAnimation::SLIDE_LEFT);
            return true;
        });'''

if old not in source:
    if "TRENDING CARD OPEN DETAILS" in source:
        print("Anime details A action already installed")
        raise SystemExit(0)
    raise SystemExit("Could not locate original trending card A action")

source = source.replace(old, new, 1)
path.write_text(source)
print("Forced Home card A action to AnimeDetailsActivity")
