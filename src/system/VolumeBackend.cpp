// VolumeBackend.cpp - libpulse default-sink monitor implementation.
#include "system/VolumeBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {
// Run a pa_operation-returning call and release the handle.
template <typename Op>
void fire(Op* op) {
    if (op) { pa_operation_unref(op); }
}
}  // namespace

VolumeBackend::VolumeBackend(EventLoop& loop) : pulseLoop_(loop) {}

VolumeBackend::~VolumeBackend() {
    stopped_ = true;  // the reconnect timer must not outlive the loop
    if (retryTimer_ >= 0) {
        pulseLoop_.loop().removeTimer(retryTimer_);
        retryTimer_ = -1;
    }
    if (ctx_ != nullptr) {
        // Silence callbacks first: disconnect() fires the TERMINATED state
        // callback synchronously, and consumers (StatusBar) may already be
        // destroyed by now.
        onChange_ = nullptr;
        pa_context_set_state_callback(ctx_, nullptr, nullptr);
        pa_context_set_subscribe_callback(ctx_, nullptr, nullptr);
        // Disconnect synchronously frees every io/time/defer event that
        // libpulse created through PulseLoop.
        pa_context_disconnect(ctx_);
        pa_context_unref(ctx_);
    }
}

bool VolumeBackend::start() {
    if (ctx_ != nullptr) {
        // A previous attempt failed asynchronously; tear it down before
        // retrying. Silence callbacks first: disconnect() fires TERMINATED
        // synchronously, which must not publish a bogus state here.
        pa_context_set_state_callback(ctx_, nullptr, nullptr);
        pa_context_set_subscribe_callback(ctx_, nullptr, nullptr);
        pa_context_disconnect(ctx_);
        pa_context_unref(ctx_);
        ctx_ = nullptr;
    }

    ctx_ = pa_context_new(pulseLoop_.api(), "qypr");
    if (ctx_ == nullptr) { return false; }

    pa_context_set_state_callback(ctx_, &VolumeBackend::onContextState, this);
    pa_context_set_subscribe_callback(ctx_, &VolumeBackend::onSubscribe, this);

    if (pa_context_connect(ctx_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        std::fprintf(stderr, "qypr: pulse connect failed (%s); retrying\n",
                     pa_strerror(pa_context_errno(ctx_)));
        pa_context_unref(ctx_);
        ctx_ = nullptr;
        snap_.available = false;
        notifyReady();  // hide the placeholder
        scheduleReconnect();
        return false;
    }
    return true;
}

void VolumeBackend::scheduleReconnect() {
    if (stopped_ || retryTimer_ >= 0) { return; }
    // The server may still be coming up (pipewire-pulse starts slowly), so a
    // dropped/refused connection is retried instead of leaving the indicator
    // on its placeholder forever.
    retryTimer_ = pulseLoop_.loop().addTimer(3000, /*repeat=*/false, [this] {
        retryTimer_ = -1;
        if (stopped_) { return; }
        start();
    });
}

void VolumeBackend::onContextState(pa_context* c, void* userdata) {
    auto* self = static_cast<VolumeBackend*>(userdata);
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY: {
            using mask_t = std::underlying_type_t<pa_subscription_mask_t>;
            // Bitmask cast: the analyzer flags the combined value as out of
            // enum range; the flags are only ever OR-ed together like this.
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            auto const mask = static_cast<pa_subscription_mask_t>(
                static_cast<mask_t>(PA_SUBSCRIPTION_MASK_SINK) |
                static_cast<mask_t>(PA_SUBSCRIPTION_MASK_SINK_INPUT) |
                static_cast<mask_t>(PA_SUBSCRIPTION_MASK_SERVER));
            fire(pa_context_subscribe(c, mask, nullptr, nullptr));
            self->queryServer();
            self->querySinks();
            self->queryStreams();
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED: {
            // Publish unconditionally: if the connection died before the first
            // query (e.g. the server dropped us mid-startup), changed() would
            // suppress the push as "unchanged" and ready_ would never flip,
            // leaving the indicator on its placeholder forever.
            self->snap_ = VolumeSnapshot{};
            self->notifyReady();
            self->scheduleReconnect();
            break;
        }
        default:
            break;
    }
}

void VolumeBackend::onSubscribe(pa_context* /*unused*/, pa_subscription_event_type_t t,
                                uint32_t /*unused*/, void* userdata) {
    auto* self = static_cast<VolumeBackend*>(userdata);
    const auto facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
        self->queryServer();  // default sink may have changed
        self->querySinks();   // …which re-marks isDefault
    } else if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
        self->querySink();   // default volume/mute changed
        self->querySinks();  // a device came/went
    } else if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        self->queryStreams();  // an app stream came/went/changed
    }
}

void VolumeBackend::queryServer() {
    fire(pa_context_get_server_info(ctx_, &VolumeBackend::onServerInfo, this));
}

void VolumeBackend::onServerInfo(pa_context* /*unused*/, const pa_server_info* info,
                                 void* userdata) {
    auto* self = static_cast<VolumeBackend*>(userdata);
    if ((info == nullptr) || (info->default_sink_name == nullptr)) { return; }
    self->defaultSink_ = info->default_sink_name;
    self->querySink();
}

void VolumeBackend::querySink() {
    if (defaultSink_.empty()) { return; }
    fire(pa_context_get_sink_info_by_name(ctx_, defaultSink_.c_str(), &VolumeBackend::onSinkInfo,
                                          this));
}

void VolumeBackend::onSinkInfo(pa_context* /*unused*/, const pa_sink_info* info, int eol,
                               void* userdata) {
    if ((eol != 0) || (info == nullptr)) { return; }
    auto* self = static_cast<VolumeBackend*>(userdata);
    self->channels_ = info->volume.channels;

    VolumeSnapshot next;
    next.available = true;
    next.level = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    next.muted = info->mute != 0;
    next.sinkName = (info->description != nullptr) ? info->description : "";
    self->changed(next);
}

void VolumeBackend::querySinks() {
    sinksBuilding_.clear();
    fire(pa_context_get_sink_info_list(ctx_, &VolumeBackend::onSinkList, this));
}

void VolumeBackend::onSinkList(pa_context* /*unused*/, const pa_sink_info* info, int eol,
                               void* userdata) {
    auto* self = static_cast<VolumeBackend*>(userdata);
    if (eol != 0) {
        // Enumeration complete: publish if it actually changed.
        if (self->sinksBuilding_ != self->sinks_) {
            self->sinks_ = self->sinksBuilding_;
            self->notify();
        }
        self->sinksBuilding_.clear();
        return;
    }
    if (info == nullptr) { return; }
    AudioSink s;
    s.name = (info->name != nullptr) ? info->name : "";
    s.description = (info->description != nullptr) ? info->description : s.name;
    s.isDefault = s.name == self->defaultSink_;
    self->sinksBuilding_.push_back(std::move(s));
}

void VolumeBackend::queryStreams() {
    streamsBuilding_.clear();
    fire(pa_context_get_sink_input_info_list(ctx_, &VolumeBackend::onStreamList, this));
}

void VolumeBackend::onStreamList(pa_context* /*unused*/, const pa_sink_input_info* info, int eol,
                                 void* userdata) {
    auto* self = static_cast<VolumeBackend*>(userdata);
    if (eol != 0) {
        if (self->streamsBuilding_ != self->streams_) {
            self->streams_ = self->streamsBuilding_;
            self->notify();
        }
        self->streamsBuilding_.clear();
        return;
    }
    if (info == nullptr) { return; }
    // Skip streams with no client (e.g. internal monitors) and PulseAudio's own
    // helpers; a stream with no app name is not useful in the app list.
    const char* app = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
    AudioStream s;
    s.index = info->index;
    if (app != nullptr) {
        s.appName = app;
    } else {
        s.appName = (info->name != nullptr) ? info->name : "Audio";
    }
    s.level = static_cast<double>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM;
    s.muted = info->mute != 0;
    s.channels = info->volume.channels;
    self->streamsBuilding_.push_back(std::move(s));
}

void VolumeBackend::changed(const VolumeSnapshot& next) {
    if (next == snap_) { return; }
    snap_ = next;
    notifyReady();
}

void VolumeBackend::setLevel(double frac) {
    if ((ctx_ == nullptr) || defaultSink_.empty()) { return; }
    frac = std::clamp(frac, 0.0, 1.0);

    pa_cvolume cv;
    pa_cvolume_set(&cv, channels_, static_cast<pa_volume_t>(std::lround(frac * PA_VOLUME_NORM)));
    fire(pa_context_set_sink_volume_by_name(ctx_, defaultSink_.c_str(), &cv, nullptr, nullptr));

    // Optimistic; the SINK subscription event confirms.
    VolumeSnapshot next = snap_;
    next.level = frac;
    changed(next);
}

void VolumeBackend::toggleMute() {
    if ((ctx_ == nullptr) || defaultSink_.empty()) { return; }
    const bool mute = !snap_.muted;
    fire(pa_context_set_sink_mute_by_name(ctx_, defaultSink_.c_str(), mute ? 1 : 0, nullptr,
                                          nullptr));
    VolumeSnapshot next = snap_;
    next.muted = mute;
    changed(next);
}

void VolumeBackend::setDefaultSink(const std::string& name) {
    if ((ctx_ == nullptr) || name.empty()) { return; }
    fire(pa_context_set_default_sink(ctx_, name.c_str(), nullptr, nullptr));
    // Optimistic: re-mark the list; the SERVER event confirms and re-queries.
    defaultSink_ = name;
    for (auto& s : sinks_) { s.isDefault = (s.name == name); }
    notify();
}

void VolumeBackend::setStreamVolume(uint32_t index, double frac) {
    if (ctx_ == nullptr) { return; }
    frac = std::clamp(frac, 0.0, 1.0);
    for (auto& s : streams_) {
        if (s.index != index) { continue; }
        pa_cvolume cv;
        pa_cvolume_set(&cv, s.channels,
                       static_cast<pa_volume_t>(std::lround(frac * PA_VOLUME_NORM)));
        fire(pa_context_set_sink_input_volume(ctx_, index, &cv, nullptr, nullptr));
        s.level = frac;  // optimistic; the SINK_INPUT event confirms
        notify();
        return;
    }
}

void VolumeBackend::toggleStreamMute(uint32_t index) {
    if (ctx_ == nullptr) { return; }
    for (auto& s : streams_) {
        if (s.index != index) { continue; }
        const bool mute = !s.muted;
        fire(pa_context_set_sink_input_mute(ctx_, index, mute ? 1 : 0, nullptr, nullptr));
        s.muted = mute;
        notify();
        return;
    }
}

}  // namespace qypr
