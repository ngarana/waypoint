#include "video/VideoPlayer.hpp"

#include <cairo/cairo.h>

#include <cstdio>
#include <ctime>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {

// The playlists live next to the binary's project root; entries are relative
// to the playlist file, so we only need to locate the playlists directory.
std::string resolvePlaylistDir() {
    if (FILE* f = std::fopen("playlists/day.m3u", "r")) {
        std::fclose(f);
        return "playlists";
    }
    return "/home/arch/.config/qypr/playlists";
}

const char* playlistFile() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    bool night = tm->tm_hour < 7 || tm->tm_hour >= 19;
    return night ? "night.m3u" : "day.m3u";
}

constexpr int kFrameIntervalMs = 33;  // ~30 fps sampling

}  // namespace

VideoPlayer::VideoPlayer(EventLoop& loop, RenderHost& host) : loop_(loop), host_(host) {}

VideoPlayer::~VideoPlayer() {
    stop();
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

bool VideoPlayer::init(const std::string& playlistDir) {
    mpv_ = mpv_create();
    if (!mpv_) {
        std::fprintf(stderr, "qypr-lock: mpv_create failed\n");
        return false;
    }

    mpv_set_option_string(mpv_, "vo", "libmpv");  // required by the render API
    mpv_set_option_string(mpv_, "audio", "no");
    mpv_set_option_string(mpv_, "loop-playlist", "inf");
    mpv_set_option_string(mpv_, "shuffle", "yes");
    mpv_set_option_string(mpv_, "hwdec", "no");       // SW render reads CPU frames
    mpv_set_option_string(mpv_, "sw-fast", "yes");    // faster CPU scaling path
    mpv_set_option_string(mpv_, "keepaspect", "no");  // we handle aspect in draw()

    // Correct order: initialise the core, then create the render context.
    if (mpv_initialize(mpv_) < 0) {
        std::fprintf(stderr, "qypr-lock: mpv_initialize failed\n");
        mpv_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
        mpv_render_param(),
    };
    if (mpv_render_context_create(&renderCtx_, mpv_, params) < 0) {
        std::fprintf(stderr, "qypr-lock: mpv_render_context_create failed\n");
        mpv_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_set_wakeup_callback(mpv_, onWakeup, this);
    mpv_request_log_messages(mpv_, "error");

    std::string dir = playlistDir.empty() ? resolvePlaylistDir() : playlistDir;
    std::string file = dir + "/" + playlistFile();
    std::fprintf(stderr, "qypr-lock: loading playlist %s\n", file.c_str());
    loadPlaylist(file);
    return true;
}

void VideoPlayer::loadPlaylist(const std::string& path) {
    const char* cmd[] = {"loadlist", path.c_str(), nullptr};
    mpv_command_async(mpv_, 0, cmd);
}

void VideoPlayer::start() {
    if (!mpv_ || renderTimer_ >= 0) return;
    renderTimer_ = loop_.addTimer(kFrameIntervalMs, true, [this] { renderFrame(); });
}

void VideoPlayer::pause() {
    if (!mpv_ || paused_) return;
    paused_ = true;
    if (renderTimer_ >= 0) {
        loop_.removeTimer(renderTimer_);
        renderTimer_ = -1;
    }
    int flag = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
}

void VideoPlayer::resume() {
    if (!mpv_ || !paused_) return;
    paused_ = false;
    int flag = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
    if (renderTimer_ < 0)
        renderTimer_ = loop_.addTimer(kFrameIntervalMs, true, [this] { renderFrame(); });
}

void VideoPlayer::stop() {
    if (renderTimer_ >= 0) {
        loop_.removeTimer(renderTimer_);
        renderTimer_ = -1;
    }
    if (renderCtx_) {
        mpv_render_context_free(renderCtx_);
        renderCtx_ = nullptr;
    }
    if (mpv_) {
        mpv_destroy(mpv_);
        mpv_ = nullptr;
    }
    pixels_.clear();
    frameW_ = frameH_ = 0;
    haveContent_ = false;
}

// -----------------------------------------------------------------------------
// mpv events (posted from mpv threads onto the loop thread)
// -----------------------------------------------------------------------------

void VideoPlayer::onWakeup(void* self) {
    static_cast<VideoPlayer*>(self)->handleWakeup();
}

void VideoPlayer::handleWakeup() {
    loop_.post([this] { processEvents(); });
}

void VideoPlayer::processEvents() {
    if (!mpv_) return;
    while (true) {
        mpv_event* ev = mpv_wait_event(mpv_, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto* log = static_cast<mpv_event_log_message*>(ev->data);
            if (log) std::fprintf(stderr, "qypr-lock: [mpv/%s] %s", log->prefix, log->text);
        }
    }
}

// -----------------------------------------------------------------------------
// Render the current frame straight into our CPU buffer
// -----------------------------------------------------------------------------

void VideoPlayer::ensureBuffer() {
    if (!mpv_) return;
    int64_t w = 0, h = 0;
    if (mpv_get_property(mpv_, "dwidth", MPV_FORMAT_INT64, &w) < 0) return;
    if (mpv_get_property(mpv_, "dheight", MPV_FORMAT_INT64, &h) < 0) return;
    if (w <= 0 || h <= 0) return;
    if (w == frameW_ && h == frameH_ && !pixels_.empty()) return;
    frameW_ = static_cast<int>(w);
    frameH_ = static_cast<int>(h);
    pixels_.assign(static_cast<size_t>(frameW_) * frameH_ * 4, 0);
    haveContent_ = false;
    std::fprintf(stderr, "qypr-lock: video buffer %dx%d\n", frameW_, frameH_);
}

void VideoPlayer::renderFrame() {
    if (!renderCtx_) return;
    ensureBuffer();
    if (frameW_ <= 0 || frameH_ <= 0 || pixels_.empty()) return;

    int size[2] = {frameW_, frameH_};
    size_t stride = static_cast<size_t>(frameW_) * 4;
    char format[] = "bgr0";  // B,G,R,X bytes == cairo RGB24 on little-endian
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_SW_SIZE, size},
        {MPV_RENDER_PARAM_SW_FORMAT, format},
        {MPV_RENDER_PARAM_SW_STRIDE, &stride},
        {MPV_RENDER_PARAM_SW_POINTER, pixels_.data()},
        mpv_render_param(),
    };
    if (mpv_render_context_render(renderCtx_, params) == 0) {
        haveContent_ = true;
        host_.invalidate();
    }
}

// -----------------------------------------------------------------------------
// cairo compositing (aspect-fill / cover)
// -----------------------------------------------------------------------------

void VideoPlayer::draw(cairo_t* cr, int outW, int outH) {
    if (!hasFrame()) return;

    double oa = static_cast<double>(outW) / outH;
    double sx, sy, sw, sh;
    if (static_cast<double>(frameW_) / frameH_ > oa) {  // source wider: crop sides
        sh = frameH_;
        sw = sh * oa;
        sx = (frameW_ - sw) / 2.0;
        sy = 0;
    } else {  // source taller: crop top/bottom
        sw = frameW_;
        sh = sw / oa;
        sx = 0;
        sy = (frameH_ - sh) / 2.0;
    }

    cairo_surface_t* img = cairo_image_surface_create_for_data(pixels_.data(), CAIRO_FORMAT_RGB24,
                                                               frameW_, frameH_, frameW_ * 4);

    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, outW, outH);
    cairo_clip(cr);
    cairo_scale(cr, outW / sw, outH / sh);
    cairo_set_source_surface(cr, img, -sx, -sy);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(img);
}

}  // namespace qypr
