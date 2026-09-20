// VideoPlayer.hpp - Optional video wallpaper behind the lock screen.
//
// Uses libmpv's software render API (MPV_RENDER_API_TYPE_SW): mpv decodes and
// renders directly into a CPU buffer we then composite with cairo. No EGL/GL
// pipeline is needed, which keeps this simple and robust (no context/FBO/
// read-back/orientation pitfalls). Playback is sampled on a repeating timer.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mpv/client.h>
#include <mpv/render.h>

#include "core/Interfaces.hpp"  // cairo_t, RenderHost

namespace qypr {

class EventLoop;

class VideoPlayer {
public:
    VideoPlayer(EventLoop& loop, RenderHost& host);
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Create mpv + the software render context and load the time-of-day
    // playlist. Returns false (non-fatal) if mpv is unavailable.
    bool init(const std::string& playlistDir = "");

    void start();  // begin sampling frames on a timer
    void stop();

    // Idle control: pause() halts decoding (stops the CPU cost) and freezes the
    // render timer; resume() undoes both. Safe to call redundantly.
    void pause();
    void resume();
    bool paused() const { return paused_; }

    void draw(cairo_t* cr, int outW, int outH);
    bool hasFrame() const { return frameW_ > 0 && frameH_ > 0 && haveContent_; }

private:
    static void onWakeup(void* self);
    void handleWakeup();
    void processEvents();
    void renderFrame();
    void ensureBuffer();  // size the CPU buffer once mpv knows the video size
    void loadPlaylist(const std::string& path);

    EventLoop& loop_;
    RenderHost& host_;

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* renderCtx_ = nullptr;

    std::vector<uint8_t> pixels_;  // BGRX, frameW_ * frameH_ * 4
    int frameW_ = 0;
    int frameH_ = 0;
    bool haveContent_ = false;
    bool paused_ = false;
    int renderTimer_ = -1;
};

}  // namespace qypr
