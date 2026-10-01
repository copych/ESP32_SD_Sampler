#pragma once
#include <stdint.h>

// Changes the admission limit only. All allocated voices must still be mixed
// and refilled while they finish their release after the limit is reduced.
class AdaptivePolyphony {
public:
    void reset(uint8_t cap, uint32_t misses, uint32_t cpuPressure, uint32_t nowMs) {
        _cap = cap ? cap : 1;
        _limit = _cap;
        _lastMisses = misses;
        _lastCpuPressure = cpuPressure;
        _lastCheck = _lastPressure = nowMs;
        _lastReduction = nowMs - 1000;
        _cpuPending = false;
    }

    bool update(uint32_t misses, uint32_t cpuPressure, uint32_t nowMs) {
        if (uint32_t(nowMs - _lastCheck) < 250) return false;
        _lastCheck = nowMs;
        const bool sdChanged = misses != _lastMisses;
        const bool cpuChanged = cpuPressure != _lastCpuPressure;
        const uint32_t sdBurst = misses - _lastMisses;
        if (sdChanged || cpuChanged) {
            _lastMisses = misses;
            _lastCpuPressure = cpuPressure;
            _lastPressure = nowMs;
            if (cpuChanged) _cpuPending = true;
        }
        if (sdChanged) {
            // A real underrun gets immediate treatment. One reduction also
            // covers CPU pressure observed in the same interval.
            _cpuPending = false;
            const uint8_t floor = _cap < 4 ? _cap : 4;
            const uint8_t drop = sdBurst >= 4 ? 2 : 1;
            const uint8_t next = _limit > floor + drop ? uint8_t(_limit - drop) : floor;
            if (next == _limit) return false;
            _limit = next;
            _lastReduction = nowMs;
            return true;
        }
        // CPU pressure is deliberately gentler: let released voices leave the
        // mixer before deciding whether another reduction is necessary.
        if (_cpuPending && uint32_t(nowMs - _lastReduction) >= 1000) {
            _cpuPending = false;
            const uint8_t floor = _cap < 4 ? _cap : 4;
            if (_limit <= floor) return false;
            --_limit;
            _lastReduction = nowMs;
            return true;
        }
        if (!_cpuPending && _limit < _cap && uint32_t(nowMs - _lastPressure) >= 10000) {
            ++_limit;
            _lastPressure = nowMs;
            return true;
        }
        return false;
    }

    uint8_t limit() const { return _limit; }
    uint8_t cap() const { return _cap; }

private:
    uint8_t _cap = 1, _limit = 1;
    bool _cpuPending = false;
    uint32_t _lastMisses = 0, _lastCpuPressure = 0;
    uint32_t _lastCheck = 0, _lastPressure = 0, _lastReduction = 0;
};
