#include "auth/PamAuthenticator.hpp"

#include <security/pam_appl.h>
#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {
// Passed to the PAM conversation so it can answer password prompts. It borrows
// the worker's SecureBuffer (no copy): the only secret material that leaves this
// process is the strdup()ed reply libpam takes ownership of and wipes itself.
struct ConvData {
    const SecureBuffer* password = nullptr;
};

int conversation(int numMsg, const pam_message** msg, pam_response** resp, void* appdata) {
    if (numMsg <= 0) { return PAM_CONV_ERR; }
    auto* data = static_cast<ConvData*>(appdata);

    // NOLINTBEGIN(cppcoreguidelines-owning-memory) // PAM takes ownership via *resp
    // NOLINTBEGIN(cppcoreguidelines-no-malloc,hicpp-no-malloc) // PAM takes ownership via *resp
    auto* replies = static_cast<pam_response*>(calloc(numMsg, sizeof(pam_response)));
    // NOLINTEND(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    // NOLINTEND(cppcoreguidelines-owning-memory)
    if (replies == nullptr) { return PAM_BUF_ERR; }

    for (int i = 0; i < numMsg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:  // the password prompt
                replies[i].resp = strdup(data->password != nullptr ? data->password->cStr() : "");
                break;
            case PAM_PROMPT_ECHO_ON:
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO:
            default:
                replies[i].resp = nullptr;
                break;
        }
        replies[i].resp_retcode = 0;
    }
    *resp = replies;
    return PAM_SUCCESS;
}

std::string currentUser() {
    if (const passwd* pw = getpwuid(getuid())) { return pw->pw_name; }
    return "";
}
}  // namespace

PamAuthenticator::PamAuthenticator(EventLoop& loop, std::string service)
    : loop_(loop),
      service_(std::move(service)) {}

PamAuthenticator::~PamAuthenticator() {
    if (worker_.joinable()) { worker_.join(); }
}

bool PamAuthenticator::authenticate(SecureBuffer&& password, Done done) {
    if (busy_.load() || password.empty()) { return false; }
    if (worker_.joinable()) {
        worker_.join();  // reap the previous (finished) attempt
    }

    busy_.store(true);
    std::string const service = service_;
    std::string const user = currentUser();

    // `pw` is *moved* into the worker: the password lives in exactly one
    // allocation from here on, and the SecureBuffer destructor wipes it when the
    // thread exits (success, failure, or a PAM error).
    worker_ = std::thread(
        [this, service, user, pw = std::move(password), done = std::move(done)]() mutable {
            ConvData data{&pw};
            pam_conv const conv{.conv = conversation, .appdata_ptr = &data};
            pam_handle_t* pamh = nullptr;

            int rc = pam_start(service.c_str(), user.c_str(), &conv, &pamh);
            if (rc == PAM_SUCCESS) { rc = pam_authenticate(pamh, 0); }
            if (rc == PAM_SUCCESS) { rc = pam_acct_mgmt(pamh, 0); }

            std::string const message = pam_strerror(pamh, rc);
            pam_end(pamh, rc);
            pw.clear();  // explicit, in addition to the destructor's wipe

            Result result = Result::Error;
            if (rc == PAM_SUCCESS) {
                result = Result::Success;
            } else if (rc == PAM_AUTH_ERR) {
                result = Result::Failure;
            }

            loop_.post([done, result, message] { done(result, message); });
            busy_.store(false);
        });
    return true;
}

}  // namespace qypr
