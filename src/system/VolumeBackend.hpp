// VolumeBackend.hpp - Default-sink volume via libpulse (pipewire-pulse).
//
// In-process native client on the shared EventLoop through PulseLoop — no
// CLI spawning, no polling (minimal-footprint principle). Connects async;
// once ready it resolves the default sink, reads volume/mute, and subscribes
// to SINK + SERVER events for push updates. Writes are async with optimistic
// snapshot updates.

#pragma once

#include <pulse/pulseaudio.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "system/PulseLoop.hpp"

namespace qypr {

class EventLoop;

struct VolumeSnapshot {
    bool available = false;  // connected and a default sink resolved
    double level = 0.0;      // 0..1 (PA_VOLUME_NORM = 1.0)
    bool muted = false;
    std::string sinkName;  // human-readable description

    bool operator==(const VolumeSnapshot&) const = default;
};

// An output device (PulseAudio sink). `name` is the internal id (the switch
// target); `description` is what the user sees.
struct AudioSink {
    std::string name;
    std::string description;
    bool isDefault = false;

    bool operator==(const AudioSink&) const = default;
};

// A per-application playback stream (PulseAudio sink-input).
struct AudioStream {
    uint32_t index = 0;
    std::string appName;
    double level = 0.0;
    bool muted = false;
    uint8_t channels = 2;  // for building the write cvolume

    bool operator==(const AudioStream&) const = default;
};

class VolumeBackend {
public:
    explicit VolumeBackend(EventLoop& loop);
    ~VolumeBackend();

    VolumeBackend(const VolumeBackend&) = delete;
    VolumeBackend& operator=(const VolumeBackend&) = delete;

    // Begins the async connect; availability arrives via onChange once the
    // context is ready. Returns false only if the context refused to start.
    bool start();

    const VolumeSnapshot& snapshot() const { return snap_; }

    // Output devices and per-app streams (Phase 14 audio depth). Rebuilt on the
    // matching subscription events; empty until the first enumeration lands.
    const std::vector<AudioSink>& sinks() const { return sinks_; }
    const std::vector<AudioStream>& streams() const { return streams_; }

    // Fires whenever a pushed update actually changed the snapshot OR the sink /
    // stream lists.
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // True once the backend has produced its first result — real data or a
    // definitive "absent". Indicators show a neutral placeholder until then, so
    // an unrelated backend's push cannot prematurely mark this one loaded.
    bool ready() const { return ready_; }

    // Seed from the previous session's persisted snapshot (see StateCache).
    // The daemon that owns this state is often not running yet when the bar
    // starts — UPower in particular is D-Bus-activated and comes up *after* it
    // — so without a seed the indicator sits on its neutral "unknown" glyph for
    // seconds. Seeding marks the backend ready() so the very first frame
    // carries real values; the first live reply overwrites both the snapshot
    // and this flag. A no-op once a live reply has landed.
    void seed(const VolumeSnapshot& s) {
        if (ready_) { return; }
        snap_ = s;
        ready_ = true;
    }

    // Set the default sink volume 0..1 / toggle mute (async, optimistic).
    void setLevel(double frac);
    void toggleMute();

    // Switch the default output device (async).
    void setDefaultSink(const std::string& name);
    // Per-app stream volume 0..1 / mute (async, optimistic).
    void setStreamVolume(uint32_t index, double frac);
    void toggleStreamMute(uint32_t index);

private:
    static void onContextState(pa_context* c, void* userdata);
    static void onServerInfo(pa_context* c, const pa_server_info* info, void* userdata);
    static void onSinkInfo(pa_context* c, const pa_sink_info* info, int eol, void* userdata);
    static void onSinkList(pa_context* c, const pa_sink_info* info, int eol, void* userdata);
    static void onStreamList(pa_context* c, const pa_sink_input_info* info, int eol,
                             void* userdata);
    static void onSubscribe(pa_context* c, pa_subscription_event_type_t t, uint32_t idx,
                            void* userdata);
    void queryServer();
    void querySink();
    void querySinks();    // the full output-device list
    void queryStreams();  // the per-app stream list
    void changed(const VolumeSnapshot& next);
    // Re-arm start() after a failed/dropped connection (the server may still be
    // coming up). One 3s retry at a time; stopped_ guards the destructor path.
    void scheduleReconnect();
    void notify() {
        if (onChange_) onChange_();
    }

    PulseLoop pulseLoop_;
    pa_context* ctx_ = nullptr;
    std::string defaultSink_;  // internal sink name (write target)
    uint8_t channels_ = 2;
    VolumeSnapshot snap_;
    std::vector<AudioSink> sinks_;
    std::vector<AudioSink> sinksBuilding_;  // accumulates during enumeration
    std::vector<AudioStream> streams_;
    std::vector<AudioStream> streamsBuilding_;
    std::function<void()> onChange_;
    int retryTimer_ = -1;   // pending reconnect timer fd (EventLoop)
    bool stopped_ = false;  // destructor ran; never schedule/publish again
    // Every result path calls this instead of onChange_ directly, so ready()
    // flips true exactly when the first real snapshot is published.
    void notifyReady() {
        ready_ = true;
        if (onChange_) onChange_();
    }
    bool ready_ = false;
};

}  // namespace qypr
