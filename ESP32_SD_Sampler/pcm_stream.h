#pragma once
#include <stdint.h>
#include <stddef.h>

// Source positions are PCM frames, never sector or byte offsets.
enum eLoopType_t { LOOP_NONE = 0, LOOP_FORWARD, LOOP_SUSTAIN, LOOP_PINGPONG };

struct PcmFrame { int16_t l = 0, r = 0; };

inline bool resolveLoop(uint32_t frames, int64_t metadataStart, int64_t metadataEnd,
                        int64_t iniStart, int64_t iniEnd, uint32_t& first, uint32_t& end) {
    const int64_t l = iniStart >= 0 ? iniStart : (metadataStart >= 0 ? metadataStart : 0);
    const int64_t r = iniEnd == 0 ? frames :
        (iniEnd > 0 ? iniEnd : (metadataEnd >= 0 ? metadataEnd : frames));
    if (l < 0 || r <= l || r > frames) return false;
    first = uint32_t(l); end = uint32_t(r);
    return true;
}

// Core1 owns this cursor. Boundary decisions are deferred until the next frame
// is requested, so release at an exact R boundary can still choose the tail.
class PcmCursor {
public:
    void reset(uint32_t frames, eLoopType_t mode, uint32_t first, uint32_t end) {
        _frames = frames; _mode = mode; _first = first; _end = end;
        _position = 0; _reverse = false; _released = false; _exited = false;
        if (first >= end || end > frames) _mode = LOOP_NONE;
    }
    void release() { _released = true; }
    bool next(uint32_t& frame) {
        if (_mode != LOOP_NONE && !_exited) {
            if (!_reverse && _position == _end) {
                if (_mode == LOOP_SUSTAIN && _released) _exited = true;
                else if (_mode == LOOP_PINGPONG && _end - _first > 1) {
                    _reverse = true; _position = _end - 2;
                } else _position = _first;
            } else if (_reverse && _position < _first) {
                _reverse = false; _position = _first + 1;
            }
        }
        if (_position < 0 || uint64_t(_position) >= _frames) return false;
        frame = uint32_t(_position);
        _position += _reverse ? -1 : 1;
        return true;
    }
    bool reverse() const { return _reverse; }
private:
    int64_t _position = 0;
    uint32_t _frames = 0, _first = 0, _end = 0;
    eLoopType_t _mode = LOOP_NONE;
    bool _reverse = false, _released = false, _exited = false;
};

// Every published buffer owns its interpolation neighbour. The neighbour is
// carried into the next buffer as frame zero, not generated a second time.
class PcmProducer {
public:
    PcmCursor cursor;
    void reset(uint32_t frames, eLoopType_t mode, uint32_t first, uint32_t end) {
        cursor.reset(frames, mode, first, end);
        _carryValid = false; _finished = false; _error = false;
    }
    template<class ReadFrame>
    uint32_t fill(PcmFrame* out, uint32_t capacity, bool& final, ReadFrame read) {
        uint32_t count = 0;
        if (_carryValid) { out[count++] = _carry; _carryValid = false; }
        while (count <= capacity && !_finished) {
            uint32_t index;
            if (!cursor.next(index)) { _finished = true; break; }
            if (!read(index, cursor.reverse(), out[count])) {
                _error = true; _finished = true; break;
            }
            ++count;
        }
        if (count > capacity) {
            _carry = out[capacity]; _carryValid = true; final = false;
            return capacity;
        }
        final = true;
        // Hold the final frame for interpolation at fractional EOF positions.
        out[count] = count ? out[count - 1] : PcmFrame{};
        return count;
    }
    bool failed() const { return _error; }
private:
    PcmFrame _carry;
    bool _carryValid = false, _finished = false, _error = false;
};

inline int16_t decodePcm(const uint8_t* p, unsigned bits) {
    if (bits == 8) return int16_t((int(p[0]) - 128) * 256);
    // Keep the most significant 16 bits of 16/24/32-bit integer PCM.
    p += bits / 8 - 2;
    return int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}
