// SecureBuffer.hpp - A fixed-capacity, non-pageable buffer for secrets.
//
// Why not std::string (QL-3 of docs/LOCK_SECURITY_REVIEW.md):
//
//   * std::string::clear()/erase() change the length; they do not overwrite the
//     bytes. Every `+=` that grows past the capacity reallocates and *frees* the
//     old block — which still holds a prefix of the password — and std::string
//     copies leak whole copies (lambda captures, temporaries).
//   * Nothing stops the bytes reaching disk: a swap-out or a core dump of the
//     lock process writes the heap out verbatim.
//
// So the secret needs a container that (a) never reallocates, (b) can be locked
// into RAM, and (c) wipes itself. That is all this is: a char buffer with a
// length, a capacity reserved once, `mlock()`ed on best effort, and
// `explicit_bzero()` on every shrink, clear, move-out and destruction.
//
// It is deliberately *not* a general string type — no iterators, no
// concatenation operators, no implicit std::string conversion (which would
// silently create an unwiped copy). The only way out is cStr()/view(), both of
// which borrow storage owned here.

#pragma once

#include <cstddef>
#include <string_view>

namespace qypr {

class SecureBuffer {
public:
    // Long enough for any password plus room for a paste; a fixed capacity is
    // the point (a reallocation would free a block holding part of the secret).
    static constexpr size_t kDefaultCapacity = 512;

    SecureBuffer() : SecureBuffer(kDefaultCapacity) {}
    explicit SecureBuffer(size_t capacity);
    ~SecureBuffer();

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    // Move-out *leaves the source empty but usable* (it gets a fresh allocation)
    // so a field that hands its contents to a worker can keep accepting input.
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;

    // Append bytes. Anything past the capacity is dropped — the field simply
    // stops accepting input rather than reallocating.
    void append(std::string_view bytes);

    // Remove one UTF-8 code point from the end, zeroing the bytes removed (a
    // plain shorten would leave the popped character in place — LockScreen's
    // Backspace path).
    void popBack();

    // Zero the whole allocation and reset the length.
    void clear();

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ >= cap_; }
    size_t capacity() const { return cap_; }

    // Borrowed NUL-terminated view (valid until the next mutation/destruction).
    // PAM's conversation needs this; it strdup()s it into a reply that libpam
    // owns and wipes.
    const char* cStr() const { return buf_ != nullptr ? buf_ : ""; }
    std::string_view view() const {
        return {buf_ != nullptr ? buf_ : "", size_};
    }

    // True when the bytes are actually pinned in RAM: `mlock` can fail under
    // RLIMIT_MEMLOCK (the buffer is then still wiped, but swappable).
    bool locked() const { return locked_; }

private:
    // Release the current allocation and take ownership of `other`'s.
    void steal(SecureBuffer& other) noexcept;
    // Allocate a fresh empty buffer (used by the move constructor so a moved-out
    // source stays writable). Never throws: on OOM the buffer is left inert.
    void allocate(size_t capacity) noexcept;
    void release() noexcept;

    char* buf_ = nullptr;
    size_t cap_ = 0;
    size_t size_ = 0;
    bool locked_ = false;
};

}  // namespace qypr