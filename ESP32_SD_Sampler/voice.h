#pragma once
#include <atomic>
#include "adsr.h"
#ifdef SAMPLER_HOST_TEST
#include "tests/host_platform.h"
#else
#include "sdmmc.h"
#endif
#include "pcm_stream.h"
#if DEBUG_AUDIO_DIAGNOSTICS
#include "audio_debug.h"
#endif

static constexpr uint32_t VOICE_BUFFER_FRAMES = READ_BUF_SECTORS * BYTES_PER_SECTOR / sizeof(PcmFrame);

static constexpr uint16_t INVALID_SAMPLE_SOURCE = UINT16_MAX;

// Stored once for every WAV found in the current sample-set.
struct sample_source_t {
    uint32_t byte_offset = 0, size = 0, data_size = 0;
    int sample_rate = 44100, channels = 0, bit_depth = 0;
    uint32_t loop_first_smp = 0, loop_last_smp = 0;
    bool loop_points = false;
    std::vector<chain_t> sectors;
};

// Compact note/velocity mapping. Many cells can refer to the same WAV while
// carrying their own pitch, gain and resolved loop interval.
struct sample_t {
    uint16_t source = INVALID_SAMPLE_SOURCE;
    uint8_t orig_velo_layer = 0;
    bool native_freq = false;
    float speed = 0, amp = 1;
    uint32_t loop_first_smp = 0, loop_last_smp = 0;
    int8_t loop_mode = LOOP_NONE;
};

static_assert(sizeof(sample_t) <= 24,
              "sample_t grew: the 128x16 map must remain compact");

class Voice {
public:
    bool init(SDMMC_FAT32*, uint32_t* sustain, uint32_t* normalized);
    bool allocateBuffers();
    void start(const sample_source_t&, const sample_t&, uint8_t note, uint8_t velocity);
    void end(Adsr::eEnd_t);
    void feed();
    void getSample(float& left, float& right);
    uint32_t hunger();
    void setPressed(uint32_t pressed) { _pressed = pressed; }
    void setPitch(float modifier);
    int getChannels() const { return _sampleSource ? _sampleSource->channels : 0; }
    bool isActive() const { return _active.load(); }
    bool isDying() const { return _dying.load(); }
    uint8_t getMidiNote() const { return _midiNote; }
    uint8_t getMidiVelo() const { return _midiVelo; }
    float getKillScore() const;
    void setAttackTime(float value);
    void setDecayTime(float value);
    void setReleaseTime(float value);
    void setSustainLevel(float value);
    uint32_t underruns() const { return _underruns.load(); }
    uint32_t maxFeedMicros() const { return _maxFeedMicros; }
    int my_id = 0;
private:
#ifdef SAMPLER_HOST_TEST
    friend struct VoiceTestAccess;
#endif
    struct Slot {
        PcmFrame* pcm = nullptr;
        uint32_t count = 0;
        bool final = false;
        std::atomic<bool> ready{false};
    } _slots[2];
    // Core1 waits for the current render call when changing lifecycle/envelope.
    // Core0 checks flags only: no allocation, I/O, locks or waiting.
    void pauseAudio();
    void resumeAudio() { _enabled.store(true); }
    std::atomic<bool> _enabled{false}, _rendering{false}, _active{false}, _dying{false};
    SDMMC_FAT32* _card = nullptr;
    uint32_t* _sustain = nullptr;
    uint32_t* _normalized = nullptr;
    sample_t _sampleFile;
    const sample_source_t* _sampleSource = nullptr;
    Adsr AmpEnv;
    PcmProducer _producer;
    PcmFrame* _loopCache = nullptr;
    uint32_t _cacheCount = 0, _cacheCapacity = 0, _cacheFirst = 0, _cacheAllocated = 0;
    unsigned _writeSlot = 0, _readSlot = 0;
    bool _producedFinal = false, _pressed = false, _hasPlayed = false;
    uint8_t _midiNote = 255, _midiVelo = 0;
    float _position = 0, _speed = 1, _speedModifier = 1, _amp = 0, _killScoreCoef = 0;
    float _lastL = 0, _lastR = 0;
    // Audio-core-owned fallback while Core1 briefly updates voice state. Using
    // the last frame avoids inserting a zero sample into an otherwise
    // continuous release. Core1 never accesses these fields.
    float _pauseHoldL = 0, _pauseHoldR = 0;
    unsigned _underrunFade = 0;
    std::atomic<uint32_t> _playedFrames{0}, _underruns{0};
    std::atomic<uint32_t> _framesRemaining{0};
    uint32_t _maxFeedMicros = 0;
#if DEBUG_AUDIO_DIAGNOSTICS
    // Audio-core-owned history survives control-side start/end resets.
    bool _debugPaused = false, _debugExpectFirst = false;
    bool _debugReleasePending = false, _debugResumePending = false;
    uint32_t _debugPauseSamples = 0;
    uint8_t _debugReleaseNote = 255;
    PcmFrame _debugExpectedFirst;
    float _debugLastL = 0, _debugLastR = 0;
#endif
};
