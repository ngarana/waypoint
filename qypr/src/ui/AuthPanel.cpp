#include "ui/AuthPanel.hpp"

#include <algorithm>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {

constexpr const char* kFingerprintGlyph = "\uF469";  // Nerd Font fingerprint
constexpr const char* kLockGlyph = "\uF023";         // Nerd Font padlock
constexpr const char* kReturnGlyph = "\u21B5";       // ↵
constexpr const char* kBullet = "\u2022";            // •

constexpr double kBlinkPeriodMs = 1060.0;

TextStyle headerStyle(const theme::State& theme) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeSmall), PANGO_WEIGHT_BOLD,
            theme.colors.primary, 1.4};
}

TextStyle microStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeMicro), PANGO_WEIGHT_MEDIUM,
            theme.colors.textMuted, 1.6};
}

TextStyle nameStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeLarge), PANGO_WEIGHT_MEDIUM,
            theme.colors.text};
}

TextStyle subStyle(const theme::State& theme) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeSmall), PANGO_WEIGHT_NORMAL,
            theme.colors.textMuted, 1.2};
}

TextStyle promptStyle(const theme::State& theme) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeBody), PANGO_WEIGHT_BOLD,
            theme.colors.primary};
}

TextStyle inputStyle(const theme::State& theme) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeTitle), PANGO_WEIGHT_NORMAL,
            theme.colors.text};
}

TextStyle placeholderStyle(const theme::State& theme) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeTitle), PANGO_WEIGHT_NORMAL,
            theme.colors.textMuted.withAlpha(0.8)};
}

TextStyle buttonStyle(const theme::State& theme, bool hovered) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeSmall), PANGO_WEIGHT_BOLD,
            hovered ? theme.colors.primary : theme.colors.textSubtle, 1.4};
}

TextStyle footerStyle(const theme::State& theme) {
    return {theme.font.monoFamily, static_cast<double>(theme.font.sizeMicro), PANGO_WEIGHT_NORMAL,
            theme.colors.textMuted, 1.2};
}

std::string upper(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
    }
    return text;
}

// The biometric tile's colour follows the fingerprint worker: the error
// colour when the reader complained, the accent while it waits, muted when
// there is no reader at all.
Color biometricColor(const theme::State& theme, bool active, bool error) {
    if (error) { return theme.colors.error; }
    if (active) { return theme.colors.primary; }
    return theme.colors.textMuted;
}

}  // namespace

Size AuthPanel::measure(Painter& p, double width) const {
    const theme::State& t = theme();
    const auto& a = t.lock.auth;
    const double pad = t.spacing.large;
    const double innerW = width - (2 * pad);

    const Size header = p.measureText("[ AUTH // PASSWORD ]", headerStyle(t));
    const Size operatorLabel = p.measureText("OPERATOR", microStyle(t));
    const Size name = p.measureText(operatorId.empty() ? "—" : operatorId, nameStyle(t),
                                    innerW - a.tileSize - t.spacing.large);
    const Size subSize = p.measureText(node, subStyle(t), innerW);

    const double height = (2 * pad) + header.h + t.spacing.small + 1.0 + t.spacing.small +
                          operatorLabel.h + a.lineGap + name.h + a.lineGap +
                          (node.empty() ? 0.0 : subSize.h) + t.spacing.medium + a.inputHeight +
                          t.spacing.small + footerStyle(t).size;
    return {width, height};
}

void AuthPanel::draw(Painter& p, int64_t now) {
    const theme::State& t = theme();
    const auto& a = t.lock.auth;
    const double pad = t.spacing.large;
    const double x0 = bounds.x + pad;
    const double innerW = bounds.w - (2 * pad);
    const double nameMaxW = innerW - a.tileSize - t.spacing.large;

    // Card surface: the shared glass treatment (translucent fill, top
    // sheen, hairline border), so the card reads like every other qypr
    // surface instead of a foreign panel.
    p.fillGlass(bounds, t.radius.large, t.colors.glass, t.colors.glassBorder,
                t.style.mode == "solid");

    double y = bounds.y + pad;

    // Header: the auth request on the left, the session state on the right.
    const std::string header = "[ AUTH // PASSWORD ]";
    const Size headerSize = p.measureText(header, headerStyle(t));
    p.drawText(x0, y, header, headerStyle(t), HAlign::Left);
    y += headerSize.h + t.spacing.small;

    p.horizontalGradient({x0, y, innerW, 1.0}, t.colors.glassBorder,
                         t.colors.glassBorder.withAlpha(0.0));
    y += 1.0 + t.spacing.small;

    // Operator identity, with the biometric tile on the right.
    const double operatorTop = y;
    const Size operatorLabel = p.measureText("OPERATOR", microStyle(t));
    p.drawText(x0, y, "OPERATOR", microStyle(t), HAlign::Left);
    y += operatorLabel.h + a.lineGap;

    const std::string name = operatorId.empty() ? std::string("—") : operatorId;
    const Size nameSize = p.measureText(name, nameStyle(t), nameMaxW);
    p.drawText(x0, y, name, nameStyle(t), HAlign::Left, nameMaxW);
    y += nameSize.h + a.lineGap;

    if (!node.empty()) {
        const Size subSize = p.measureText(node, subStyle(t), innerW);
        p.drawText(x0, y, node, subStyle(t), HAlign::Left, innerW);
        y += subSize.h;
    }

    const Rect tile{bounds.x + bounds.w - pad - a.tileSize, operatorTop, a.tileSize, a.tileSize};
    const Color tileColor = biometricColor(t, fingerprintActive, fingerprintError);
    p.fillRoundedRect(tile, t.radius.medium, t.colors.surface.withAlpha(0.35));
    p.strokeRoundedRect(tile, t.radius.medium, tileColor, 1);
    const TextStyle glyphStyle{t.font.iconFamily, static_cast<double>(t.font.sizeIcon),
                               PANGO_WEIGHT_NORMAL, tileColor};
    const char* glyph = fingerprintActive ? kFingerprintGlyph : kLockGlyph;
    const Size glyphSize = p.measureText(glyph, glyphStyle);
    p.drawText(tile.cx() - (glyphSize.w / 2.0), tile.cy() - (glyphSize.h / 2.0), glyph, glyphStyle,
               HAlign::Left);

    y += t.spacing.medium;

    // Input field: a contained row with a prompt, the masked input, a
    // blinking cursor and the inline unlock affordance.
    const double inputY = y;
    const double inputBottom = inputY + a.inputHeight;
    const Rect field{x0, inputY, innerW, a.inputHeight};
    p.fillRoundedRect(field, t.radius.medium, t.colors.surface.withAlpha(0.30));
    p.strokeRoundedRect(field, t.radius.medium, t.colors.glassBorder, 1);

    const Size promptSize = p.measureText(">", promptStyle(t));
    p.drawText(x0 + a.fieldPadX, inputY + (a.inputHeight / 2.0) - (promptSize.h / 2.0), ">",
               promptStyle(t), HAlign::Left);
    const double textX = x0 + a.fieldPadX + promptSize.w + t.spacing.small;

    std::string dots;
    dots.reserve(static_cast<size_t>(std::max(charCount, 0)) * 3);
    for (int i = 0; i < charCount; ++i) { dots += kBullet; }
    const bool empty = charCount == 0;
    const TextStyle inputTextStyle = empty ? placeholderStyle(t) : inputStyle(t);
    const std::string inputText = empty ? "ENTER PASSWORD" : dots;
    unlock_ = {x0 + innerW - a.fieldPadX - a.buttonWidth,
               inputY + ((a.inputHeight - a.buttonHeight) / 2.0), a.buttonWidth, a.buttonHeight};
    const double inputMaxW = unlock_.x - t.spacing.medium - textX;
    const Size inputSize = p.measureText(inputText, inputTextStyle, inputMaxW);
    p.drawText(textX, inputY + (a.inputHeight / 2.0) - (inputSize.h / 2.0), inputText,
               inputTextStyle, HAlign::Left, inputMaxW);

    // Cursor: a notch that blinks like the mock's, trailing the input.
    const auto blinkPeriod = static_cast<int64_t>(kBlinkPeriodMs);
    const bool blinkOn = (now % blinkPeriod) < (blinkPeriod / 2);
    if (blinkOn) {
        const double cursorX = textX + inputSize.w + a.cursorGap;
        p.fillRect({cursorX, inputY + (a.inputHeight / 2.0) - (a.cursorHeight / 2.0), a.cursorWidth,
                    a.cursorHeight},
                   t.colors.primary.withAlpha(0.85));
    }

    // Inline unlock affordance.
    p.fillRoundedRect(unlock_, t.radius.small,
                      unlockHovered_ ? t.colors.primary.withAlpha(0.18)
                                     : t.colors.surface.withAlpha(0.55));
    p.strokeRoundedRect(unlock_, t.radius.small,
                        unlockHovered_ ? t.colors.primary : t.colors.glassBorder, 1);
    const std::string label = std::string("UNLOCK ") + kReturnGlyph;
    const Size labelSize = p.measureText(label, buttonStyle(t, unlockHovered_));
    p.drawText(unlock_.cx() - (labelSize.w / 2.0), unlock_.cy() - (labelSize.h / 2.0), label,
               buttonStyle(t, unlockHovered_), HAlign::Left);

    // Footer: the PAM stack in use, plus the status line.
    const double footerY = inputBottom + t.spacing.small;
    const std::string pam = "PAM " + (pamService.empty() ? std::string("login") : pamService);
    const double footerMid = footerY + (footerStyle(t).size / 2.0);
    p.fillCircle(x0 + a.footerDotRadius, footerMid, a.footerDotRadius, t.colors.textMuted);
    p.drawText(x0 + a.footerTextGap, footerY, upper(pam), footerStyle(t), HAlign::Left);

    if (!status.message.empty()) {
        status.draw(p, bounds.x + bounds.w - pad, footerY, HAlign::Right);
    }
}

}  // namespace qypr
