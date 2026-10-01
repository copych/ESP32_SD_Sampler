#pragma once
#include <atomic>
#include <stdint.h>
#include <math.h>

// Fixed memory, one producer per queue (audio/control), ControlTask consumes.
// No allocation, logging or locks on the audio path. Values are millionths
// of amplitude, except timing kinds (microseconds) and I2S errors (code).
namespace audio_debug {
enum Kind : unsigned {
    START, EOF_STOP, FORCE_STOP, RELEASE, FAST_RELEASE, ENVELOPE_END,
    BUFFER_SWAP, SEAM_ERROR, UNDERRUN, READ_ERROR, PAUSE_GAP, PAUSE_SAMPLES,
    PAUSE_GT1, RESUME_JUMP, RELEASE_JUMP, MIX_CLIP, MIX_NONFINITE, EDGE_EVENT, OUTPUT_JUMP,
    COMPUTE_US, SLOW_BLOCK, I2S_ERROR, CONTROL_GAP_US, COUNT
};
struct Event {
    uint32_t timeMs, frame, value;
    uint8_t kind, voice, note;
};
struct Counters {
    std::atomic<uint32_t> count[COUNT]{}, peak[COUNT]{};
};
class Recorder {
public:
    Counters counters;
    std::atomic<uint32_t> dropped{0};
    void mark(Kind kind, uint32_t value = 0, uint32_t amount = 1) {
        if (amount) counters.count[kind].fetch_add(amount, std::memory_order_relaxed);
        // Single writer for this recorder; readers only load.
        if (value > counters.peak[kind].load(std::memory_order_relaxed))
            counters.peak[kind].store(value, std::memory_order_relaxed);
    }
    void event(Kind kind, uint32_t timeMs, uint32_t frame, uint32_t value,
               uint8_t voice = 255, uint8_t note = 255) {
        const uint32_t w = written.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) == capacity) {
            dropped.fetch_add(1, std::memory_order_relaxed); return;
        }
        events[w % capacity] = {timeMs, frame, value, uint8_t(kind), voice, note};
        written.store(w + 1, std::memory_order_release);
    }
    bool pop(Event& event) {
        const uint32_t r = read.load(std::memory_order_relaxed);
        if (r == written.load(std::memory_order_acquire)) return false;
        event = events[r % capacity];
        read.store(r + 1, std::memory_order_release); return true;
    }
    uint32_t count(Kind k) const { return counters.count[k].load(std::memory_order_relaxed); }
    uint32_t peak(Kind k) const { return counters.peak[k].load(std::memory_order_relaxed); }
private:
    static constexpr unsigned capacity = 16;
    Event events[capacity]{};
    std::atomic<uint32_t> written{0}, read{0};
};
inline uint32_t level(float l, float r) {
    const float v = fmaxf(fabsf(l), fabsf(r));
    return !isfinite(v) || v >= 4000.f ? UINT32_MAX : uint32_t(v * 1000000.f);
}
inline Recorder audio, control;
inline const char* name(Kind kind) {
    static const char* names[] = {"start", "eof", "force_stop", "release", "fast_release", "env_end",
        "buffer_swap", "seam_error", "underrun", "read_error", "control_hold", "pause_samples",
        "pause_gt1", "resume_jump", "release_jump", "mix_clip", "nonfinite", "edge", "output_jump",
        "compute_us", "slow_block", "i2s_error", "control_gap_us"};
    return unsigned(kind) < COUNT ? names[kind] : "unknown";
}
}
