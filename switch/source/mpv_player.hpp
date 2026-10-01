#pragma once

#include <borealis.hpp>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <string>
#include <vector>

class SaikouMpvVideoView : public brls::View
{
public:
    explicit SaikouMpvVideoView(std::string url, std::vector<std::string> headers = {});
    ~SaikouMpvVideoView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void togglePause();

private:
    void handleEvents();

    mpv_handle* m_mpv = nullptr;
    mpv_render_context* m_render = nullptr;
    int m_defaultFramebuffer = 0;
    std::vector<std::string> m_headers;
    std::string m_status = "Connecting to stream...";
};

class SaikouMpvPlayerActivity : public brls::Activity
{
public:
    SaikouMpvPlayerActivity(std::string animeTitle, std::string episodeTitle,
                            std::string streamLabel, std::string url, std::vector<std::string> headers = {});
    brls::View* createContentView() override;

private:
    std::string m_animeTitle;
    std::string m_episodeTitle;
    std::string m_streamLabel;
    std::string m_url;
    std::vector<std::string> m_headers;
    SaikouMpvVideoView* m_video = nullptr;
};
