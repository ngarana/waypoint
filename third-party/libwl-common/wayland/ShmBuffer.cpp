#include "wayland/ShmBuffer.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

namespace qypr {

const wl_buffer_listener kBufferListener = {
    .release = ShmBuffer::handleRelease,
};

std::unique_ptr<ShmBuffer> ShmBuffer::create(wl_shm* shm, int width, int height) {
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    const size_t size = static_cast<size_t>(stride) * height;

    int fd = memfd_create("qypr-shm", MFD_CLOEXEC);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return nullptr;
    }

    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return nullptr;
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    wl_buffer* buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    auto self = std::unique_ptr<ShmBuffer>(new ShmBuffer());
    self->buffer_ = buffer;
    self->data_ = data;
    self->size_ = size;
    self->width_ = width;
    self->height_ = height;
    self->cairo_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data),
                                                       CAIRO_FORMAT_ARGB32, width, height, stride);

    wl_buffer_add_listener(buffer, &kBufferListener, self.get());
    return self;
}

ShmBuffer::~ShmBuffer() {
    if (cairo_) cairo_surface_destroy(cairo_);
    if (buffer_) wl_buffer_destroy(buffer_);
    if (data_ && data_ != MAP_FAILED) munmap(data_, size_);
}

void ShmBuffer::handleRelease(void* data, wl_buffer*) {
    static_cast<ShmBuffer*>(data)->busy_ = false;
}

}  // namespace qypr
