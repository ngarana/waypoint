#include "auth/FingerprintAuthenticator.hpp"

#include <security/pam_appl.h>
#include <pwd.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {

std::string currentUser() {
    if (const passwd* pw = getpwuid(getuid())) { return pw->pw_name; }
    return "";
}

struct ConvData {
    EventLoop* loop = nullptr;
    FingerprintAuthenticator::Info* info = nullptr;
};

// Dedicated-service conversation (swaylock#445 style): every echo prompt is
// answered with an empty reply — the pam_fprintd module never needs a typed
// secret, it reads the finger itself. Info/error messages are SDDM-style:
// captured here on the worker thread and posted to the loop so the status
// line can show progress while pam_authenticate() stays blocked.
int conversation(int numMsg, const pam_message** msg, pam_response** resp, void* appdata) {
    if (numMsg <= 0) { return PAM_CONV_ERR; }
    auto* data = static_cast<ConvData*>(appdata);

    // NOLINTBEGIN(cppcoreguidelines-owning-memory) // PAM takes ownership via *resp
    // NOLINTBEGIN(cppcoreguidelines-no-malloc,hicpp-no-malloc) // PAM takes ownership via *resp
    auto* replies =
        static_cast<pam_response*>(calloc(static_cast<size_t>(numMsg), sizeof(pam_response)));
    // NOLINTEND(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    // NOLINTEND(cppcoreguidelines-owning-memory)
    if (replies == nullptr) { return PAM_BUF_ERR; }

    for (int i = 0; i < numMsg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON:
                replies[i].resp = strdup("");
                break;
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO: {
                if (data->loop != nullptr && data->info != nullptr && *data->info) {
                    std::string text = msg[i]->msg != nullptr ? msg[i]->msg : "";
                    const bool isError = msg[i]->msg_style == PAM_ERROR_MSG;
                    auto cb = *data->info;
                    data->loop->post([cb = std::move(cb), text = std::move(text), isError] {
                        cb(text, isError);
                    });
                }
                replies[i].resp = nullptr;
                break;
            }
            default:
                replies[i].resp = nullptr;
                break;
        }
        replies[i].resp_retcode = 0;
    }
    *resp = replies;
    return PAM_SUCCESS;
}

// Hard misconfiguration: retrying is pointless, report Unavailable once.
bool isHardFailure(int rc) {
    switch (rc) {
        case PAM_AUTHINFO_UNAVAIL:
        case PAM_MODULE_UNKNOWN:
        case PAM_SERVICE_ERR:
        case PAM_USER_UNKNOWN:
        case PAM_BUF_ERR:
        case PAM_CONV_ERR:
            return true;
        default:
            return false;
    }
}

}  // namespace

FingerprintAuthenticator::FingerprintAuthenticator(EventLoop& loop, std::string service)
    : loop_(loop),
      service_(std::move(service)) {}

FingerprintAuthenticator::~FingerprintAuthenticator() {
    stop();
    if (worker_.joinable()) {
        // A blocked pam_authenticate() (fingerprint timeout, typically 30s)
        // must not stall unlock/exit on the loop thread. Join only a worker
        // known to have finished; otherwise detach and let process exit reap
        // it. Its late result is already invalidated by stop()'s generation
        // bump, and the first thing a woken worker does is drop its result.
        if (finished_.load()) {
            worker_.join();
        } else {
            worker_.detach();
        }
    }
}

bool FingerprintAuthenticator::moduleInstalled() {
    // pam_fprintd.so lives in /usr/lib/security on Arch (with a compat
    // /usr/lib64/security symlink); check both plus the loader's lib dir.
    static constexpr std::array<const char*, 3> kPaths = {
        "/usr/lib/security/pam_fprintd.so",
        "/lib/security/pam_fprintd.so",
        "/usr/lib64/security/pam_fprintd.so",
    };
    for (const char* p : kPaths) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) { return true; }
    }
    return false;
}

void FingerprintAuthenticator::start(Done done, Info info) {
    if (running_.load()) { return; }
    if (worker_.joinable()) {
        // Reap a finished previous run without blocking: a still-blocked
        // worker was already invalidated by stop()'s generation bump, so
        // detach it instead of joining a potentially 30s pam_authenticate().
        if (finished_.load()) {
            worker_.join();
        } else {
            worker_.detach();
        }
    }
    running_.store(true);
    finished_.store(false);
    const uint64_t gen = generation_.load() + 1;
    generation_.store(gen);
    worker_ = std::thread([this, done = std::move(done), info = std::move(info), gen]() mutable {
        // `done` binds to workerMain's const-ref (it lives in this capture,
        // on the same thread); `info` moves (forwarded into posted lambdas).
        workerMain(done, std::move(info), gen);
    });
}

void FingerprintAuthenticator::stop() {
    generation_.fetch_add(1);
    running_.store(false);
    // Deliberately no join(): called from the loop thread on every unlock /
    // collapse, and the worker may be blocked in pam_authenticate() until its
    // timeout. The generation bump makes any late Success a no-op.
}

void FingerprintAuthenticator::workerMain(const Done& done, Info info, uint64_t generation) {
    const std::string service = service_;
    const std::string user = currentUser();
    auto finish = [&](auto&& postFn) {
        finished_.store(true);
        if (generation == generation_.load()) {
            loop_.post(std::forward<decltype(postFn)>(postFn));
        }
    };
    auto stillCurrent = [&] {
        return generation == generation_.load() && running_.load();
    };

    if (user.empty()) {
        running_.store(false);
        finish([done] { done(Result::Unavailable, "unknown user"); });
        return;
    }

    // Retry transient failures (finger mismatch / timeout surface as
    // PAM_AUTH_ERR from pam_fprintd) until stop() or a hard error.
    for (;;) {
        ConvData data{&loop_, &info};
        pam_conv const conv{.conv = conversation, .appdata_ptr = &data};
        pam_handle_t* pamh = nullptr;

        int rc = pam_start(service.c_str(), user.c_str(), &conv, &pamh);
        if (rc == PAM_SUCCESS) { rc = pam_authenticate(pamh, 0); }
        // Deliberately no pam_acct_mgmt(): this worker only *verifies identity*
        // to return to an existing session. The shipped qypr-fingerprint
        // service defines no `account` stack, so acct_mgmt falls through to
        // pam_warn+deny and would turn every successful verification into a
        // transient failure (infinite retry, never unlocks). The password path
        // on the `login` service keeps its own acct_mgmt untouched.

        std::string const message = pamh != nullptr ? pam_strerror(pamh, rc) : "pam_start failed";
        pam_end(pamh, rc);

        if (!stillCurrent()) {
            finished_.store(true);
            return;  // stopped while blocked: drop the late result
        }

        if (rc == PAM_SUCCESS) {
            running_.store(false);
            finish([done] { done(Result::Success, ""); });
            return;
        }
        if (isHardFailure(rc)) {
            running_.store(false);
            finish([done, message] { done(Result::Unavailable, message); });
            return;
        }
        // Transient PAM_AUTH_ERR (mismatch, timeout, swipe again): back off
        // briefly and retry. Sleep in small slices so stop() shortens the wait.
        for (int i = 0; i < 10; ++i) {
            if (!stillCurrent()) {
                finished_.store(true);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

}  // namespace qypr
