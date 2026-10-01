#pragma once
#include <stdint.h>
#include <string.h>

// Bounded random access over FAT extents; storage is shared by Core1 readers.
template<class Chains, class ReadSectors>
class SectorReader {
public:
    SectorReader(const Chains& chains, uint32_t size, uint8_t* storage,
                 uint32_t sectors, ReadSectors read)
        : _chains(chains), _size(size), _storage(storage), _capacity(sectors), _read(read) {}
    // Short contiguous PCM frame view, valid until the next load. Avoid a
    // per-frame memcpy; include all bytes when a frame crosses a sector edge.
    const uint8_t* view(uint32_t offset, uint32_t length, bool reverse,
                        uint32_t readAheadBytes) {
        if (!length || uint64_t(offset) + length > _size || !_storage || !_capacity)
            return nullptr;
        if (offset >= _begin && uint64_t(offset) + length <= uint64_t(_begin) + _valid)
            return _storage + offset - _begin;
        uint32_t sector = offset / 512;
        const uint32_t last = uint32_t((uint64_t(offset) + length - 1) / 512);
        if (last - sector + 1 > _capacity) return nullptr;
        uint32_t count;
        if (reverse) {
            const uint32_t behind = readAheadBytes > length ? readAheadBytes - length : 0;
            const uint32_t first = offset > behind ? (offset - behind) / 512 : 0;
            sector = last >= _capacity - 1 ? last - (_capacity - 1) : 0;
            if (sector < first) sector = first;
            if (sector > offset / 512) sector = offset / 512;
            count = last - sector + 1;
        } else {
            const uint64_t bytes = uint64_t(offset % 512) +
                (readAheadBytes > length ? readAheadBytes : length);
            count = uint32_t((bytes + 511) / 512);
            if (count > _capacity) count = _capacity;
        }
        if (!load(sector, count) || uint64_t(offset) + length > uint64_t(_begin) + _valid)
            return nullptr;
        return _storage + offset - _begin;
    }
    bool read(uint32_t offset, void* destination, uint32_t length, bool reverse = false,
              uint32_t readAheadBytes = UINT32_MAX) {
        if (uint64_t(offset) + length > _size || !_storage || !_capacity) return false;
        auto* out = static_cast<uint8_t*>(destination);
        while (length) {
            if (offset < _begin || uint64_t(offset) >= uint64_t(_begin) + _valid) {
                uint32_t sector = offset / 512;
                uint32_t count = _capacity;
                if (reverse) {
                    const uint32_t behind = readAheadBytes > length ? readAheadBytes - length : 0;
                    const uint32_t first = offset > behind ? (offset - behind) / 512 : 0;
                    uint32_t last = uint32_t((uint64_t(offset) + length - 1) / 512);
                    sector = last >= _capacity - 1 ? last - (_capacity - 1) : 0;
                    if (sector < first) sector = first;
                    if (sector > offset / 512) sector = offset / 512;
                    count = last - sector + 1;
                } else if (readAheadBytes != UINT32_MAX) {
                    const uint64_t desired = uint64_t(offset % 512) + (readAheadBytes > length ? readAheadBytes : length);
                    if ((desired + 511) / 512 < count) count = uint32_t((desired + 511) / 512);
                }
                if (!load(sector, count)) return false;
            }
            uint32_t n = _valid - (offset - _begin);
            if (n > length) n = length;
            memcpy(out, _storage + offset - _begin, n);
            out += n; offset += n; length -= n;
        }
        return true;
    }
private:
    bool load(uint32_t logical, uint32_t requested) {
        _valid = 0;
        const uint32_t fileSectors = uint32_t((uint64_t(_size) + 511) / 512);
        if (logical >= fileSectors) return false;
        uint32_t count = fileSectors - logical;
        if (count > _capacity) count = _capacity;
        if (count > requested) count = requested;
        uint32_t skip = logical, done = 0;
        for (const auto& chain : _chains) {
            if (chain.last < chain.first) return false;
            uint64_t available = uint64_t(chain.last) - chain.first + 1;
            if (skip >= available) { skip -= uint32_t(available); continue; }
            uint32_t n = uint32_t(available - skip);
            if (n > count - done) n = count - done;
            if (!_read(_storage + done * 512, chain.first + skip, n)) return false;
            done += n; skip = 0;
            if (done == count) break;
        }
        if (done != count) return false;
        _begin = logical * 512;
        const uint64_t remaining = uint64_t(_size) - _begin;
        _valid = remaining < uint64_t(done) * 512 ? uint32_t(remaining) : done * 512;
        return true;
    }
    const Chains& _chains;
    uint32_t _size;
    uint8_t* _storage;
    uint32_t _capacity, _begin = 0, _valid = 0;
    ReadSectors _read;
};
