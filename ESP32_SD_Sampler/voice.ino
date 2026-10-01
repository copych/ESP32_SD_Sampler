#include "voice.h"
#include "sd_stream.h"

bool Voice::allocateBuffers() {
    for (auto& slot : _slots) {
        slot.pcm = static_cast<PcmFrame*>(heap_caps_malloc(
            (VOICE_BUFFER_FRAMES + 1) * sizeof(PcmFrame), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!slot.pcm) {
            for (auto& cleanup : _slots) { heap_caps_free(cleanup.pcm); cleanup.pcm = nullptr; }
            return false;
        }
    }
    return sdStreamWindow() != nullptr;
}

bool Voice::init(SDMMC_FAT32* card, uint32_t* sustain, uint32_t* normalized) {
    _card = card; _sustain = sustain; _normalized = normalized;
    if (!allocateBuffers()) return false;
    AmpEnv.init(SAMPLE_RATE);
    AmpEnv.end(Adsr::END_NOW);
    resumeAudio();
    return true;
}

void Voice::pauseAudio() {
    _enabled.store(false); // seq_cst handshake with getSample()
    while (_rendering.load()) taskYIELD();
}

void Voice::start(const sample_source_t& source, const sample_t& sample, uint8_t note, uint8_t velocity) {
    pauseAudio();
    _active.store(false);
    for (auto& slot : _slots) slot.ready.store(false);
    _sampleFile = sample;
    _sampleSource = &source;
    _midiNote = note; _midiVelo = velocity;
    _readSlot = _writeSlot = 0; _position = 0;
    _producedFinal = _hasPlayed = false; _pressed = true;
    _dying.store(false); _playedFrames.store(0); _framesRemaining.store(0);
    _lastL = _lastR = 0; _underrunFade = 0;
#if DEBUG_AUDIO_DIAGNOSTICS
    _debugExpectFirst = false; // renderer is paused
    _debugReleasePending = false;
    _debugResumePending = false; _debugPauseSamples = 0;
#endif
    const uint32_t frameBytes = source.channels * (source.bit_depth / 8);
    const uint32_t frames = frameBytes ? source.data_size / frameBytes : 0;
    if (!frames || source.sectors.empty() || !isfinite(sample.speed) || sample.speed <= 0 ||
        (source.channels != 1 && source.channels != 2) ||
        (source.bit_depth != 8 && source.bit_depth != 16 && source.bit_depth != 24 && source.bit_depth != 32) ||
        source.data_size % frameBytes || uint64_t(source.byte_offset) + source.data_size > source.size) {
        resumeAudio(); return;
    }
    if (sample.loop_mode < LOOP_NONE || sample.loop_mode > LOOP_PINGPONG ||
        sample.loop_first_smp >= sample.loop_last_smp || sample.loop_last_smp > frames)
        _sampleFile.loop_mode = LOOP_NONE;
    _producer.reset(frames, static_cast<eLoopType_t>(_sampleFile.loop_mode),
                    uint32_t(sample.loop_first_smp), uint32_t(sample.loop_last_smp));
    _cacheFirst = sample.loop_first_smp;
    _cacheCount = _cacheCapacity = 0;
    if (_sampleFile.loop_mode != LOOP_NONE) {
        const uint32_t wanted = min(VOICE_BUFFER_FRAMES, sample.loop_last_smp - sample.loop_first_smp);
        // One output-buffer worth of loop prefix; short loops fit completely.
        // Cache is optional and read on Core1 only, so PSRAM is suitable.
        if (_cacheAllocated < wanted) {
            heap_caps_free(_loopCache); _loopCache = nullptr; _cacheAllocated = 0;
            _loopCache = static_cast<PcmFrame*>(heap_caps_malloc(
                wanted * sizeof(PcmFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!_loopCache) _loopCache = static_cast<PcmFrame*>(heap_caps_malloc(
                wanted * sizeof(PcmFrame), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (_loopCache) _cacheAllocated = wanted;
        }
        if (_loopCache) _cacheCapacity = wanted;
        else ESP_LOGW("VOICE", "Loop prefix cache unavailable; streaming without cache");
    }
    _speed = fminf(MAX_PLAYBACK_SPEED, fmaxf(0.001f, sample.speed * _speedModifier));
    _amp = sample.amp * 0.000033f; // preserve the existing sampler gain
    if (*_normalized) _amp *= float(velocity) / 127.0f;
    _killScoreCoef = 1.0f / (float(frames) * float(velocity ? velocity : 1));
    AmpEnv.retrigger(Adsr::END_NOW);
    _active.store(true);
    resumeAudio();
}

void Voice::end(Adsr::eEnd_t type) {
    if (!isActive()) return;
    if (type == Adsr::END_REGULAR && (_pressed || *_sustain)) return;
    if (type == Adsr::END_REGULAR) _producer.cursor.release();
    pauseAudio();
#if DEBUG_AUDIO_DIAGNOSTICS
    const auto reason = type == Adsr::END_NOW ? audio_debug::FORCE_STOP :
        (type == Adsr::END_REGULAR ? audio_debug::RELEASE : audio_debug::FAST_RELEASE);
    const uint32_t level = audio_debug::level(_lastL, _lastR);
    audio_debug::control.mark(reason, level);
    if (type == Adsr::END_NOW && level >= 10000)
        audio_debug::control.event(reason, micros() / 1000, _playedFrames.load(), level, my_id, _midiNote);
#endif
    AmpEnv.end(type);
#if DEBUG_AUDIO_DIAGNOSTICS
    if (type == Adsr::END_REGULAR) {
        _debugReleasePending = true;
        _debugReleaseNote = _midiNote;
    }
#endif
    if (type == Adsr::END_NOW) _active.store(false);
    else if (type != Adsr::END_REGULAR) _dying.store(true);
    resumeAudio();
}

void Voice::feed() {
    if (!isActive() || _producedFinal) return;
    Slot& slot = _slots[_writeSlot];
    if (slot.ready.load(std::memory_order_acquire)) return;
    const uint32_t began = micros();
    const sample_source_t& source = *_sampleSource;
    SdStreamReader reader(source.sectors, source.size, sdStreamWindow(),
                          SD_STREAM_SECTORS, SdSectorRead{_card});
    const unsigned channelBytes = source.bit_depth / 8;
    const unsigned frameBytes = source.channels * channelBytes;
    const uint32_t sourceFrames = source.data_size / frameBytes;
    uint32_t remaining = VOICE_BUFFER_FRAMES + 1;
    auto read = [&](uint32_t index, bool reverse, PcmFrame& frame) -> bool {
        const uint32_t budget = remaining--;
        if (index >= _cacheFirst && index - _cacheFirst < _cacheCount) {
            frame = _loopCache[index - _cacheFirst]; return true;
        }
        const uint64_t offset = uint64_t(source.byte_offset) + uint64_t(index) * frameBytes;
        uint32_t span = sourceFrames - index;
        if (reverse) span = index - _sampleFile.loop_first_smp + 1;
        else if (_sampleFile.loop_mode != LOOP_NONE && index < _sampleFile.loop_last_smp)
            span = _sampleFile.loop_last_smp - index;
        span = min(span, budget);
        if (offset + frameBytes > source.size) return false;
        const uint8_t* bytes = reader.view(uint32_t(offset), frameBytes, reverse, span * frameBytes);
        if (!bytes) return false;
        frame.l = decodePcm(bytes, source.bit_depth);
        frame.r = source.channels == 1 ? frame.l : decodePcm(bytes + channelBytes, source.bit_depth);
        if (index >= _cacheFirst && index - _cacheFirst == _cacheCount && _cacheCount < _cacheCapacity)
            _loopCache[_cacheCount++] = frame;
        return true;
    };
    slot.count = _producer.fill(slot.pcm, VOICE_BUFFER_FRAMES, slot.final, read);
    _producedFinal = slot.final;
    if (_producer.failed()) {
#if DEBUG_AUDIO_DIAGNOSTICS
        audio_debug::control.mark(audio_debug::READ_ERROR);
        audio_debug::control.event(audio_debug::READ_ERROR, micros() / 1000, _playedFrames.load(), 0, my_id, _midiNote);
#else
        ESP_LOGE("VOICE", "SD read failed for voice %d; ending stream", my_id);
#endif
    }
    _maxFeedMicros = max(_maxFeedMicros, uint32_t(micros() - began));
    slot.ready.store(true, std::memory_order_release);
    _writeSlot ^= 1;
}

void IRAM_ATTR Voice::getSample(float& left, float& right) {
    left = right = 0;
    _rendering.store(true);
    if (!_enabled.load()) {
        // Core1 has exclusive access to the voice state. Preserve continuity
        // without reading anything Core1 may currently modify.
        left = _pauseHoldL; right = _pauseHoldR;
#if DEBUG_AUDIO_DIAGNOSTICS
        if (_active.load()) {
            if (!_debugPaused) {
                const uint32_t level = audio_debug::level(left, right);
                audio_debug::audio.mark(audio_debug::PAUSE_GAP, level);
                // Do not read control-owned note/file state while disabled. This
                // records a prevented gap: the held sample is returned.
            }
            ++_debugPauseSamples;
        }
        _debugPaused = true;
#endif
        _rendering.store(false); return;
    }
    if (!_active.load()) {
#if DEBUG_AUDIO_DIAGNOSTICS
        _debugPaused = true;
        _debugPauseSamples = 0; _debugResumePending = false;
#endif
        _pauseHoldL = _pauseHoldR = 0;
        _rendering.store(false); return;
    }
#if DEBUG_AUDIO_DIAGNOSTICS
    if (_debugPauseSamples) {
        audio_debug::audio.mark(audio_debug::PAUSE_SAMPLES, _debugPauseSamples, _debugPauseSamples);
        if (_debugPauseSamples > 1) audio_debug::audio.mark(audio_debug::PAUSE_GT1);
        _debugResumePending = true;
        _debugPauseSamples = 0;
    }
    _debugPaused = false;
#endif
    if (_underrunFade) {
        const float gain = float(--_underrunFade) / DMA_BUF_LEN;
        left = _lastL * gain; right = _lastR * gain;
        if (!_underrunFade) _active.store(false);
        _rendering.store(false); return;
    }
    while (true) {
        Slot& slot = _slots[_readSlot];
        if (!slot.ready.load(std::memory_order_acquire)) {
            if (_hasPlayed) {
#if DEBUG_AUDIO_DIAGNOSTICS
                const uint32_t level = audio_debug::level(_lastL, _lastR);
                audio_debug::audio.mark(audio_debug::UNDERRUN, level);
                audio_debug::audio.event(audio_debug::UNDERRUN, micros() / 1000, _playedFrames.load(), level, my_id, _midiNote);
#endif
                _underruns.store(_underruns.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                _underrunFade = DMA_BUF_LEN;
                _dying.store(true); left = _lastL; right = _lastR;
            }
            _rendering.store(false); return;
        }
#if DEBUG_AUDIO_DIAGNOSTICS
        if (_debugExpectFirst && slot.count) {
            if (slot.pcm[0].l != _debugExpectedFirst.l || slot.pcm[0].r != _debugExpectedFirst.r) {
                const uint32_t diff = max(abs(int(slot.pcm[0].l) - _debugExpectedFirst.l),
                                          abs(int(slot.pcm[0].r) - _debugExpectedFirst.r));
                audio_debug::audio.mark(audio_debug::SEAM_ERROR, diff);
                audio_debug::audio.event(audio_debug::SEAM_ERROR, micros() / 1000, _playedFrames.load(), diff, my_id, _midiNote);
            }
            _debugExpectFirst = false;
        }
#endif
        if (_position < slot.count) break;
        if (slot.final) {
#if DEBUG_AUDIO_DIAGNOSTICS
            const uint32_t level = audio_debug::level(_lastL, _lastR);
            audio_debug::audio.mark(audio_debug::EOF_STOP, level);
            if (level >= 10000) audio_debug::audio.event(audio_debug::EOF_STOP, micros() / 1000, _playedFrames.load(), level, my_id, _midiNote);
#endif
            _active.store(false); _rendering.store(false); return;
        }
#if DEBUG_AUDIO_DIAGNOSTICS
        _debugExpectedFirst = slot.pcm[slot.count];
        _debugExpectFirst = true;
        audio_debug::audio.mark(audio_debug::BUFFER_SWAP);
#endif
        _position -= slot.count;
        slot.ready.store(false, std::memory_order_release);
        _readSlot ^= 1;
    }
    Slot& slot = _slots[_readSlot];
    const uint32_t index = uint32_t(_position);
    const float fraction = _position - index;
    const PcmFrame a = slot.pcm[index], b = slot.pcm[index + 1];
    const float gain = AmpEnv.process() * _amp;
    left = (float(a.l) + float(int(b.l) - a.l) * fraction) * gain;
    right = (float(a.r) + float(int(b.r) - a.r) * fraction) * gain;
#if DEBUG_AUDIO_DIAGNOSTICS
    if (_debugResumePending) {
        const uint32_t jump = audio_debug::level(left - _pauseHoldL, right - _pauseHoldR);
        audio_debug::audio.mark(audio_debug::RESUME_JUMP, jump);
        if (jump >= 5000)
            audio_debug::audio.event(audio_debug::RESUME_JUMP, micros() / 1000,
                                     _playedFrames.load(), jump, my_id, _midiNote);
        _debugResumePending = false;
    }
    if (_debugReleasePending) {
        const uint32_t jump = audio_debug::level(left - _debugLastL, right - _debugLastR);
        audio_debug::audio.mark(audio_debug::RELEASE_JUMP, jump);
        if (jump >= 5000)
            audio_debug::audio.event(audio_debug::RELEASE_JUMP, micros() / 1000,
                                     _playedFrames.load(), jump, my_id, _debugReleaseNote);
        _debugReleasePending = false;
    }
#endif
    _pauseHoldL = left; _pauseHoldR = right;
#if DEBUG_AUDIO_DIAGNOSTICS
    if (!_hasPlayed) audio_debug::audio.mark(audio_debug::START, audio_debug::level(left, right));
    _debugLastL = left; _debugLastR = right;
#endif
    _lastL = left; _lastR = right; _hasPlayed = true;
    const uint32_t advance = uint32_t(_position + _speed) - index;
    _position += _speed;
    _framesRemaining.store(_position < slot.count ? uint32_t(slot.count - _position) : 0, std::memory_order_relaxed);
    const uint32_t played = _playedFrames.load(std::memory_order_relaxed);
    _playedFrames.store(played > UINT32_MAX - advance ? UINT32_MAX : played + advance, std::memory_order_relaxed);
    if (AmpEnv.isIdle()) {
#if DEBUG_AUDIO_DIAGNOSTICS
        audio_debug::audio.mark(audio_debug::ENVELOPE_END);
#endif
        _active.store(false);
    }
    _rendering.store(false);
}

uint32_t Voice::hunger() {
    if (!isActive() || _producedFinal || _slots[_writeSlot].ready.load(std::memory_order_acquire)) return 0;
    if (!_slots[_writeSlot ^ 1].ready.load(std::memory_order_acquire)) return UINT32_MAX;
    // Highest priority = shortest time until the published buffer runs out.
    const uint32_t remaining = _framesRemaining.load(std::memory_order_relaxed);
    return 1 + uint32_t(1000000.0f * _speed / float(remaining + 1));
}

float Voice::getKillScore() const {
    return !isActive() || isDying() ? 0 : float(_playedFrames.load()) * _killScoreCoef;
}
void Voice::setPitch(float modifier) {
    if (!isfinite(modifier) || modifier <= 0) return;
    pauseAudio(); _speedModifier = modifier;
    _speed = fminf(MAX_PLAYBACK_SPEED, fmaxf(0.001f, _sampleFile.speed * modifier)); resumeAudio();
}
void Voice::setAttackTime(float v) { pauseAudio(); AmpEnv.setAttackTime(v); resumeAudio(); }
void Voice::setDecayTime(float v) { pauseAudio(); AmpEnv.setDecayTime(v); resumeAudio(); }
void Voice::setReleaseTime(float v) { pauseAudio(); AmpEnv.setReleaseTime(v); resumeAudio(); }
void Voice::setSustainLevel(float v) { pauseAudio(); AmpEnv.setSustainLevel(v); resumeAudio(); }
