    std::string m_username;
    std::string m_loadStatus;
    std::vector<AniListEntry> m_entries;

    std::thread m_worker;
    std::atomic<bool> m_ready{ false };
    bool m_loading = false;
    bool m_loaded = false;

    struct CoverJob
    {
        brls::Image* image = nullptr;
        std::string url;
        std::string path;
    };

    std::vector<brls::HScrollingFrame*> m_categoryScrolls;
    std::vector<CoverJob> m_pendingCoverJobs;
    std::thread m_coverWorker;
    bool m_coverWorkerDone = true;
    std::shared_ptr<std::atomic<bool>> m_coverLifetime =
        std::make_shared<std::atomic<bool>>(true);

    void build_sections()
    {
        if (!m_sections)
            return;

        clear_box(m_sections);
        m_categoryScrolls.clear();

        std::vector<AniListEntry> continueEntries;
        size_t currentCount = 0;

        for (const AniListEntry& entry : m_entries)
        {
            if (entry.listStatus != "CURRENT")
                continue;

            ++currentCount;

            if (continueEntries.size() < kHomeBatchSize)
                continueEntries.push_back(entry);
        }

        if (!continueEntries.empty())
        {
            brls::Box* continueHeader =
                new brls::Box(brls::Axis::ROW);
            continueHeader->setWidth(1160.0f);
            continueHeader->setHeight(28.0f);
            continueHeader->setAlignItems(
                brls::AlignItems::CENTER);

            brls::Label* continueTitle =
                new brls::Label();
            continueTitle->setText("CONTINUE WATCHING");
            continueTitle->setFontSize(19.0f);
            continueTitle->setTextColor(
                nvgRGB(220, 228, 240));
            continueHeader->addView(continueTitle);

            brls::Label* continueCount =
                new brls::Label();
            continueCount->setText(
                "  " +
                std::to_string(continueEntries.size()));
            continueCount->setFontSize(13.0f);
            continueCount->setTextColor(
                nvgRGB(135, 147, 166));
            continueHeader->addView(continueCount);

            m_sections->addView(continueHeader);
