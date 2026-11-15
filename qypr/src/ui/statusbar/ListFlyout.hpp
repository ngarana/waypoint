// ListFlyout.hpp - A module flyout built from a list of rows with buttons.
//
// Device-style flyouts (drives, a paired phone) share one shape: a header,
// optional section labels, and rows of glyph + title + subtitle with a few
// trailing buttons ("Open", "Eject", "Ring"). ListFlyout draws that shape
// from a FlyoutModel the module rebuilds on every frame, so the flyout always
// shows the backend's current state and the module owns no view state.
//
// Buttons capture what they act on by identity (a drive's object path, a
// device id), never by index: a click on a row whose object has gone away
// reaches a backend that no longer knows it and does nothing.
//
// Keyboard: Tab/Down and Shift+Tab/Up move between buttons in reading order,
// Enter/Space presses the selected one.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/statusbar/DetailedPopover.hpp"

namespace qypr {

struct FlyoutAction {
    std::string label;
    std::function<void()> run;
    bool enabled = true;
    bool closes = false;  // close the flyout after running (it opened something else)
};

struct FlyoutRow {
    enum class Kind : uint8_t { Item, Section, Note };
    enum class Tone : uint8_t { Normal, Subtle, Error, Success };
    Kind kind = Kind::Item;
    std::string glyph;
    std::string title;
    std::string subtitle;
    Tone tone = Tone::Subtle;  // subtitle (Item) or text (Note) colour
    bool busy = false;         // spinner in place of the glyph
    std::vector<FlyoutAction> actions;
};

struct FlyoutModel {
    std::string title;
    std::string detail;  // right side of the header ("84%", "4.0 TB")
    std::vector<FlyoutRow> rows;
    std::string empty = "Nothing to show";
};

class ListFlyout : public DetailedPopover {
public:
    using Builder = std::function<FlyoutModel()>;
    explicit ListFlyout(Builder build);

    [[nodiscard]] double contentWidth() const override;
    [[nodiscard]] double contentHeight() const override;
    void draw(Painter& p, int64_t now) override;
    bool handleClick(double x, double y) override;
    bool handleMotion(double x, double y) override;
    bool handleKey(uint32_t keysym) override;
    bool consumeCloseRequest() override;

    [[nodiscard]] const FlyoutModel& model() const { return model_; }
    // Re-run the builder (draw does this every frame).
    void rebuild();

private:
    struct Hit {
        Rect r;
        size_t row = 0;
        size_t action = 0;
    };
    bool press(size_t row, size_t action);
    [[nodiscard]] static double rowHeight(const FlyoutRow& row);
    [[nodiscard]] static double buttonWidth(const FlyoutAction& action);
    [[nodiscard]] static double buttonsWidth(const FlyoutRow& row);

    Builder build_;
    FlyoutModel model_;
    std::vector<Hit> hits_;
    int selected_ = -1;
    double hoverX_ = -1;
    double hoverY_ = -1;
    bool closeRequested_ = false;
};

}  // namespace qypr
