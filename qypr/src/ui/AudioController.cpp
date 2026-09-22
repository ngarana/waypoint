#include "ui/AudioController.hpp"

#include <algorithm>
#include <cstdio>

#include "mpris/MprisController.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
constexpr const char* kAudioIconFamily = "Noto Sans";  // colourless media glyphs

TextStyle smallMuted(const theme::State& theme) {
    return {theme.font.family, 11.0, PANGO_WEIGHT_MEDIUM, theme.colors.textMuted};
}
TextStyle titleStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeLarge), PANGO_WEIGHT_BOLD,
            theme.colors.text};
}
TextStyle subtitleStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.size), PANGO_WEIGHT_NORMAL,
            theme.colors.textSubtle};
}

std::string formatArtistAlbum(const std::string& artist, const std::string& album) {
    std::string out = artist;
    if (!album.empty()) out += (out.empty() ? "" : " — ") + album;
    return out.empty() ? "Unknown Artist" : out;
}

std::string formatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}
}  // namespace

AudioController::AudioController(MprisController& mpris) : mpris_(mpris) {
    for (ActionButton* b : {&prev_, &playPause_, &next_}) { b->iconFamily = kAudioIconFamily; }
    prev_.label = "Previous";
    playPause_.label = "Play/Pause";
    next_.label = "Next";
    prev_.onClick = [this] {
        mpris_.previous();
    };
    playPause_.onClick = [this] {
        mpris_.togglePlaying();
    };
    next_.onClick = [this] {
        mpris_.next();
    };
    // Deterministic pre-bind state (compiled defaults); the owner re-binds
    // the live copy via setTheme().
    setTheme(theme::kDefaultState);
}

void AudioController::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    for (ActionButton* b : {&prev_, &playPause_, &next_}) {
        b->setTheme(state);
        b->diameter = state.audio.buttonSize;
        b->iconSize = state.audio.buttonIconSize;
    }
}

bool AudioController::active() const {
    return mpris_.active();
}
void AudioController::refresh() {
    mpris_.refresh();
}

void AudioController::draw(Painter& p, int64_t now, double centerX, double topY, double maxWidth) {
    const double panelW = std::clamp(maxWidth, static_cast<double>(theme().audio.minWidth),
                                     static_cast<double>(theme().audio.maxWidth));
    const double pad = theme().audio.panelPadding;
    const double sp = theme().audio.spacing;
    const double cw = panelW - 2 * pad;

    const bool hasDuration = mpris_.durationSeconds() > 0;
    const std::string source = mpris_.sourceLabel();
    const std::string title = mpris_.title().empty() ? "Unknown Track" : mpris_.title();
    const std::string sub = formatArtistAlbum(mpris_.artist(), mpris_.album());
    const bool showVolume = mpris_.canSetVolume();

    // Measure block heights to size the panel before painting its background.
    double hSource = source.empty() ? 0 : p.measureText(source, smallMuted(theme())).h;
    double hNow = p.measureText("Now Playing", smallMuted(theme())).h;
    double hTitle = p.measureText(title, titleStyle(theme()), cw).h;
    double hSub = p.measureText(sub, subtitleStyle(theme()), cw).h;
    double hTime = p.measureText("0:00", smallMuted(theme())).h;
    double hProgress = hasDuration ? (theme().audio.progressHeight + theme().spacing.small + hTime)
                                   : p.measureText("LIVE", smallMuted(theme())).h;
    double hTransport = theme().audio.buttonSize;
    double hVolume = showVolume ? std::max(p.measureText("Vol", smallMuted(theme())).h, 12.0) : 0;

    int blocks = 0;
    double content = 0;
    auto addBlock = [&](double h, bool present = true) {
        if (!present) return;
        content += h;
        ++blocks;
    };
    addBlock(hSource, !source.empty());
    addBlock(hNow);
    addBlock(hTitle);
    addBlock(hSub);
    addBlock(hProgress);
    addBlock(hTransport);
    addBlock(hVolume, showVolume);
    content += sp * std::max(0, blocks - 1);

    const double panelH = content + 2 * pad;
    const double panelX = centerX - panelW / 2.0;
    Rect panel{panelX, topY, panelW, panelH};
    p.fillRoundedRect(panel, theme().radius.large, theme().colors.glass);
    p.strokeRoundedRect(panel, theme().radius.large, theme().colors.glassBorder, 1);

    const double cx0 = panelX + pad;
    double y = topY + pad;
    auto advance = [&](double h) {
        y += h + sp;
    };

    if (!source.empty()) {
        p.drawText(cx0, y, source, smallMuted(theme()), HAlign::Left, cw);
        advance(hSource);
    }
    p.drawText(cx0, y, "Now Playing", smallMuted(theme()), HAlign::Left, cw);
    advance(hNow);
    p.drawText(cx0, y, title, titleStyle(theme()), HAlign::Left, cw);
    advance(hTitle);
    p.drawText(cx0, y, sub, subtitleStyle(theme()), HAlign::Left, cw);
    advance(hSub);

    // Progress bar + times, or a LIVE indicator.
    if (hasDuration) {
        Rect track{cx0, y, cw, static_cast<double>(theme().audio.progressHeight)};
        p.fillRoundedRect(track, track.h / 2, theme().colors.glassBorder);
        double ratio = clamp01(mpris_.positionSeconds() / mpris_.durationSeconds());
        if (ratio > 0)
            p.fillRoundedRect({track.x, track.y, track.w * ratio, track.h}, track.h / 2,
                              theme().colors.primary);
        double ty = y + theme().audio.progressHeight + theme().spacing.small;
        p.drawText(cx0, ty, formatTime(mpris_.positionSeconds()), smallMuted(theme()),
                   HAlign::Left);
        p.drawText(cx0 + cw, ty, formatTime(mpris_.durationSeconds()), smallMuted(theme()),
                   HAlign::Right);
    } else {
        p.fillCircle(cx0 + 4, y + hProgress / 2, 4, theme().colors.error);
        TextStyle live{theme().font.family, 11.0, PANGO_WEIGHT_BOLD, theme().colors.error};
        p.drawText(cx0 + 14, y, "LIVE", live, HAlign::Left);
    }
    advance(hProgress);

    // Transport row (centred).
    playPause_.icon = mpris_.playing() ? "⏸" : "▶";  // ⏸ / ▶
    prev_.icon = "⏮";                                // ⏮
    next_.icon = "⏭";                                // ⏭
    prev_.enabled = mpris_.canGoPrevious();
    playPause_.enabled = mpris_.canTogglePlaying();
    next_.enabled = mpris_.canGoNext();

    const double bd = theme().audio.buttonSize;
    const double totalW = 3 * bd + 2 * sp;
    double bx = centerX - totalW / 2.0;
    ActionButton* buttons[3] = {&prev_, &playPause_, &next_};
    for (auto* b : buttons) {
        b->bounds = {bx, y, bd, bd};
        b->draw(p, now);
        bx += bd + sp;
    }
    advance(hTransport);

    // Volume row.
    if (showVolume) {
        TextStyle lbl = smallMuted(theme());
        Size volLbl = p.measureText("Vol", lbl);
        std::string pct = std::to_string(static_cast<int>(mpris_.volume() * 100 + 0.5)) + "%";
        Size pctSz = p.measureText(pct, lbl);
        double sliderMax = cw - volLbl.w - pctSz.w - 2 * theme().spacing.small;
        double sliderW = std::min(static_cast<double>(theme().audio.volumeSliderWidth), sliderMax);
        double rowMid = y + hVolume / 2.0;

        p.drawText(cx0, rowMid - volLbl.h / 2.0, "Vol", lbl, HAlign::Left);
        double trackX = cx0 + volLbl.w + theme().spacing.small;
        double trackH = theme().audio.progressHeight * 3;
        volumeTrack_ = {trackX, rowMid - trackH / 2.0, sliderW, trackH};
        p.fillRoundedRect(volumeTrack_, trackH / 2, theme().colors.glassBorder);
        double vol = clamp01(mpris_.volume());
        if (vol > 0)
            p.fillRoundedRect({trackX, volumeTrack_.y, sliderW * vol, trackH}, trackH / 2,
                              theme().colors.primary);
        p.drawText(cx0 + cw, rowMid - pctSz.h / 2.0, pct, lbl, HAlign::Right);
    } else {
        volumeTrack_ = {};
    }
}

bool AudioController::handlePress(double x, double y, int64_t now) {
    if (!active()) return false;
    for (ActionButton* b : {&prev_, &playPause_, &next_}) {
        if (b->contains(x, y)) {
            b->click();
            return true;
        }
    }
    if (volumeTrack_.valid() && volumeTrack_.contains(x, y)) {
        dragging_ = true;
        handleDrag(x, y);
        return true;
    }
    return false;
}

void AudioController::handleDrag(double x, double) {
    if (!dragging_ || !volumeTrack_.valid()) return;
    double ratio = clamp01((x - volumeTrack_.x) / volumeTrack_.w);
    mpris_.setVolume(ratio);
}

void AudioController::updateHover(double x, double y, int64_t now) {
    for (ActionButton* b : {&prev_, &playPause_, &next_}) b->setHovered(b->contains(x, y), now);
}

void AudioController::clearHover(int64_t now) {
    for (ActionButton* b : {&prev_, &playPause_, &next_}) b->setHovered(false, now);
}

bool AudioController::animating(int64_t now) const {
    return prev_.animating(now) || playPause_.animating(now) || next_.animating(now);
}

}  // namespace qypr
