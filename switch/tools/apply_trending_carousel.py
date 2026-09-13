from pathlib import Path

path = Path("switch/source/main.cpp")
source = path.read_text()

# The API path is maintained in main.cpp itself. This build-time patch only
# installs the carousel renderer; it must never try to rewrite API code.
if "class TrendingCarouselViewport" not in source:
    start = source.find("static void render_trending(")
    end = source.find("\nint main(", start)
    if start < 0 or end < 0:
        raise SystemExit("Could not locate render_trending() boundaries")

    replacement = r'''class TrendingCarouselViewport : public brls::Box
{
public:
    static constexpr size_t kVisibleCards = 5;
    static constexpr float kCardStep = 128.0f;

    TrendingCarouselViewport() : brls::Box(brls::Axis::ROW) {}

    void setContent(brls::Box* content)
    {
        this->content = content;
        this->addView(content);
    }

    void configure(size_t count)
    {
        totalCards = count;
        offset = 0;
        animating = false;
        scrollX.reset(0.0f);
        if (content)
            content->setTranslationX(0.0f);
    }

    void registerCardAction(brls::View* card, size_t index)
    {
        card->registerAction("Previous", brls::BUTTON_LEFT, [this, index](brls::View*) {
            if (index == offset && offset > 0)
            {
                moveLeft();
                return true;
            }
            return false;
        });

        card->registerAction("Next", brls::BUTTON_RIGHT, [this, index](brls::View*) {
            if (totalCards == 0)
                return false;
            const size_t visibleEnd = std::min(offset + kVisibleCards - 1, totalCards - 1);
            if (index == visibleEnd && visibleEnd + 1 < totalCards)
            {
                moveRight();
                return true;
            }
            return false;
        });
    }

private:
    brls::Box* content = nullptr;
    size_t totalCards = 0;
    size_t offset = 0;
    bool animating = false;
    brls::Animatable scrollX = 0.0f;

    void moveRight()
    {
        if (!content || animating || totalCards == 0)
            return;
        const size_t visibleEnd = std::min(offset + kVisibleCards - 1, totalCards - 1);
        if (visibleEnd + 1 >= totalCards)
            return;
        animateTo(offset + 1, true);
    }

    void moveLeft()
    {
        if (!content || animating || offset == 0)
            return;
        animateTo(offset - 1, false);
    }

    void animateTo(size_t newOffset, bool forward)
    {
        animating = true;
        const float target = -static_cast<float>(newOffset) * kCardStep;
        scrollX.reset(scrollX.getValue());
        scrollX.addStep(target, 220, brls::EasingFunction::quadraticOut);
        scrollX.setTickCallback([this] {
            if (content)
                content->setTranslationX(scrollX.getValue());
        });
        scrollX.setEndCallback([this, newOffset, forward](bool) {
            offset = newOffset;
            if (content)
            {
                content->setTranslationX(-static_cast<float>(offset) * kCardStep);
                const auto& children = content->getChildren();
                if (!children.empty())
                {
                    const size_t focusIndex = forward
                        ? std::min(offset + kVisibleCards - 1, children.size() - 1)
                        : std::min(offset, children.size() - 1);
                    brls::Application::giveFocus(children[focusIndex]);
                }
            }
            animating = false;
        });
        scrollX.start();
    }
};

static void render_trending(brls::Box* homeBox, const std::string& response)
{
    if (!homeBox || response.empty()) return;

    log_stage("BEFORE TRENDING PARSE");
    std::vector<std::string> titles = extract_trending_titles(response);
    std::vector<std::string> details = extract_trending_details(response);
    std::vector<std::string> covers = extract_trending_covers(response);

    char marker[64];
    std::snprintf(marker, sizeof(marker), "TRENDING PARSE FOUND %zu TITLES", titles.size());
    log_stage(marker);
    if (titles.empty())
    {
        log_stage("TRENDING PARSE FOUND NO TITLES");
        return;
    }

    brls::Label* heading = new brls::Label();
    heading->setText("Trending Now");
    heading->setFontSize(27);
    heading->setMargins(0, 10, 0, 0);
    homeBox->addView(heading);

    TrendingCarouselViewport* viewport = new TrendingCarouselViewport();
    viewport->setWidth(660);
    viewport->setHeight(245);
    viewport->setGrow(0.0f);
    viewport->setShrink(0.0f);
    viewport->setMargins(0, 7, 0, 0);

    brls::Box* row = new brls::Box(brls::Axis::ROW);
    row->setWidth(780);
    row->setHeight(235);
    row->setGrow(0.0f);
    row->setShrink(0.0f);
    row->setAlignItems(brls::AlignItems::FLEX_START);
    viewport->setContent(row);
    viewport->configure(std::min<size_t>(titles.size(), 6));
    homeBox->addView(viewport);

    const size_t cardCount = std::min<size_t>(titles.size(), 6);
    for (size_t i = 0; i < cardCount; ++i)
    {
        brls::Box* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(124);
        card->setHeight(232);
        card->setShrink(0.0f);
        card->setMargins(2, 3, 2, 0);
        card->setFocusable(true);
        card->setHighlightPadding(5.0f);
        card->setCornerRadius(5.0f);
        card->setFocusSound(brls::SOUND_FOCUS_CHANGE);

        bool imageAttached = false;
        if (i < covers.size() && !covers[i].empty())
        {
            char pathBuffer[128];
            std::snprintf(pathBuffer, sizeof(pathBuffer), "%s/trending_%zu.jpg", kCacheDir, i);
            const std::string imagePath = pathBuffer;
            log_stage("BEFORE TRENDING CARD IMAGE DOWNLOAD");
            if (download_image(covers[i], imagePath))
            {
                brls::Image* image = new brls::Image();
                image->setDimensions(116, 174);
                image->setScalingType(brls::ImageScalingType::CROP);
                image->setImageFromFile(imagePath);
                image->setFocusable(false);
                card->addView(image);
                imageAttached = true;
                log_stage("TRENDING CARD IMAGE ATTACHED");
            }
        }

        if (!imageAttached)
        {
            brls::Label* missing = new brls::Label();
            missing->setText("No image");
            missing->setFontSize(13);
            missing->setSingleLine(true);
            card->addView(missing);
        }

        brls::Label* title = new brls::Label();
        title->setText(compact_title(titles[i]));
        title->setFontSize(14);
        title->setLineHeight(17);
        title->setMaxWidth(116);
        title->setMargins(2, 4, 2, 0);
        title->setFocusable(false);
        card->addView(title);

        if (i < details.size() && !details[i].empty())
        {
            brls::Label* detail = new brls::Label();
            detail->setText(details[i]);
            detail->setFontSize(11);
            detail->setLineHeight(14);
            detail->setMaxWidth(116);
            detail->setSingleLine(true);
            detail->setMargins(2, 1, 2, 0);
            detail->setFocusable(false);
            card->addView(detail);
        }

        viewport->registerCardAction(card, i);
        card->registerAction("Open anime", brls::BUTTON_A, [i](brls::View*) {
            char marker[64];
            std::snprintf(marker, sizeof(marker), "TRENDING CARD SELECTED %zu", i);
            log_stage(marker);
            return true;
        });
        row->addView(card);
    }

    log_stage("TRENDING UI ATTACHED");
}
'''
    source = source[:start] + replacement + source[end:]

path.write_text(source)
print("Trending carousel patch applied; API code left untouched")
