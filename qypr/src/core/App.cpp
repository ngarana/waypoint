#include "core/App.hpp"

#include <cairo/cairo.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "core/Config.hpp"
#include "core/SolarCalc.hpp"
#include "ui/Notification.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
void renderToPng(qypr::Shell& shell, const std::string& path, int w, int h) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    shell.draw(cr, w, h, 1);
    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, path.c_str());
    cairo_surface_destroy(surface);
}
}  // namespace

App::App()
    : display_(loop_),
      lock_(display_),
      pam_(loop_),
      audio_(mpris_),
      video_(loop_, *this),
      shell_(loop_, *this, pam_, power_, backends_) {
    // Lock-process hardening (QL-3): no core dump of the heap that holds the
    // password, and no same-uid ptrace attach to read it out. /proc/<pid>/* turns
    // root-owned, which blocks other processes but not this one (a process may
    // always read its own /proc/self — libmpv and libpulse are unaffected).
    if (prctl(PR_SET_DUMPABLE, 0) != 0) {
        std::fprintf(stderr, "qypr-lock: PR_SET_DUMPABLE failed; core dumps stay enabled\n");
    }

    // The lock screen never registers a BlueZ pairing agent: nothing in a locked
    // session should be able to answer a pairing prompt, or to render one inside
    // the device picker. The picker is denied wholesale by the lock-screen
    // interaction policy; this is the second half of QL-1.
    bluetooth_.setPairingAgentEnabled(false);

    shell_.setAudioController(&audio_);
    shell_.setVideoPlayer(&video_);
    shell_.setNotificationDismissHandler(
        [this](const Notification& note) { notificationActions_.close(note.daemonId); });

    // Live notifications are pushed into the UI as the monitor sees them.
    // No fallback data on the real lock screen — samples are preview-only.
    notifications_.setOnChange([this] {
        shell_.setNotifications(notifications_.notifications());
        invalidate();
    });
}

int App::run() {
    // Repo-wide theme: the lock follows the same palette as the bar —
    // solar auto-switch included — instead of the compiled defaults. A
    // missing config keeps those defaults; the solar fallback (fixed hours)
    // covers a missing/denied GeoClue on top.
    Config config;
    if (config.load()) { std::fprintf(stderr, "qypr-lock: config %s\n", config.path().c_str()); }

    themeRuntime_.init(
        config,
        [this](const theme::State& s) {
            shell_.setTheme(s);
            audio_.setTheme(s);
            notifications_.setTheme(s);
        },
        [this] { invalidate(); });

    if (!lockRuntime_.acquire(
            &shell_, [this](cairo_t* cr, int w, int h, int s) { shell_.draw(cr, w, h, s); },
            [this] { return shell_.isAnimating(); })) {
        return 1;
    }

    lockRuntime_.startBackends(backends_, notifications_);

    // Location for the solar auto-palette, same as the bar. A sunrise during
    // a long lock re-themes the screen; absent/denied GeoClue never fires.
    geoClue_.setOnChange([this, &config] {
        if (themeRuntime_.refreshSolarTimes(geoClue_.fix())) {
            themeRuntime_.applyTheme(config);
            invalidate();
        }
    });
    geoClue_.start();
    if (themeRuntime_.refreshSolarTimes(geoClue_.fix())) { themeRuntime_.applyTheme(config); }
    if (themeRuntime_.palette().mode == "auto") { themeRuntime_.startMinuteTimer(config); }

    // Start video playback (non-fatal if it fails).
    if (video_.init()) {
        video_.start();
    } else {
        std::fprintf(stderr, "qypr-lock: video background unavailable\n");
    }

    loop_.run();
    return 0;
}

int App::preview(const std::string& path, int width, int height) {
    Config config;
    config.load();
    themeRuntime_.init(config, [this](const theme::State& s) {
        shell_.setTheme(s);
        audio_.setTheme(s);
        notifications_.setTheme(s);
    });
    shell_.setNotifications(demoNotifications());  // sample cards, preview only
    lockRuntime_.startBackends(backends_, notifications_);

    // Volume connects asynchronously and the tray host fetches items over the
    // session bus; pump the loop briefly so the preview renders real state
    // (the locked app runs the loop anyway).
    loop_.addTimer(400, false, [this] { loop_.quit(); });
    loop_.run();

    // Idle state (before any interaction).
    renderToPng(shell_, path.substr(0, path.rfind('.')) + "-idle.png", width, height);

    // Revealed state: simulate typing, then let the reveal animation finish.
    mpris_.refresh();  // surface the audio panel if something is playing
    shell_.lockScreen().handleTextInput("password");
    usleep(700 * 1000);
    renderToPng(shell_, path, width, height);

    // Quick Settings open: click the right group chip in the status bar.
    shell_.onPointerButton(width, height, width - 180.0, 42.0, 0x110, true);
    usleep(300 * 1000);
    renderToPng(shell_, path.substr(0, path.rfind('.')) + "-qs.png", width, height);

    // DND tile (row 2, col 1 of the toggle grid): Shell suppresses the demo
    // cards and the moon icon appears in the bar.
    // Panel x = w-48-380; tile centre: x+16+84, y = 66+16+64+10+32.
    shell_.onPointerButton(width, height, width - 328.0, 188.0, 0x110, true);
    usleep(400 * 1000);
    renderToPng(shell_, path.substr(0, path.rfind('.')) + "-dnd.png", width, height);
    shell_.onPointerButton(width, height, width - 328.0, 188.0, 0x110, true);  // restore
    usleep(400 * 1000);

    // Dismiss the panel by clicking outside it (consumed by the status bar).
    shell_.onPointerButton(width, height, width / 2.0, height / 2.0, 0x110, true);
    usleep(250 * 1000);

    // Expanded power pill: simulate a click on the anchor button (bottom-right).
    shell_.lockScreen().handlePointerButton(width, height,
                                            width - 48.0 - 26.0,  // xlarge=48, button radius=26
                                            height - 48.0 - 26.0, 0x110, true);
    usleep(400 * 1000);
    renderToPng(shell_, path.substr(0, path.rfind('.')) + "-power.png", width, height);

    // Power confirmation popover: click the topmost action button (Suspend).
    // Its centre is at pillar centre X, and one button-height from the top of the pill.
    // pill width = 52+8*2=68, right edge = width-48, so centre X = width-48-34 = width-82
    // button 0 centre Y: bottom - 48 - (4*(52+16)+52+8) - 8 + 26 ... easier to just
    // click near where it should be after the expand animation settles.
    {
        const double pillCx = width - 48.0 - 34.0;  // pill horizontal centre
        const double pillBottom = height - 48.0;
        // button 0 is topmost: col.y + pad + d/2
        // fullH = 5*52 + 4*16 + 8*2 = 260+64+16 = 340
        // col.y = bottom - fullH = pillBottom - 340
        const double colY = pillBottom - 340.0;
        const double btn0cy = colY + 8.0 + 26.0;  // kPillPad + d/2
        shell_.lockScreen().handlePointerButton(width, height, pillCx, btn0cy, 0x110, true);
    }
    usleep(400 * 1000);
    renderToPng(shell_, path.substr(0, path.rfind('.')) + "-confirm.png", width, height);

    std::fprintf(stderr, "qypr-lock: wrote preview frames near %s\n", path.c_str());
    return 0;
}

int App::videoTest(int seconds) {
    if (!display_.connect()) {
        std::fprintf(stderr, "video-test: no Wayland display\n");
        return 1;
    }
    if (!video_.init()) {
        std::fprintf(stderr, "video-test: init FAILED\n");
        return 1;
    }
    video_.start();

    // Heartbeat: report frame status once a second.
    loop_.addTimer(1000, true, [this] {
        std::fprintf(stderr, "video-test: hasFrame=%d\n", video_.hasFrame() ? 1 : 0);
    });
    loop_.addTimer(static_cast<int64_t>(seconds) * 1000, false, [this] { loop_.quit(); });

    loop_.run();
    std::fprintf(stderr, "video-test: DONE hasFrame=%d\n", video_.hasFrame() ? 1 : 0);

    // Dump one composited frame so orientation/colour can be eyeballed.
    if (video_.hasFrame()) {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(s);
        video_.draw(cr, 1920, 1080);
        cairo_destroy(cr);
        cairo_surface_write_to_png(s, "/tmp/qypr-videoframe.png");
        cairo_surface_destroy(s);
        std::fprintf(stderr, "video-test: wrote /tmp/qypr-videoframe.png\n");
    }
    return video_.hasFrame() ? 0 : 2;
}

void App::setIdleTimeout(int seconds) {
    if (seconds > 0) { shell_.setIdleTimeout(static_cast<int64_t>(seconds) * 1000); }
}

void App::invalidate() {
    display_.invalidateAll();
}

void App::requestUnlock() {
    lockRuntime_.unlockAndQuit();
}

}  // namespace qypr
