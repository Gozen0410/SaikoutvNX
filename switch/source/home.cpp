#include "home.hpp"
#include "image_cache.hpp"

#include <borealis/views/image.hpp>
#include <borealis/views/label.hpp>
#include <string>

namespace
{
static std::string compactTitle(const std::string& title)
{
    constexpr size_t kMaxChars = 28;
    if (title.size() <= kMaxChars) return title;
    return title.substr(0, kMaxChars - 3) + "...";
}

static brls::Box* makeCard(const AnimeSummary& anime)
{
    brls::Box* card = new brls::Box(brls::Axis::COLUMN);
    // Keep the card close to AniList's portrait cover ratio (~2:3).
    card->setWidth(170);
    card->setHeight(305);
    card->setMargins(4, 6, 4, 0);
    card->setFocusable(true);
    // Keep the focus frame inside the card bounds so it does not intrude into
    // neighboring cards or their text.
    card->setHighlightPadding(0.0f);
    card->setCornerRadius(6.0f);

    const std::string imagePath = ensureAnimeCoverCached(anime);
    if (!imagePath.empty())
    {
        brls::Image* image = new brls::Image();
        image->setDimensions(162, 243);
        // The card is 170px wide while the image is 162px wide. Give the image
        // equal horizontal margins so it is centered without changing the
        // alignment of the title/meta labels below it.
        image->setMargins(0, 4, 0, 4);
        image->setScalingType(brls::ImageScalingType::CROP);
        image->setImageFromFile(imagePath);
        image->setFocusable(false);
        card->addView(image);
    }
    else
    {
        brls::Label* missing = new brls::Label();
        missing->setText("Cover unavailable");
        missing->setFontSize(13);
        missing->setMargins(4, 90, 4, 0);
        missing->setFocusable(false);
        card->addView(missing);
    }

    brls::Label* title = new brls::Label();
    title->setText(compactTitle(anime.title));
    title->setFontSize(15);
    title->setSingleLine(true);
    title->setMaxWidth(162);
    title->setMargins(5, 7, 5, 0);
    title->setFocusable(false);
    card->addView(title);

    std::string detail = anime.format;
    if (anime.score > 0)
    {
        if (!detail.empty()) detail += "  •  ";
        detail += "Score: " + std::to_string(anime.score);
    }
    if (!detail.empty())
    {
        brls::Label* meta = new brls::Label();
        meta->setText(detail);
        meta->setFontSize(11);
        meta->setSingleLine(true);
        meta->setMaxWidth(162);
        meta->setMargins(5, 2, 5, 0);
        meta->setFocusable(false);
        card->addView(meta);
    }

    return card;
}
}

brls::View* createHomeView(const AnimeList& trending)
{
    brls::Box* root = new brls::Box(brls::Axis::COLUMN);
    root->setPaddingTop(32);
    root->setPaddingBottom(32);
    root->setPaddingLeft(34);
    root->setPaddingRight(34);

    brls::Label* appTitle = new brls::Label();
    appTitle->setText("Saikou Switch");
    appTitle->setFontSize(34);
    root->addView(appTitle);

    brls::Label* subtitle = new brls::Label();
    subtitle->setText("Anime metadata powered by AniList");
    subtitle->setFontSize(15);
    subtitle->setMargins(0, 5, 0, 0);
    root->addView(subtitle);

    brls::Label* heading = new brls::Label();
    heading->setText("Trending");
    heading->setFontSize(28);
    heading->setMargins(0, 22, 0, 0);
    root->addView(heading);

    if (trending.empty())
    {
        brls::Label* error = new brls::Label();
        error->setText("Unable to load Trending from AniList");
        error->setFontSize(15);
        error->setMargins(0, 8, 0, 0);
        root->addView(error);
        return root;
    }

    brls::Label* status = new brls::Label();
    status->setText("AniList • " + std::to_string(trending.size()) + " cards");
    status->setFontSize(13);
    status->setMargins(0, 5, 0, 0);
    root->addView(status);

    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setGrow(0.0f);
    row->setMargins(0, 10, 0, 0);
    for (size_t i = 0; i < trending.size() && i < 6; ++i)
        row->addView(makeCard(trending[i]));
    root->addView(row);

    return root;
}

brls::View* createPlaceholderView(const char* titleText, const char* subtitleText)
{
    brls::Box* root = new brls::Box(brls::Axis::COLUMN);
    root->setPaddingTop(40);
    root->setPaddingBottom(40);
    root->setPaddingLeft(50);
    root->setPaddingRight(50);

    brls::Label* title = new brls::Label();
    title->setText(titleText);
    title->setFontSize(34);
    root->addView(title);

    brls::Label* subtitle = new brls::Label();
    subtitle->setText(subtitleText);
    subtitle->setFontSize(15);
    subtitle->setMargins(0, 12, 0, 0);
    root->addView(subtitle);
    return root;
}
