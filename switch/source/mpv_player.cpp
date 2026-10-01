#include "mpv_player.hpp"

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <vector>

static void* saikou_mpv_get_proc_address(void*, const char* name)
{
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

SaikouMpvVideoView::SaikouMpvVideoView(std::string url)
{
    setFocusable(false);
    m_mpv = mpv_create();
    if (!m_mpv)
    {
        m_status = "Could not create the video player.";
        brls::Logger::error("mpv_create failed");
        return;
    }

    // The resolved scraper URL is passed straight to libmpv. libmpv/FFmpeg
    // handles HLS playlists, MP4 streams, redirects, and media probing.
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    mpv_set_option_string(m_mpv, "idle", "yes");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "osd-level", "0");
    mpv_set_option_string(m_mpv, "audio-channels", "stereo");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "network-timeout", "30");
    mpv_set_option_string(m_mpv, "tls-verify", "no");
    mpv_set_option_string(m_mpv, "hwdec", "auto");
    // Some anime CDNs use image-like file extensions for HLS segments.
    mpv_set_option_string(m_mpv, "demuxer-lavf-o", "extension_picky=0");
#ifdef __SWITCH__
    mpv_set_option_string(m_mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(m_mpv, "vd-lavc-threads", "4");
    mpv_set_option_string(m_mpv, "opengl-glfinish", "yes");
#endif
    mpv_request_log_messages(m_mpv, "info");

    const int initResult = mpv_initialize(m_mpv);
    if (initResult < 0)
    {
        m_status = std::string("Player initialization failed: ") + mpv_error_string(initResult);
        brls::Logger::error("mpv_initialize failed: {}", mpv_error_string(initResult));
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_defaultFramebuffer);
    mpv_opengl_init_params glInit{saikou_mpv_get_proc_address, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    const int renderResult = mpv_render_context_create(&m_render, m_mpv, params);
    if (renderResult < 0)
    {
        m_status = std::string("Video renderer initialization failed: ") + mpv_error_string(renderResult);
        brls::Logger::error("mpv_render_context_create failed: {}", mpv_error_string(renderResult));
        return;
    }

    std::vector<const char*> command = {"loadfile", url.c_str(), "replace", nullptr};
    const int loadResult = mpv_command_async(m_mpv, 0, command.data());
    if (loadResult < 0)
    {
        m_status = std::string("Could not open stream: ") + mpv_error_string(loadResult);
        brls::Logger::error("mpv loadfile failed: {}", mpv_error_string(loadResult));
    }
    brls::Logger::info("Starting native playback: {}", url);
}

SaikouMpvVideoView::~SaikouMpvVideoView()
{
    if (m_render)
        mpv_render_context_free(m_render);
    if (m_mpv)
        mpv_terminate_destroy(m_mpv);
}

void SaikouMpvVideoView::togglePause()
{
    if (!m_mpv)
        return;
    const char* command[] = {"cycle", "pause", nullptr};
    const int result = mpv_command_async(m_mpv, 0, command);
    if (result < 0)
        brls::Logger::error("mpv pause toggle failed: {}", mpv_error_string(result));
    m_status = "Playback controls: A pause/resume  |  B back";
}

void SaikouMpvVideoView::handleEvents()
{
    if (!m_mpv)
        return;
    while (true)
    {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        switch (event->event_id)
        {
            case MPV_EVENT_FILE_LOADED:
                m_status = "Playing  |  A pause/resume  |  B back";
                brls::Logger::info("mpv stream loaded");
                break;
            case MPV_EVENT_END_FILE:
            {
                auto* end = static_cast<mpv_event_end_file*>(event->data);
                if (end && end->reason == MPV_END_FILE_REASON_ERROR)
                {
                    m_status = std::string("Playback error: ") + mpv_error_string(end->error);
                    brls::Logger::error("mpv playback ended with error: {}", m_status);
                }
                else
                {
                    m_status = "Stream ended  |  B back";
                }
                break;
            }
            case MPV_EVENT_LOG_MESSAGE:
            {
                auto* message = static_cast<mpv_event_log_message*>(event->data);
                if (message)
                    brls::Logger::debug("[mpv/{}] {}", message->prefix, message->text);
                break;
            }
            default:
                break;
        }
    }
}

void SaikouMpvVideoView::draw(NVGcontext* vg, float x, float y, float width,
                              float height, brls::Style style, brls::FrameContext* ctx)
{
    handleEvents();
    if (m_render)
    {
        const int framebufferWidth = static_cast<int>(brls::Application::windowWidth);
        const int framebufferHeight = static_cast<int>(brls::Application::windowHeight);
        mpv_opengl_fbo fbo{m_defaultFramebuffer, framebufferWidth, framebufferHeight, 0};
        int flipY = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        const int result = mpv_render_context_render(m_render, params);
        if (result < 0)
        {
            m_status = std::string("Video render error: ") + mpv_error_string(result);
            brls::Logger::error("mpv render failed: {}", m_status);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_defaultFramebuffer);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        mpv_render_context_report_swap(m_render);
    }

    if (!m_status.empty())
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 16.0f, y + height - 48.0f, width - 32.0f, 34.0f, 5.0f);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 190));
        nvgFill(vg);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, nvgRGB(235, 239, 246));
        nvgText(vg, x + 28.0f, y + height - 26.0f, m_status.c_str(), nullptr);
    }
}

SaikouMpvPlayerActivity::SaikouMpvPlayerActivity(
    std::string animeTitle, std::string episodeTitle, std::string streamLabel, std::string url)
    : m_animeTitle(std::move(animeTitle)),
      m_episodeTitle(std::move(episodeTitle)),
      m_streamLabel(std::move(streamLabel)),
      m_url(std::move(url))
{
}

brls::View* SaikouMpvPlayerActivity::createContentView()
{
    brls::Box* root = new brls::Box(brls::Axis::COLUMN);
    root->setWidthPercentage(100.0f);
    root->setHeightPercentage(100.0f);
    root->setPadding(24.0f);
    root->setBackgroundColor(nvgRGB(16, 20, 29));
    root->registerAction("Back", brls::BUTTON_B, [](brls::View*) {
        brls::sync([] {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [] {}, true);
        });
        return true;
    });
    root->registerAction("Pause or resume playback", brls::BUTTON_A, [this](brls::View*) {
        if (m_video)
            m_video->togglePause();
        return true;
    });

    brls::Label* title = new brls::Label();
    title->setText(m_animeTitle);
    title->setFontSize(23.0f);
    title->setTextColor(nvgRGB(244, 246, 250));
    root->addView(title);

    brls::Label* episode = new brls::Label();
    episode->setText(m_episodeTitle + "  •  " + m_streamLabel);
    episode->setFontSize(16.0f);
    episode->setTextColor(nvgRGB(174, 184, 200));
    episode->setMargins(0, 4, 0, 0);
    root->addView(episode);

    m_video = new SaikouMpvVideoView(m_url);
    m_video->setWidthPercentage(100.0f);
    m_video->setGrow(1.0f);
    m_video->setMargins(0, 12, 0, 0);
    root->addView(m_video);

    brls::Label* controls = new brls::Label();
    controls->setText("A  Pause/resume     B  Back");
    controls->setFontSize(14.0f);
    controls->setTextColor(nvgRGB(135, 147, 166));
    controls->setMargins(0, 9, 0, 0);
    controls->setFocusable(false);
    root->addView(controls);
    return root;
}
