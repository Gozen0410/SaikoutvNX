#include "anilist_parser.hpp"

#include <cassert>
#include <string>

int main()
{
    const std::string response = R"json({"data":{"Page":{"media":[
        {"id":101,"title":{"english":"English One","romaji":"Romaji One","native":"Native One"},"coverImage":{"large":"https://example/1.jpg"},"format":"TV","averageScore":85},
        {"id":102,"title":{"english":null,"romaji":"Romaji Two","native":"Native Two"},"coverImage":{"large":"https://example/2.jpg"},"format":"MOVIE","averageScore":78},
        {"id":103,"title":{"english":"English Three","romaji":"Romaji Three","native":"Native Three"},"coverImage":{"large":"https://example/3.jpg"},"format":"TV","averageScore":91},
        {"id":104,"title":{"english":"English Four","romaji":"Romaji Four","native":"Native Four"},"coverImage":{"large":"https://example/4.jpg"},"format":"OVA","averageScore":74},
        {"id":105,"title":{"english":"English Five","romaji":"Romaji Five","native":"Native Five"},"coverImage":{"large":"https://example/5.jpg"},"format":"TV","averageScore":82},
        {"id":106,"title":{"english":null,"romaji":"Romaji Six","native":"Native Six"},"coverImage":{"large":"https://example/6.jpg"},"format":"ONA","averageScore":89},
        {"id":107,"title":{"english":"Should Not Be Included","romaji":"Seven","native":"Seven"},"coverImage":{"large":"https://example/7.jpg"},"format":"TV","averageScore":70}
    ]}}})json";

    const AnimeList cards = parseAniListTrending(response);
    assert(cards.size() == 6);
    assert(cards[0].id == 101 && cards[0].title == "English One");
    assert(cards[1].id == 102 && cards[1].title == "Romaji Two");
    assert(cards[5].id == 106 && cards[5].title == "Romaji Six");
    return 0;
}
