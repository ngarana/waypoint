// FprintdTransport.hpp - The fprintd D-Bus calls fingerprint unlock is built on.
//
// FingerprintAuthenticator talks to fprintd (net.reactivated.Fprint) directly,
// the way hyprlock and gnome-shell do, instead of running pam_fprintd on a
// thread: a D-Bus client can end a scan whenever it needs to (VerifyStop), which
// a blocked pam_authenticate() cannot. That matters around suspend — see
// docs/FINGERPRINT.md.
//
// This interface is the seam between the authenticator's state machine and the
// bus. FprintdDbus is the sd-bus implementation; tests substitute a scripted
// fake. Every call completes asynchronously on the loop thread, exactly once.

#pragma once

#include <functional>
#include <string>

namespace qypr {

// Outcome of one call. An empty `name` means success; otherwise it is the D-Bus
// error name (fprintd's net.reactivated.Fprint.Error.* or a bus error).
struct FprintdError {
    std::string name;
    std::string message;
    bool failed() const { return !name.empty(); }
};

namespace fprintd_error {
// Reader held by another client (sudo's or polkit's pam_fprintd), or "device
// busy" while fprintd suspends/resumes it: transient, worth retrying.
inline constexpr const char* kAlreadyInUse = "net.reactivated.Fprint.Error.AlreadyInUse";
inline constexpr const char* kInternal = "net.reactivated.Fprint.Error.Internal";
// The claim is gone (fprintd restarted, or we never had one): claim again.
inline constexpr const char* kClaimDevice = "net.reactivated.Fprint.Error.ClaimDevice";
inline constexpr const char* kNoActionInProgress =
    "net.reactivated.Fprint.Error.NoActionInProgress";
// Permanent for this lock: nothing to verify against, or not allowed to.
inline constexpr const char* kNoEnrolledPrints = "net.reactivated.Fprint.Error.NoEnrolledPrints";
inline constexpr const char* kNoSuchDevice = "net.reactivated.Fprint.Error.NoSuchDevice";
inline constexpr const char* kPermissionDenied = "net.reactivated.Fprint.Error.PermissionDenied";
inline constexpr const char* kServiceUnknown = "org.freedesktop.DBus.Error.ServiceUnknown";
inline constexpr const char* kAccessDenied = "org.freedesktop.DBus.Error.AccessDenied";
}  // namespace fprintd_error

class FprintdTransport {
public:
    using Reply = std::function<void(const FprintdError& error)>;
    // VerifyStatus(result, done) from the device this transport claimed, and
    // only from the fprintd instance that granted the claim.
    using StatusHandler = std::function<void(const std::string& result, bool done)>;
    // fprintd dropped off the bus (crash or restart): any claim is gone.
    using VanishedHandler = std::function<void()>;

    FprintdTransport() = default;
    virtual ~FprintdTransport() = default;
    FprintdTransport(const FprintdTransport&) = delete;
    FprintdTransport& operator=(const FprintdTransport&) = delete;

    virtual void setHandlers(StatusHandler status, VanishedHandler vanished) = 0;

    // GetDefaultDevice, then Claim("") on it for the calling user.
    virtual void claim(Reply done) = 0;
    virtual void verifyStart(Reply done) = 0;  // VerifyStart("any")
    virtual void verifyStop(Reply done) = 0;
    virtual void release(Reply done) = 0;
};

}  // namespace qypr
