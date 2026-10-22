#include "ui/AudioController.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "mpris/MprisController.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
constexpr const char* kAudioIconFamily = "Noto Sans";  // colourless media glyphs

TextStyle smallMuted(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.audio.compactTimeSize),
            PANGO_WEIGHT_MEDIUM, theme.colors.textMuted};
}
TextStyle compactTitleStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.audio.compactTitleSize), PANGO_WEIGHT_BOLD,
            theme.colors.text};
}
TextStyle compactSubStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.audio.compactSubSize), PANGO_WEIGHT_NORMAL,
            theme.colors.textSubtle};
}

std::string formatArtistAlbum(const std::string& artist, const std::string& album) {
    std::string out = artist;
    if (!album.empty()) out += (out.empty() ? "" : " — ") + album;
    return out;
}

std::string formatTime(double seconds) {
    seconds = std::max<double>(seconds, 0);
    int total = static_cast<int>(seconds);
    std::array<char, 16> buf{};
    std::snprintf(buf.data(), buf.size(), "%d:%02d", total / 60, total % 60);
    return buf.data();
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
    // the live copy via setTheme(). Qualified to document that the ctor
    // intentionally binds this class's own override.
    AudioController::setTheme(theme::kDefaultState);
}

void AudioController::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    for (ActionButton* b : {&prev_, &playPause_, &next_}) {
        b->setTheme(state);
        b->diameter = state.audio.compactButtonSize;
        b->iconSize = state.audio.compactIconSize;
    }
}

bool AudioController::active() const {
    return mpris_.active();
}
void AudioController::refresh() {
    mpris_.refresh();
}

void AudioController::draw(Painter& p, int64_t now, const Rect& area) {
    const theme::State& t = theme();
    const std::string title = mpris_.title().empty() ? "Unknown Track" : mpris_.title();
    const std::string artist = formatArtistAlbum(mpris_.artist(), mpris_.album());
    const std::string sub = artist.empty() ? "" : "— " + artist;

    playPause_.icon = mpris_.playing() ? "⏸" : "▶";  // ⏸ / ▶
    prev_.icon = "⏮";                                // ⏮
    next_.icon = "⏭";                                // ⏭
    prev_.enabled = mpris_.canGoPrevious();
    playPause_.enabled = mpris_.canTogglePlaying();
    next_.enabled = mpris_.canGoNext();

    // Transport cluster on the right, vertically centred in the strip. The
    // cluster leaves a gutter to the power pill so the two control groups do
    // not read as one row.
    const auto buttonSize = static_cast<double>(t.audio.compactButtonSize);
    const auto transportGap = static_cast<double>(t.spacing.small);
    const double clusterW = (3 * buttonSize) + (2 * transportGap);
    const double clusterRight = area.x + area.w - t.spacing.large;
    const double by = area.y + ((area.h - buttonSize) / 2.0);
    double bx = clusterRight - clusterW;
    for (ActionButton* b : {&prev_, &playPause_, &next_}) {
        b->bounds = {bx, by, buttonSize, buttonSize};
        b->draw(p, now);
        bx += buttonSize + transportGap;
    }

    const double textW = std::max(0.0, (clusterRight - area.x) - clusterW - t.spacing.medium);

    // Title and artist share one line; the metadata captions and the volume
    // slider stay in the quick-settings world.
    const double titleY = area.y + 8.0;
    const TextStyle titleStyle = compactTitleStyle(t);
    const TextStyle subStyle = compactSubStyle(t);
    const Size titleSize = p.measureText(title, titleStyle, textW);
    p.drawText(area.x, titleY, title, titleStyle, HAlign::Left, textW);
    const Size subSize = p.measureText(sub, subStyle, textW);
    const double subX = area.x + titleSize.w + t.spacing.small;
    const double subW = textW - titleSize.w - t.spacing.small;
    if (!sub.empty() && subW > 12.0) {
        p.drawText(subX, titleY + (titleSize.h - subSize.h), sub, subStyle, HAlign::Left, subW);
    }

    // Progress line: elapsed, track, duration. Without a duration the strip
    // shows the same LIVE marker the panel did.
    const TextStyle muted = smallMuted(t);
    const Size sample = p.measureText("0:00", muted);
    const double progressY = area.y + area.h - 8.0 - sample.h;
    if (mpris_.durationSeconds() > 0) {
        const std::string elapsed = formatTime(mpris_.positionSeconds());
        const std::string total = formatTime(mpris_.durationSeconds());
        const Size elapsedSize = p.measureText(elapsed, muted);
        const Size totalSize = p.measureText(total, muted);
        p.drawText(area.x, progressY, elapsed, muted, HAlign::Left);
        p.drawText(area.x + textW, progressY, total, muted, HAlign::Right);

        const double trackX = area.x + elapsedSize.w + t.spacing.small;
        const double trackW =
            std::max(0.0, textW - elapsedSize.w - totalSize.w - (2 * t.spacing.small));
        const auto trackH = static_cast<double>(t.audio.progressHeight);
        const Rect track{trackX, progressY + ((sample.h - trackH) / 2.0), trackW, trackH};
        p.fillRoundedRect(track, trackH / 2.0, t.colors.glassBorder);
        const double ratio = clamp01(mpris_.positionSeconds() / mpris_.durationSeconds());
        if (ratio > 0.0) {
            p.fillRoundedRect({track.x, track.y, track.w * ratio, track.h}, track.h / 2.0,
                              t.colors.primary);
        }
    } else {
        const double cy = progressY + (sample.h / 2.0);
        p.fillCircle(area.x + 3.5, cy, 3.5, t.colors.error);
        const TextStyle live{t.font.family, static_cast<double>(t.audio.compactLiveSize),
                             PANGO_WEIGHT_BOLD, t.colors.error};
        p.drawText(area.x + 12.0, progressY, "LIVE", live, HAlign::Left);
    }
}

bool AudioController::handlePress(double x, double y, int64_t /*now*/) {
    if (!active()) return false;
    const std::array<ActionButton*, 3> buttons = {&prev_, &playPause_, &next_};
    const auto* const hit =
        std::ranges::find_if(buttons, [x, y](const ActionButton* b) { return b->contains(x, y); });
    if (hit == buttons.end()) { return false; }
    (*hit)->click();
    return true;
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
