#pragma once

#include "anime.hpp"
#include <borealis.hpp>

brls::View* createHomeView(const AnimeList& trending);
brls::View* createPlaceholderView(const char* title, const char* subtitle);
