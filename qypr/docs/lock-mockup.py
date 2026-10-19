#!/usr/bin/env python3
"""qypr-lock redesign mockups.

Ports the qypr Painter/Theme semantics to pycairo so the CURRENT lock screen
can be re-rendered over a real wallpaper frame and compared, apples-to-apples,
with the PROPOSED design.

Sources of truth mirrored here:
  common/render/Painter.cpp   (fillGlass, drawTextShadowed, roundedRectPath)
  qypr/src/ui/Theme.hpp        (palette + spacing)
  qypr/src/ui/LockLayout.cpp   (current geometry)
  qypr/src/ui/Notification.cpp (current card geometry)
  qypr/src/ui/PasswordField.cpp(current pill geometry)
"""

import math
import os
import sys

import cairo
import gi

gi.require_version("PangoCairo", "1.0")
from gi.repository import Pango, PangoCairo  # noqa: E402

W, H = 1920, 1080

# --------------------------------------------------------------------------
# Palette (matugen solarized-dark, as applied by ~/.config/qypr/matugen.css)
# --------------------------------------------------------------------------
BG = (0x00, 0x2B, 0x36, 1.0)
SURFACE = (0x07, 0x36, 0x42, 1.0)
SURFACE_HOVER = (0x07, 0x36, 0x42, 1.0)
GLASS = (0x18, 0x1A, 0x24, 0.65)
GLASS_HOVER = (0x22, 0x25, 0x34, 0.75)
GLASS_BORDER = (0xFF, 0xFF, 0xFF, 0.08)
PRIMARY = (0x26, 0x8B, 0xD2, 1.0)
PRIMARY_GLOW = (0x26, 0x8B, 0xD2, 0.25)
TEXT = (0x83, 0x94, 0x96, 1.0)
TEXT_SUBTLE = (0x93, 0xA1, 0xA1, 1.0)
TEXT_MUTED = (0x93, 0xA1, 0xA1, 1.0)
ERROR = (0xDC, 0x32, 0x2F, 1.0)

SP_SMALL, SP_MED, SP_LARGE, SP_XLARGE = 8, 16, 24, 48
FONT = "Inter"  # not installed -> Noto Sans
FONT_ICON = "CaskaydiaCove Nerd Font"

SIZE_CLOCK, SIZE_DATE = 96, 20
BAR_H, BAR_TOP, BAR_SIDE, BAR_RADIUS = 36.0, 24.0, 48.0, 14.0


def rgba(c):
    return (c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, c[3])


# --------------------------------------------------------------------------
# Painter port
# --------------------------------------------------------------------------
class Painter:
    def __init__(self, cr):
        self.cr = cr

    def _src(self, c):
        self.cr.set_source_rgba(*rgba(c))

    def fill_rect(self, x, y, w, h, c):
        self._src(c)
        self.cr.rectangle(x, y, w, h)
        self.cr.fill()

    def rounded_path(self, x, y, w, h, radius):
        rad = min(radius, min(w, h) / 2.0)
        deg = math.pi / 180.0
        cr = self.cr
        cr.new_sub_path()
        cr.arc(x + w - rad, y + rad, rad, -90 * deg, 0)
        cr.arc(x + w - rad, y + h - rad, rad, 0, 90 * deg)
        cr.arc(x + rad, y + h - rad, rad, 90 * deg, 180 * deg)
        cr.arc(x + rad, y + rad, rad, 180 * deg, 270 * deg)
        cr.close_path()

    def fill_rounded(self, x, y, w, h, radius, c):
        self.rounded_path(x, y, w, h, radius)
        self._src(c)
        self.cr.fill()

    def stroke_rounded(self, x, y, w, h, radius, c, lw=1.0):
        self.rounded_path(x + lw / 2, y + lw / 2, w - lw, h - lw, radius)
        self._src(c)
        self.cr.set_line_width(lw)
        self.cr.stroke()

    def fill_glass(self, x, y, w, h, radius, base, border, solid=False):
        if solid:
            b = (base[0], base[1], base[2], 1.0)
            self.fill_rounded(x, y, w, h, radius, b)
            self.stroke_rounded(x, y, w, h, radius, border, 1.0)
            return
        rad = min(radius, min(w, h) / 2.0)
        self.rounded_path(x, y, w, h, radius)
        self._src(base)
        self.cr.fill()

        self.cr.save()
        self.rounded_path(x, y, w, h, radius)
        self.cr.clip()
        sheen = cairo.LinearGradient(0, y, 0, y + h)
        sheen.add_color_stop_rgba(0.0, 1, 1, 1, 0.10)
        sheen.add_color_stop_rgba(0.35, 1, 1, 1, 0.02)
        sheen.add_color_stop_rgba(1.0, 1, 1, 1, 0.0)
        self.cr.set_source(sheen)
        self.cr.rectangle(x, y, w, h)
        self.cr.fill()
        self.cr.set_source_rgba(1, 1, 1, 0.14)
        self.cr.set_line_width(1.0)
        self.cr.move_to(x + rad, y + 0.5)
        self.cr.line_to(x + w - rad, y + 0.5)
        self.cr.stroke()
        self.cr.restore()

        self.stroke_rounded(x, y, w, h, radius, border, 1.0)

    def fill_circle(self, cx, cy, r, c):
        self._src(c)
        self.cr.arc(cx, cy, r, 0, 2 * math.pi)
        self.cr.fill()

    def stroke_circle(self, cx, cy, r, c, lw=1.0):
        self._src(c)
        self.cr.set_line_width(lw)
        self.cr.arc(cx, cy, r - lw / 2, 0, 2 * math.pi)
        self.cr.stroke()

    # -- text ------------------------------------------------------------
    def _layout(self, text, family, size, weight, max_width=-1.0):
        layout = PangoCairo.create_layout(self.cr)
        desc = Pango.FontDescription()
        desc.set_family(family)
        desc.set_absolute_size(size * Pango.SCALE)
        desc.set_weight(weight)
        layout.set_font_description(desc)
        layout.set_text(text, -1)
        if max_width > 0:
            layout.set_width(int(max_width * Pango.SCALE))
            layout.set_ellipsize(Pango.EllipsizeMode.END)
        return layout

    def _tracked(self, text, family, size, weight):
        """Per-glyph advances, so letter-spacing can be applied by hand
        (Pango's letter_spacing attribute is not settable from pygobject)."""
        widths = []
        h = 0
        for ch in text:
            lay = self._layout(ch, family, size, weight)
            w, ch_h = lay.get_pixel_size()
            widths.append(w)
            h = max(h, ch_h)
        return widths, h

    def measure(self, text, family, size, weight, max_width=-1.0):
        lay = self._layout(text, family, size, weight, max_width)
        w, h = lay.get_pixel_size()
        return w, h

    def ink_size(self, text, family, size, weight):
        """Tight glyph box in pixels: (offset_y, width, height).

        pango_layout_get_extents() returns the tight glyph rect first and the
        line-box rect second; the tight one is what the composition needs.
        """
        lay = self._layout(text, family, size, weight)
        ink, _box = lay.get_extents()
        return ink.y / Pango.SCALE, ink.width / Pango.SCALE, ink.height / Pango.SCALE

    def _draw_tracked(self, x, y, text, family, size, weight, color, align, spacing, offset):
        widths, _h = self._tracked(text, family, size, weight)
        total = sum(widths) + spacing * max(0, len(text) - 1)
        if align == "center":
            ax = x - total / 2.0
        elif align == "right":
            ax = x - total
        else:
            ax = x
        for i, ch in enumerate(text):
            lay = self._layout(ch, family, size, weight)
            self.cr.move_to(ax, y + offset)
            self._src(color)
            PangoCairo.show_layout(self.cr, lay)
            ax += widths[i] + spacing

    def draw_text(self, x, y, text, family, size, weight, color, align="left", max_width=-1.0,
                  spacing=0):
        if spacing:
            self._draw_tracked(x, y, text, family, size, weight, color, align, spacing, 0)
            return
        lay = self._layout(text, family, size, weight, max_width)
        w, h = lay.get_pixel_size()
        if align == "center":
            ax = x - w / 2.0
        elif align == "right":
            ax = x - w
        else:
            ax = x
        self.cr.move_to(ax, y)
        self._src(color)
        PangoCairo.show_layout(self.cr, lay)

    def draw_text_shadowed(self, x, y, text, family, size, weight, color, align="left",
                           shadow_alpha=0.6, shadow_offset=2, spacing=0):
        if spacing:
            if shadow_alpha > 0:
                self._draw_tracked(x, y, text, family, size, weight, (0, 0, 0, shadow_alpha),
                                   align, spacing, shadow_offset)
            self._draw_tracked(x, y, text, family, size, weight, color, align, spacing, 0)
            return
        lay = self._layout(text, family, size, weight, -1)
        w, h = lay.get_pixel_size()
        ax = x - w / 2.0 if align == "center" else (x - w if align == "right" else x)
        if shadow_alpha > 0:
            self.cr.move_to(ax + shadow_offset, y + shadow_offset)
            self.cr.set_source_rgba(0, 0, 0, shadow_alpha)
            PangoCairo.show_layout(self.cr, lay)
        self.cr.move_to(ax, y)
        self._src(color)
        PangoCairo.show_layout(self.cr, lay)

    def with_alpha(self, alpha, fn):
        if alpha <= 0.01:
            return
        self.cr.push_group()
        fn()
        self.cr.pop_group_to_source()
        self.cr.paint_with_alpha(alpha)


# --------------------------------------------------------------------------
# Wallpaper
# --------------------------------------------------------------------------
def draw_wallpaper(cr, path):
    surf = cairo.ImageSurface.create_from_png(path)
    sw, sh = surf.get_width(), surf.get_height()
    scale = max(W / sw, H / sh)
    dw, dh = sw * scale, sh * scale
    dx, dy = (W - dw) / 2.0, (H - dh) / 2.0
    cr.save()
    cr.rectangle(0, 0, W, H)
    cr.clip()
    cr.translate(dx, dy)
    cr.scale(scale, scale)
    cr.set_source_surface(surf, 0, 0)
    cr.paint()
    cr.restore()


# --------------------------------------------------------------------------
# CURRENT design (faithful port of the shipped code)
# --------------------------------------------------------------------------
def draw_status_bar(p, cr, alpha=1.0):
    """Chromeless-ish solid strip, as qypr renders it on the lock screen."""
    x, y = BAR_SIDE, BAR_TOP
    w, h = W - 2 * BAR_SIDE, BAR_H
    def strip():
        p.fill_rounded(x, y, w, h, BAR_RADIUS, (SURFACE[0], SURFACE[1], SURFACE[2], 1.0))
        p.stroke_rounded(x, y, w, h, BAR_RADIUS, GLASS_BORDER, 1)
        p.draw_text(x + 18, y + 9, "10:44 PM", FONT, 15, Pango.Weight.NORMAL, TEXT)

    p.with_alpha(alpha, strip)
    icons = [("󰌶", 0.55), ("󰕾", 0.55), ("󰤨", 0.55), ("󰂯", 0.55), ("󰁹", 0.85)]
    ix = x + w - 24
    for glyph, a in reversed(icons):
        p.with_alpha(alpha * a, lambda g=glyph, cx=ix: p.draw_text(
            cx, y + 9, g, FONT_ICON, 18, Pango.Weight.NORMAL, TEXT, align="right"))
        ix -= 40


def draw_current(p, cr, state):
    reveal = 0.0 if state == "idle" else 1.0

    # 1. wallpaper
    draw_wallpaper(cr, WALLPAPER)

    # 2. flat veil (Shell.cpp)
    p.fill_rect(0, 0, W, H, (0, 0, 0, 0.15 + 0.20 * reveal))

    # 3. status bar strip (dimmed while idle, like Shell::draw)
    draw_status_bar(p, cr, alpha=0.4 + 0.6 * reveal)

    # 4. clock + date (LockLayout::compute / Clock::draw)
    cx = W / 2.0
    clock_top = H * 0.18
    tw, th = p.measure("22:44", FONT, SIZE_CLOCK, Pango.Weight.BOLD)
    p.draw_text_shadowed(cx, clock_top, "22:44", FONT, SIZE_CLOCK, Pango.Weight.BOLD, TEXT,
                         align="center", shadow_alpha=0.6, shadow_offset=2)
    date_y = clock_top + th + SP_SMALL
    p.draw_text_shadowed(cx, date_y, "Monday, October 19", FONT, SIZE_DATE, Pango.Weight.BOLD,
                         TEXT_SUBTLE, align="center", shadow_alpha=0.6, shadow_offset=2)

    # 5. password pill (PasswordField::draw)
    col_w = 420.0
    px = cx - col_w / 2.0
    py = clock_top + th + SP_XLARGE
    ph = 56.0

    def draw_field():
        glow = (px - 3, py - 3, col_w + 6, ph + 6)
        p.stroke_rounded(glow[0], glow[1], glow[2], glow[3], ph / 2.0 + 3, PRIMARY_GLOW, 2)
        p.fill_rounded(px, py, col_w, ph, ph / 2.0, GLASS)
        p.stroke_rounded(px, py, col_w, ph, ph / 2.0, PRIMARY, 1)
        p.draw_text(px + SP_LARGE, py + ph / 2.0 - 11, "", FONT_ICON, 16,
                    Pango.Weight.NORMAL, (TEXT[0], TEXT[1], TEXT[2], 0.7))
        if state == "idle":
            p.draw_text(px + 56, py + ph / 2.0 - 9, "Touch sensor or enter password...",
                        FONT, 16, Pango.Weight.NORMAL, TEXT_MUTED)
        else:
            p.draw_text(px + 56, py + ph / 2.0 - 9, "•" * 8, FONT, 16, Pango.Weight.NORMAL, TEXT)

    p.with_alpha(reveal, draw_field)

    # 6. status message
    status_top = py + ph + SP_SMALL
    if state == "error":
        p.with_alpha(reveal, lambda: p.draw_text_shadowed(
            cx, status_top, "Authentication failed", FONT, 16, Pango.Weight.MEDIUM, ERROR,
            align="center", shadow_alpha=0.6, shadow_offset=1))

    # 7. media card (AudioController)
    audio_top = status_top + 20 + SP_SMALL

    def draw_media():
        ax, ay, aw = cx - 210.0, audio_top, 420.0
        ah = 280.0
        p.fill_rounded(ax, ay, aw, ah, 12, (SURFACE[0], SURFACE[1], SURFACE[2], 0.85))
        p.stroke_rounded(ax, ay, aw, ah, 12, GLASS_BORDER, 1)
        p.draw_text(ax + 16, ay + 16, "Mozilla firefox", FONT, 13, Pango.Weight.NORMAL, TEXT_SUBTLE)
        p.draw_text(ax + 16, ay + 44, "Now Playing", FONT, 13, Pango.Weight.NORMAL, TEXT_SUBTLE)
        p.draw_text(ax + 16, ay + 72, "Mad", FONT, 22, Pango.Weight.BOLD, TEXT)
        p.draw_text(ax + 16, ay + 110, "Ne-Yo - Topic", FONT, 18, Pango.Weight.NORMAL, TEXT_SUBTLE)
        p.fill_rect(ax + 16, ay + 152, aw - 32, 1, (TEXT[0], TEXT[1], TEXT[2], 0.15))
        p.draw_text(ax + 16, ay + 160, "0:00", FONT, 12, Pango.Weight.NORMAL, TEXT_MUTED)
        p.draw_text(ax + aw - 16, ay + 160, "4:14", FONT, 12, Pango.Weight.NORMAL, TEXT_MUTED,
                    align="right")
        for i, g in enumerate(("", "", "")):
            gx = ax + aw / 2 + (i - 1) * 60
            p.stroke_circle(gx, ay + 215, 24, (TEXT[0], TEXT[1], TEXT[2], 0.12), 1)
            p.draw_text(gx, ay + 207, g, FONT_ICON, 16, Pango.Weight.NORMAL,
                        (TEXT[0], TEXT[1], TEXT[2], 0.8), align="center")
        p.draw_text(ax + 16, ay + 248, "Vol", FONT, 12, Pango.Weight.NORMAL, TEXT_MUTED)
        p.fill_rounded(ax + 48, ay + 250, 150, 6, 3, PRIMARY)
        p.draw_text(ax + aw - 16, ay + 248, "100%", FONT, 12, Pango.Weight.NORMAL, TEXT_MUTED,
                    align="right")

    p.with_alpha(reveal, draw_media)

    # 8. notification cards (bottom-left)
    card_w, pad, icon, gap = 360.0, 16.0, 36.0, 12.0
    bottom = H - SP_XLARGE
    notes = [("Claude Code", (0x1E, 0x1E, 0x2E, 1.0), "C"),
             ("Connected", (0xA6, 0xE3, 0xA1, 1.0), "C"),
             ("Claude Code", (0xF5, 0xC2, 0xE7, 1.0), "C"),
             ("Claude Code", (0xF9, 0xE2, 0xAF, 1.0), "C")]
    card_h = 2 * pad + icon
    for i, (title, accent, glyph) in enumerate(notes):
        y = bottom - (i + 1) * card_h - i * gap
        x = SP_XLARGE
        p.fill_rounded(x, y, card_w, card_h, 12, GLASS)
        p.stroke_rounded(x, y, card_w, card_h, 12, GLASS_BORDER, 1)
        p.fill_rounded(x + pad, y + pad, icon, icon, 8, accent)
        p.draw_text(x + pad + icon / 2, y + pad + icon / 2 - 9, glyph, FONT, 18,
                    Pango.Weight.BOLD, TEXT, align="center")
        p.draw_text(x + pad + icon + pad, y + pad + 8, title, FONT, 14, Pango.Weight.BOLD, TEXT)

    # 9. power pill (bottom-right)
    d, spacing, pill_pad = 52.0, 16.0, 8.0
    full_h = (5 * d) + (4 * spacing) + (pill_pad * 2)
    pill_w = d + pill_pad * 2
    right, bottomm = W - SP_XLARGE, H - SP_XLARGE
    col_x, col_y = right - pill_w, bottomm - full_h
    p.fill_rounded(col_x, col_y, pill_w, full_h, 26, GLASS)
    glyphs = ("", "󰒲", "↻", "⏼", "")
    for i, g in enumerate(glyphs):
        gy = col_y + pill_pad + d / 2 + i * (d + spacing)
        p.fill_circle(col_x + pill_w / 2, gy, d / 2, (SURFACE[0], SURFACE[1], SURFACE[2], 0.55))
        p.stroke_circle(col_x + pill_w / 2, gy, d / 2, GLASS_BORDER, 1)
        p.draw_text(col_x + pill_w / 2, gy - 9, g, FONT_ICON, 18, Pango.Weight.NORMAL,
                    (TEXT[0], TEXT[1], TEXT[2], 0.8), align="center")


# --------------------------------------------------------------------------
# PROPOSED design
# --------------------------------------------------------------------------
# Overlay foreground ramp — fixed, not palette-derived: the hero sits on a
# photograph, and the bar's palette text colours are tuned for a solid chip.
OV_TEXT = (0xFF, 0xFF, 0xFF, 0.96)
OV_SUBTLE = (0xFF, 0xFF, 0xFF, 0.62)
OV_MUTED = (0xFF, 0xFF, 0xFF, 0.42)

def draw_scrim(p, cr, margin_x=96.0):
    """Left-anchored cinematic scrim.

    The old design washed the whole frame (a flat 15-35% black) to buy
    legibility, which darkened the picture for nothing. The new one darkens
    only where the type lives — a horizontal falloff from the text edge —
    plus a soft bottom band for the peripheral controls and a light top band
    for the status glyphs.
    """
    # Horizontal falloff: dense at the type edge, gone by 62% of the width.
    g = cairo.LinearGradient(0, 0, W * 0.62, 0)
    g.add_color_stop_rgba(0.0, 0, 0, 0, 0.66)
    g.add_color_stop_rgba(0.45, 0, 0, 0, 0.42)
    g.add_color_stop_rgba(1.0, 0, 0, 0, 0.0)
    cr.set_source(g)
    cr.rectangle(0, 0, W, H)
    cr.fill()

    # Bottom band anchors the notification stack and the power control.
    gb = cairo.LinearGradient(0, H * 0.62, 0, H)
    gb.add_color_stop_rgba(0.0, 0, 0, 0, 0.0)
    gb.add_color_stop_rgba(1.0, 0, 0, 0, 0.55)
    cr.set_source(gb)
    cr.rectangle(0, 0, W, H)
    cr.fill()

    # Top band for the status glyphs.
    gt = cairo.LinearGradient(0, 0, 0, 96)
    gt.add_color_stop_rgba(0.0, 0, 0, 0, 0.42)
    gt.add_color_stop_rgba(1.0, 0, 0, 0, 0.0)
    cr.set_source(gt)
    cr.rectangle(0, 0, W, H)
    cr.fill()


def draw_proposed(p, cr, state):
    """MERIDIAN — flush-left editorial lock screen.

    One alignment axis (x = 96), one type stack, one accent (the palette
    primary). The auth field is a hairline-underlined input, not a capsule.
    """
    reveal = 0.0 if state == "idle" else 1.0
    error = state == "error"
    x = 96.0

    draw_wallpaper(cr, WALLPAPER)
    draw_scrim(p, cr, x)

    # ---- status strip: chromeless glyphs, right-aligned, no duplicate clock --
    icons = [("󰌶", 0.50), ("󰕾", 0.50), ("󰤨", 0.50), ("󰂯", 0.50)]
    ix = W - 64 - 16
    for glyph, a in reversed(icons):
        p.with_alpha(a, lambda g=glyph, gx=ix: p.draw_text(
            gx, 30, g, FONT_ICON, 17, Pango.Weight.NORMAL, OV_TEXT, align="right"))
        ix -= 38
    p.with_alpha(0.50, lambda: p.draw_text(
        ix - 24, 32, "80%", FONT, 13, Pango.Weight.MEDIUM, OV_TEXT, align="right"))
    p.with_alpha(0.50, lambda: p.draw_text(
        ix - 2, 30, "󰁹", FONT_ICON, 17, Pango.Weight.NORMAL, OV_TEXT, align="right"))

    # ---- 1. accent rule (palette primary) ---------------------------------
    top = H * 0.30
    p.fill_rounded(x, top, 40, 3, 1.5, PRIMARY)

    # ---- 2. date: a tracked, uppercase label ------------------------------
    label_y = top + 3 + 20
    p.draw_text_shadowed(x, label_y, "MONDAY, OCTOBER 19", FONT, 12, Pango.Weight.SEMIBOLD,
                         OV_SUBTLE, align="left", shadow_alpha=0.5, shadow_offset=1, spacing=1.6)

    # ---- 3. hero clock: light weight, tight tracking, accent colon ---------
    clock_y = label_y + 30
    lift = 10.0 * reveal
    clock_y -= lift
    cr.save()
    cr.translate(x, clock_y)
    cr.scale(1.0 - 0.04 * reveal, 1.0 - 0.04 * reveal)
    cr.translate(-x, -clock_y)

    tw, th = p.measure("22:44", FONT, 124, Pango.Weight.LIGHT)
    # "22" + accent ":" + "44"
    w22, _ = p._tracked("22", FONT, 124, Pango.Weight.LIGHT)
    wcolon, _ = p._tracked(":", FONT, 124, Pango.Weight.LIGHT)
    w44, _ = p._tracked("44", FONT, 124, Pango.Weight.LIGHT)
    track = -3.0
    adv22 = sum(w22) + track
    advcolon = sum(wcolon) + track
    p.draw_text_shadowed(x, clock_y, "22", FONT, 124, Pango.Weight.LIGHT, OV_TEXT,
                         align="left", shadow_alpha=0.6, shadow_offset=2, spacing=track)
    p.draw_text_shadowed(x + adv22, clock_y, ":", FONT, 124, Pango.Weight.LIGHT, PRIMARY,
                         align="left", shadow_alpha=0.6, shadow_offset=2, spacing=track)
    p.draw_text_shadowed(x + adv22 + advcolon, clock_y, "44", FONT, 124, Pango.Weight.LIGHT,
                         OV_TEXT, align="left", shadow_alpha=0.6, shadow_offset=2, spacing=track)
    cr.restore()

    # ---- 4. auth field: a hairline-underlined input ------------------------
    fw = 340.0
    ink_y, _iw, ih = p.ink_size("22:44", FONT, 124, Pango.Weight.LIGHT)
    field_top = clock_y + ink_y + ih + 26
    text_y = field_top + 6
    line_y = field_top + 46

    if state == "idle":
        p.draw_text(x, text_y, "Password", FONT, 17, Pango.Weight.NORMAL, OV_MUTED)
    else:
        p.draw_text(x, text_y, "•" * 8, FONT, 17, Pango.Weight.NORMAL, OV_TEXT)

    if error:
        p.fill_rect(x, line_y, fw, 2, ERROR)
    else:
        # resting hairline with the palette accent as the focus affordance
        p.fill_rect(x, line_y, fw, 1, (0xFF, 0xFF, 0xFF, 0.28))
        p.fill_rect(x, line_y, fw, 2, (PRIMARY[0], PRIMARY[1], PRIMARY[2], 0.75))

    # ---- 5. hint / status line --------------------------------------------
    hint_y = line_y + 20
    if error:
        p.with_alpha(reveal, lambda: p.draw_text(
            x, hint_y, "Authentication failed", FONT, 13, Pango.Weight.MEDIUM,
            (0xFF, 0x8A, 0x8A, 0.95)))
    else:
        p.with_alpha(reveal * 0.95, lambda: p.draw_text(
            x, hint_y, "Touch sensor to unlock", FONT, 13, Pango.Weight.NORMAL, OV_MUTED))

    # ---- 6. media: one quiet transport line, not a 420px card --------------
    def draw_media():
        my = hint_y + 30
        p.draw_text(x, my, "Mad  ·  Ne-Yo", FONT, 13, Pango.Weight.NORMAL, OV_SUBTLE)
        for i, g in enumerate(("", "", "")):
            p.draw_text(x + 190 + i * 32, my, g, FONT_ICON, 14, Pango.Weight.NORMAL, OV_MUTED)

    p.with_alpha(reveal, draw_media)

    # ---- 7. notifications: a horizontal row along the bottom ---------------
    draw_notification_row(p, x, H - 56, NOTES)

    # ---- 8. power: one anchor, labelled row on expand ---------------------
    d = 40.0
    ax, ay = W - 56 - d, H - 56 - d
    p.fill_circle(ax + d / 2, ay + d / 2, d / 2, (0x05, 0x0A, 0x0E, 0.40))
    p.stroke_circle(ax + d / 2, ay + d / 2, d / 2, (0xFF, 0xFF, 0xFF, 0.16), 1)
    p.draw_text(ax + d / 2, ay + d / 2 - 10, "⏻", FONT_ICON, 18, Pango.Weight.NORMAL,
                (0xFF, 0xFF, 0xFF, 0.75), align="center")


# The demo set the lock screen shows in the mockups.
NOTES = [
    ("Claude Code", "Task in progress", (0xF5, 0xC2, 0xE7, 1.0), "C"),
    ("Connected", "SSH Tokyo-01", (0xA6, 0xE3, 0xA1, 1.0), "C"),
    ("Claude Code", "Daemon idle", (0x89, 0xB4, 0xFA, 1.0), "C"),
]


def notification_card_width(p, title, body):
    """Content-sized card: the width of the longest line, clamped."""
    tw, _h = p.measure(title, FONT, 13, Pango.Weight.MEDIUM)
    bw, _h = p.measure(body, FONT, 11.5, Pango.Weight.NORMAL)
    text_w = min(max(tw, bw) + 2, 200)
    return 10 + 28 + 10 + text_w + 12, text_w


def draw_notification_row(p, x, bottom, notes, more=0):
    """A horizontal row of compact notification cards, bottom-left.

    Card: 28px accent tile | title (13px) | body (11.5px) — content-sized,
    rounded, on a subtle fill with a hairline border.
    """
    card_h = 56.0
    gap = 8.0
    y = bottom - card_h
    cx = x

    for title, body, accent, glyph in notes:
        w, text_w = notification_card_width(p, title, body)
        p.fill_rounded(cx, y, w, card_h, 12, (0x05, 0x0A, 0x0E, 0.42))
        p.stroke_rounded(cx, y, w, card_h, 12, (0xFF, 0xFF, 0xFF, 0.12), 1)

        tile = 28.0
        ty = y + (card_h - tile) / 2.0
        p.fill_rounded(cx + 10, ty, tile, tile, 8, accent)
        p.draw_text(cx + 10 + tile / 2, ty + tile / 2 - 8, glyph, FONT, 14,
                    Pango.Weight.BOLD, (0xFF, 0xFF, 0xFF, 0.92), align="center")

        tx = cx + 10 + tile + 10
        p.draw_text(tx, y + 11, title, FONT, 13, Pango.Weight.MEDIUM, (0xFF, 0xFF, 0xFF, 0.92),
                    max_width=text_w)
        p.draw_text(tx, y + 30, body, FONT, 11.5, Pango.Weight.NORMAL, (0xFF, 0xFF, 0xFF, 0.55),
                    max_width=text_w)
        cx += w + gap

    if more:
        w = 46.0
        p.fill_rounded(cx, y, w, card_h, 12, (0x05, 0x0A, 0x0E, 0.42))
        p.stroke_rounded(cx, y, w, card_h, 12, (0xFF, 0xFF, 0xFF, 0.12), 1)
        p.draw_text(cx + w / 2, y + card_h / 2 - 8, f"+{more}", FONT, 13, Pango.Weight.NORMAL,
                    (0xFF, 0xFF, 0xFF, 0.55), align="center")


def draw_notification_detail(cr, path):
    """A 2x crop of the notification row, for the design document."""
    cw, ch = 860, 220
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, cw, ch)
    c2 = cairo.Context(surf)
    p2 = Painter(c2)
    c2.set_source_rgba(0.02, 0.05, 0.07, 1.0)
    c2.rectangle(0, 0, cw, ch)
    c2.fill()
    c2.scale(2.0, 2.0)
    draw_notification_row(p2, 12, 96, NOTES, more=2)
    surf.write_to_png(path)
    print("wrote", path)



# --------------------------------------------------------------------------
# Output names match the committed images in docs/images/ (those are JPEG
# copies, downscaled, so the repository stays small — re-encode after
# rendering if you want byte-identical files).
# --------------------------------------------------------------------------
WALLPAPER = os.environ.get("WALLPAPER", "wall_bright.png")
OUT = sys.argv[1] if len(sys.argv) > 1 else "."

FRAMES = [
    ("lockscreen-current-wallpaper", draw_current, "typing"),
    ("lockscreen-current-idle", draw_current, "idle"),
    ("lockscreen-proposed-idle", draw_proposed, "idle"),
    ("lockscreen-proposed-typing", draw_proposed, "typing"),
    ("lockscreen-proposed-error", draw_proposed, "error"),
]

for name, fn, state in FRAMES:
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    cr = cairo.Context(surf)
    p = Painter(cr)
    fn(p, cr, state)
    path = os.path.join(OUT, f"{name}.png")
    surf.write_to_png(path)
    print("wrote", path)

draw_notification_detail(cairo.Context(cairo.ImageSurface(cairo.FORMAT_ARGB32, 1, 1)),
                         os.path.join(OUT, "lockscreen-notifications-detail.png"))
