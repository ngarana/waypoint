// MediaIndicator.cpp - Now-playing applet implementation.
#include "ui/indicators/MediaIndicator.hpp"

#include <array>
#include <vector>

#include "mpris/MprisController.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {

constexpr const char* kPlay = "󰐊";   // nf-md-play
constexpr const char* kPause = "󰏤";  // nf-md-pause
constexpr const char* kPrev = "󰒮";
constexpr const char* kNext = "󰒭";
constexpr double kMenuW = 300.0;
constexpr double kPad = 12.0;

// Cap the bar text; a long track title must not push the zones around. Counts
// UTF-8 codepoints so multibyte titles are never cut mid-character.
std::string truncateUtf8(const std::string& s, size_t maxCps) {
    size_t cps = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = (c < 0x80) ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        if (cps + 1 > maxCps) return s.substr(0, i) + "…";
        i += len;
        ++cps;
    }
    return s;
}

class MediaPopover : public DetailedPopover {
public:
    explicit MediaPopover(MprisController* m) : mpris_(m) {}

    double contentWidth() const override { return kMenuW; }
    double contentHeight() const override { return 132.0; }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        b.y += (growUp ? 1.0 : -1.0) * (1.0 - openProgress_.value(now)) * 6.0;

        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius))
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
        if (!mpris_) return;

        // Source (player identity).
        TextStyle src{theme().font.family, 11.0, PANGO_WEIGHT_BOLD, theme().colors.primary};
        p.drawText(b.x + kPad, b.y + kPad, mpris_->sourceLabel(), src, HAlign::Left,
                   b.w - kPad * 2);

        // Title + artist.
        TextStyle title{theme().font.family, 14.0, PANGO_WEIGHT_NORMAL, theme().colors.text};
        p.drawText(b.x + kPad, b.y + kPad + 16.0, mpris_->title(), title, HAlign::Left,
                   b.w - kPad * 2);
        TextStyle artist{theme().font.family, 12.0, PANGO_WEIGHT_NORMAL, theme().colors.textSubtle};
        p.drawText(b.x + kPad, b.y + kPad + 36.0, mpris_->artist(), artist, HAlign::Left,
                   b.w - kPad * 2);

        // Progress bar (only when the player reports a length).
        const double dur = mpris_->durationSeconds();
        if (dur > 0.0) {
            const double frac = std::min(1.0, std::max(0.0, mpris_->positionSeconds() / dur));
            const Rect track{b.x + kPad, b.y + kPad + 60.0, b.w - kPad * 2, 3.0};
            p.fillRoundedRect(track, 1.5, theme().colors.textSubtle.withAlpha(0.25));
            p.fillRoundedRect({track.x, track.y, track.w * frac, track.h}, 1.5,
                              theme().colors.primary);
        }

        // Transport row: prev / play-pause / next, centred.
        const double cy = b.y + b.h - 26.0;
        const double cx = b.x + b.w / 2.0;
        buttons_.clear();
        struct Btn {
            const char* glyph;
            double dx;
            bool enabled;
        };
        const std::array<Btn, 3> btns{{
            {kPrev, -44.0, mpris_->canGoPrevious()},
            {mpris_->playing() ? kPause : kPlay, 0.0, mpris_->canTogglePlaying()},
            {kNext, 44.0, mpris_->canGoNext()},
        }};
        for (const auto& btn : btns) {
            const Rect r{cx + btn.dx - 16.0, cy - 16.0, 32.0, 32.0};
            buttons_.push_back(r);
            if (r.contains(hoverX_, hoverY_) && btn.enabled) {
                p.fillRoundedRect(r, 16.0, theme().colors.glassHover);
            }
            TextStyle g{theme().font.iconFamily, 17.0, PANGO_WEIGHT_NORMAL,
                        btn.enabled ? theme().colors.text
                                    : theme().colors.textSubtle.withAlpha(0.4)};
            const Size sz = p.measureText(btn.glyph, g);
            p.drawText(r.x + (r.w - sz.w) / 2.0, r.y + (r.h - sz.h) / 2.0, btn.glyph, g);
        }
    }

    bool handleClick(double x, double y) override {
        if (!mpris_ || buttons_.size() < 3) return false;
        if (buttons_[0].contains(x, y)) {
            mpris_->previous();
            return true;
        }
        if (buttons_[1].contains(x, y)) {
            mpris_->togglePlaying();
            return true;
        }
        if (buttons_[2].contains(x, y)) {
            mpris_->next();
            return true;
        }
        return false;
    }

    bool handleDrag(double x, double y) override {
        hoverX_ = x;
        hoverY_ = y;
        return false;
    }

private:
    MprisController* mpris_ = nullptr;
    std::vector<Rect> buttons_;  // rebuilt each draw; hit-tested on click
    double hoverX_ = -1, hoverY_ = -1;
};

}  // namespace

MediaIndicator::MediaIndicator(const SystemBackends& backends)
    : StatusIndicator("media", Zone::Right, 50),
      mpris_(backends.mpris) {
    visible = false;  // hidden until something is actually playing
}

bool MediaIndicator::playing() const {
    return mpris_ && mpris_->playing();
}

std::string MediaIndicator::icon() const {
    return playing() ? kPause : kPlay;
}

std::string MediaIndicator::themedIcon() const {
    return playing() ? "media-playback-pause-symbolic" : "media-playback-start-symbolic";
}

std::string MediaIndicator::label() const {
    if (!mpris_ || !mpris_->active()) return "";
    const std::string& t = mpris_->title();
    const std::string& a = mpris_->artist();
    if (t.empty()) return a.empty() ? mpris_->sourceLabel() : truncateUtf8(a, 28);
    return truncateUtf8(a.empty() ? t : t + " — " + a, 28);
}

std::string MediaIndicator::tooltip() const {
    if (!mpris_ || !mpris_->active()) return "";
    std::string s = mpris_->title();
    if (!mpris_->artist().empty()) s += " — " + mpris_->artist();
    if (!mpris_->sourceLabel().empty()) s += "  (" + mpris_->sourceLabel() + ")";
    return s;
}

Color MediaIndicator::iconColor() const {
    return playing() ? theme().colors.primary : theme().colors.textSubtle;
}

bool MediaIndicator::onClick(double, double) {
    if (!mpris_ || !mpris_->canTogglePlaying()) return false;
    mpris_->togglePlaying();
    return true;  // consumed: a click toggles rather than opening the popover
}

bool MediaIndicator::onScroll(double dx, double dy, double x, double y) {
    (void)x;
    (void)y;
    if (!mpris_) return false;
    const double d = dy != 0.0 ? dy : dx;
    if (d < 0 && mpris_->canGoNext()) {
        mpris_->next();
        return true;
    }
    if (d > 0 && mpris_->canGoPrevious()) {
        mpris_->previous();
        return true;
    }
    return false;
}

void MediaIndicator::onBackendUpdate() {
    visible = mpris_ && mpris_->available() && mpris_->active();
}

std::unique_ptr<DetailedPopover> MediaIndicator::createDetailedView() {
    return std::make_unique<MediaPopover>(mpris_);
}

REGISTER_INDICATOR("media", Zone::Right, 50, MediaIndicator)

}  // namespace qypr
