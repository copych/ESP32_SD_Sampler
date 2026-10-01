#pragma once
#include <stdint.h>
#include <string.h>
#include <vector>

namespace wavmeta {
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
struct Info {
    uint32_t offset = 0, bytes = 0, rate = 0, frames = 0;
    uint16_t channels = 0, bits = 0;
    int64_t first = -1, end = -1;
    const char* source = "defaults";
};
struct Region { uint32_t id, length; };
struct Cue { uint32_t id, frame; };
struct Loop { uint32_t first; uint64_t end; };

// read(offset, destination, length) must be bounded by the actual file size.
// Metadata collections are capped; no allocation follows untrusted counts.
template<class Read>
bool parse(uint32_t size, Read read, Info& result) {
    result = Info{};
    uint8_t b[64];
    if (size < 12 || !read(0, b, 12) || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return false;
    const uint64_t limit = uint64_t(u32(b + 4)) + 8;
    if (limit > size || limit < 12) return false;
    bool fmt = false, data = false, regionsTruncated = false;
    uint16_t align = 0;
    std::vector<Loop> loops;
    std::vector<Cue> cues;
    std::vector<Region> regions;
    for (uint64_t pos = 12; pos < limit;) {
        if (limit - pos < 8 || !read(uint32_t(pos), b, 8)) return false;
        uint32_t n = u32(b + 4);
        uint64_t payload = pos + 8, next = payload + n + (n & 1);
        if (next > limit) return false;
        if (!memcmp(b, "fmt ", 4)) {
            if (fmt || n < 16 || !read(uint32_t(payload), b, n < 40 ? n : 40)) return false;
            const uint16_t format = u16(b);
            result.channels = u16(b + 2); result.rate = u32(b + 4);
            align = u16(b + 12); result.bits = u16(b + 14);
            if (format == 0xfffe) {
                static const uint8_t pcmGuid[16] = {1,0,0,0,0,0,16,0,128,0,0,170,0,56,155,113};
                if (n < 40 || u16(b + 16) < 22 || u16(b + 18) != result.bits || memcmp(b + 24, pcmGuid, 16)) return false;
            } else if (format != 1) return false;
            if (result.channels < 1 || result.channels > 2 || !result.rate ||
                (result.bits != 8 && result.bits != 16 && result.bits != 24 && result.bits != 32) ||
                align != result.channels * (result.bits / 8)) return false;
            fmt = true;
        } else if (!memcmp(b, "data", 4)) {
            if (data) return false; // multiple PCM streams / wavl are unsupported
            result.offset = uint32_t(payload); result.bytes = n; data = true;
        } else if (!memcmp(b, "smpl", 4)) {
            if (n >= 36) {
                if (!read(uint32_t(payload), b, 36)) return false;
                uint32_t count = u32(b + 28);
                if (count > (n - 36) / 24) return false;
                for (uint32_t i = 0; i < count && loops.size() < 64; ++i) {
                    if (!read(uint32_t(payload + 36 + uint64_t(i) * 24), b, 24)) return false;
                    loops.push_back({u32(b + 8), uint64_t(u32(b + 12)) + 1});
                }
            }
        } else if (!memcmp(b, "cue ", 4)) {
            if (n < 4 || !read(uint32_t(payload), b, 4)) return false;
            uint32_t count = u32(b);
            if (count > (n - 4) / 24) return false;
            if (count > 64 - cues.size()) regionsTruncated = true;
            for (uint32_t i = 0; i < count && cues.size() < 64; ++i) {
                if (!read(uint32_t(payload + 4 + uint64_t(i) * 24), b, 24)) return false;
                if (!memcmp(b + 8, "data", 4) && u32(b + 12) == 0 && u32(b + 16) == 0)
                    cues.push_back({u32(b), u32(b + 20)});
            }
        } else if (!memcmp(b, "LIST", 4)) {
            if (n < 4 || !read(uint32_t(payload), b, 4)) return false;
            if (!memcmp(b, "adtl", 4)) {
                for (uint64_t sub = payload + 4; sub < payload + n;) {
                    if (payload + n - sub < 8 || !read(uint32_t(sub), b, 8)) return false;
                    const uint32_t len = u32(b + 4);
                    const uint64_t after = sub + 8 + len + (len & 1);
                    if (after > payload + n) return false;
                    if (!memcmp(b, "ltxt", 4) && len >= 20) {
                        if (!read(uint32_t(sub + 8), b, 20)) return false;
                        if (u32(b + 4)) {
                            if (regions.size() < 64) regions.push_back({u32(b), u32(b + 4)});
                            else regionsTruncated = true;
                        }
                    }
                    sub = after;
                }
            }
        }
        pos = next;
    }
    if (!fmt || !data || !result.bytes || result.bytes % align) return false;
    result.frames = result.bytes / align;
    for (const auto& loop : loops) {
        if (loop.first < loop.end && loop.end <= result.frames) {
            result.first = loop.first; result.end = loop.end; result.source = "smpl"; return true;
        }
    }
    // Exactly one usable region is unambiguous. Markers without lengths and
    // files with multiple candidate regions fall back to INI / full PCM.
    unsigned matches = 0;
    for (const auto& region : regions) for (const auto& cue : cues) {
        const uint64_t end = uint64_t(cue.frame) + region.length;
        if (cue.id == region.id && end <= result.frames) {
            result.first = cue.frame; result.end = end; ++matches;
        }
    }
    if (matches == 1 && !regionsTruncated) result.source = "cue/ltxt";
    else { result.first = result.end = -1; if (matches > 1) result.source = "ambiguous regions: defaults"; }
    return true;
}
} // namespace wavmeta
