#pragma once

#include <string>
#include <vector>

struct AnimeSummary
{
    int id = 0;
    std::string title;
    std::string coverUrl;
    std::string format;
    int score = 0;
};

using AnimeList = std::vector<AnimeSummary>;
