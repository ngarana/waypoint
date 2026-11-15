#include "wayland/ShmBuffer.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <climits>
#include <cstring>

namespace qypr {

const wl_buffer_listener kBufferListener = {
    .release = ShmBuffer::handleRelease,
};

std::unique_ptr<ShmBuffer> ShmBuffer::create(wl_shm* shm, int width, int height) {
    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    return create(shm, width, height, stride, WL_SHM_FORMAT_ARGB8888);
}

cairo_format_t ShmBuffer::cairoFormatForWlShmFormat(uint32_t wl_shm_format) {
    if (wl_shm_format == WL_SHM_FORMAT_ARGB8888) return CAIRO_FORMAT_ARGB32;
    if (wl_shm_format == WL_SHM_FORMAT_XRGB8888) return CAIRO_FORMAT_RGB24;
    return CAIRO_FORMAT_INVALID;
}

std::unique_ptr<ShmBuffer> ShmBuffer::create(wl_shm* shm, int width, int height, int stride,
                                             uint32_t wl_shm_format) {
    cairo_format_t const cairo_format = cairoFormatForWlShmFormat(wl_shm_format);
    if (!shm || width <= 0 || height <= 0 || cairo_format == CAIRO_FORMAT_INVALID || stride <= 0 ||
        stride < cairo_format_stride_for_width(cairo_format, width)) {
        return nullptr;
    }
    const size_t size = static_cast<size_t>(stride) * static_cast<size_t>(height);
    if (size > static_cast<size_t>(INT_MAX)) return nullptr;

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

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int>(size));
    if (!pool) {
        close(fd);
        munmap(data, size);
        return nullptr;
    }
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, wl_shm_format);
    wl_shm_pool_destroy(pool);
    close(fd);
    if (!buffer) {
        munmap(data, size);
        return nullptr;
    }

    auto self = std::unique_ptr<ShmBuffer>(new ShmBuffer());
    self->buffer_ = buffer;
    self->data_ = data;
    self->size_ = size;
    self->width_ = width;
    self->height_ = height;
    self->stride_ = stride;
    self->format_ = wl_shm_format;
    self->cairo_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data),
                                                       cairo_format, width, height, stride);
    if (cairo_surface_status(self->cairo_) != CAIRO_STATUS_SUCCESS) return nullptr;

    if (wl_buffer_add_listener(buffer, &kBufferListener, self.get()) != 0) return nullptr;
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
