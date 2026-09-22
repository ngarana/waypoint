// SecureBuffer.cpp - see the header for why this type exists (QL-3).
#include "core/SecureBuffer.hpp"

#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

namespace qypr {

namespace {
// Overwrite `n` bytes at `p`. explicit_bzero() is the only memset the compiler
// is forbidden to optimise away, which is exactly what a wipe needs.
void wipe(char* p, size_t n) {
    if (p != nullptr && n > 0) { ::explicit_bzero(p, n); }
}

// One warning is enough: a failing mlock is a property of the host's
// RLIMIT_MEMLOCK, not of this call.
bool warnedAboutMlock = false;
}  // namespace

SecureBuffer::SecureBuffer(size_t capacity) {
    allocate(capacity);
}

SecureBuffer::~SecureBuffer() {
    release();
}

// Ownership transfer that cannot throw. `other` is deliberately *not* left
// inert: the caller (LockScreen) keeps typing into it, so it gets its own fresh
// allocation and only its secret moves out.
SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept {
    steal(other);                      // this takes other's storage
    other.allocate(kDefaultCapacity);  // other stays empty *and usable*
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        release();                         // drop whatever this held, wiped
        steal(other);                      // this takes other's storage
        other.allocate(kDefaultCapacity);  // other stays empty and usable
    }
    return *this;
}

void SecureBuffer::allocate(size_t capacity) noexcept {
    if (capacity == 0) return;
    buf_ = static_cast<char*>(std::malloc(capacity));
    if (buf_ == nullptr) return;  // inert but safe: appends become no-ops
    cap_ = capacity;
    size_ = 0;
    locked_ = ::mlock(buf_, cap_) == 0;
    if (!locked_ && !warnedAboutMlock) {
        warnedAboutMlock = true;
        std::fprintf(stderr, "qypr: mlock(%zu) failed (%s); the secret stays swappable\n", cap_,
                     std::strerror(errno));
    }
}

void SecureBuffer::steal(SecureBuffer& other) noexcept {
    buf_ = other.buf_;
    cap_ = other.cap_;
    size_ = other.size_;
    locked_ = other.locked_;
    other.buf_ = nullptr;
    other.cap_ = 0;
    other.size_ = 0;
    other.locked_ = false;
}

void SecureBuffer::release() noexcept {
    if (buf_ == nullptr) return;
    wipe(buf_, cap_);  // the whole allocation, not just the live prefix
    if (locked_) { ::munlock(buf_, cap_); }
    std::free(buf_);
    buf_ = nullptr;
    cap_ = 0;
    size_ = 0;
    locked_ = false;
}

void SecureBuffer::append(std::string_view bytes) {
    if (buf_ == nullptr || bytes.empty()) return;
    // -1 so a terminating NUL always fits: `cStr()`/`view()` expose the live
    // prefix, and PAM consumers may build a C string from it. The terminator is
    // metadata, not part of the secret (and is wiped with everything else).
    const size_t room = cap_ - size_ > 0 ? cap_ - size_ - 1 : 0;
    const size_t n = bytes.size() < room ? bytes.size() : room;
    std::memcpy(buf_ + size_, bytes.data(), n);
    size_ += n;
    buf_[size_] = '\0';
}

void SecureBuffer::popBack() {
    if (size_ == 0) return;
    // Walk back to the start of the last UTF-8 code point so a multi-byte
    // character is removed whole.
    size_t i = size_ - 1;
    while (i > 0 && (static_cast<unsigned char>(buf_[i]) & 0xC0) == 0x80) { --i; }
    wipe(buf_ + i, size_ - i);
    size_ = i;
    if (buf_ != nullptr) { buf_[size_] = '\0'; }  // keep the C-string view well-formed
}

void SecureBuffer::clear() {
    if (buf_ != nullptr) { wipe(buf_, cap_); }
    size_ = 0;
}

}  // namespace qypr