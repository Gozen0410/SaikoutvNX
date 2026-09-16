#include "anilist_parser.hpp"
#include "json.hpp"

AnimeList parseAniListTrending(const std::string& response)
{
    AnimeList result;
    const std::vector<std::string> mediaObjects = json::objects(response, "media");
    for (const std::string& media : mediaObjects)
    {
        AnimeSummary anime;
        anime.id = static_cast<int>(json::integer(media, "id", 0));

        const std::string title = json::object(media, "title");
        if (!title.empty())
        {
            anime.title = json::str(title, "english");
            if (anime.title.empty()) anime.title = json::str(title, "romaji");
            if (anime.title.empty()) anime.title = json::str(title, "native");
        }

        const std::string cover = json::object(media, "coverImage");
        if (!cover.empty()) anime.coverUrl = json::str(cover, "large");

        anime.format = json::str(media, "format");
        anime.score = static_cast<int>(json::integer(media, "averageScore", 0));

        if (anime.id > 0 && !anime.title.empty()) result.push_back(anime);
        if (result.size() == 6) break;
    }
    return result;
}
