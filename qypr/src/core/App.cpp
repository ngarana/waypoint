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
    palette_ = theme::AutoPalette::fromConfig(config, theme::localHourNow());
    applyTheme(config);

    if (!display_.connect()) {
        std::fprintf(stderr, "qypr-lock: no Wayland display or no ext-session-lock support\n");
        return 1;
    }

    display_.setInputSink(&shell_);
    display_.setRenderFn([this](cairo_t* cr, int w, int h, int s) { shell_.draw(cr, w, h, s); });
    display_.setAnimatingFn([this] { return shell_.isAnimating(); });

    lock_.setOnFinished([this] {
        std::fprintf(stderr, "qypr-lock: session lock refused or lost\n");
        loop_.quit();
    });

    if (!lock_.lock()) {
        std::fprintf(stderr, "qypr-lock: failed to acquire session lock\n");
        return 1;
    }

    // Start the notification monitor, seeded with the pre-lock backlog from a
    // running --record service (non-fatal: it logs when unavailable).
    notifications_.start(/*seedFromLog=*/true);

    // Status bar backends: one startup fetch, push-only afterwards
    // (non-fatal: the affected indicator stays hidden).
    battery_.start();
    brightness_.start();
    wifi_.start();
    bluetooth_.start();
    volume_.start();
    sni_.start();

    // Location for the solar auto-palette, same as the bar. A sunrise during
    // a long lock re-themes the screen; absent/denied GeoClue never fires.
    geoClue_.setOnChange([this, &config] {
        refreshSolarTimes();
        if (palette_.tick(theme::localHourNow())) {
            applyTheme(config);
            invalidate();
        }
    });
    geoClue_.start();
    refreshSolarTimes();
    if (palette_.mode == "auto") {
        loop_.addTimer(60'000, /*repeat=*/true, [this, &config] {
            refreshSolarTimes();
            if (palette_.tick(theme::localHourNow())) {
                applyTheme(config);
                invalidate();
            }
        });
    }

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
    shell_.setNotifications(demoNotifications());  // sample cards, preview only
    battery_.start();     // live status bar state: battery, backlight, WiFi,
    brightness_.start();  // Bluetooth, volume, and the SNI tray
    wifi_.start();
    bluetooth_.start();
    volume_.start();
    sni_.start();

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
    loop_.addTimer(seconds * 1000, false, [this] { loop_.quit(); });

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

void App::applyTheme(const Config& config) {
    theme_ = theme::loadThemeState(config, palette_);
    shell_.setTheme(theme_);
    audio_.setTheme(theme_);
    notifications_.setTheme(theme_);
}

void App::refreshSolarTimes() {
    if (palette_.location != "auto") {
        palette_.clearSolarTimes();
        return;
    }
    const auto& fix = geoClue_.fix();
    if (!fix) {
        palette_.clearSolarTimes();
        return;
    }
    const auto times =
        solarTimesForDate(fix->latitude, fix->longitude, localDateNow(), localTzOffsetMin());
    if (!times) {
        palette_.clearSolarTimes();  // polar day/night: fixed hours carry the mode
        return;
    }
    palette_.setSolarTimes(times->sunriseMin, times->sunsetMin);
}

void App::invalidate() {
    display_.invalidateAll();
}

void App::requestUnlock() {
    lock_.unlock();
    loop_.quit();
}

}  // namespace qypr
