// ActiveWindowIndicator.hpp - Focused window widget (wlr-foreign-toplevel).
//
// Session-sensitive: hidden while locked. Text-only indicator showing the
// currently focused window's title (falling back to its app id), following
// keyboard focus.
#pragma once

#include <string>

#include "system/ToplevelBackend.hpp"  // ToplevelSnapshot
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class ActiveWindowIndicator : public StatusIndicator {
public:
    explicit ActiveWindowIndicator(const SystemBackends& backends);

    std::string icon() const override { return ""; }  // text-only
    std::string label() const override;
    std::string tooltip() const override;
    bool sensitive() const override { return true; }

    void onBackendUpdate() override;

private:
    ToplevelBackend* backend_ = nullptr;
    ToplevelSnapshot snap_;
};

}  // namespace qypr
