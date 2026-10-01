#pragma once
#include "sector_reader.h"
#include "pcm_stream.h"
#ifdef SAMPLER_HOST_TEST
#include "tests/host_platform.h"
#else
#include "sdmmc.h"
#endif

// Used by setup before tasks start and by control_task only.
// Fit a full decoded voice buffer even for stereo 32-bit PCM, including
// interpolation lookahead and an unaligned first sector. Shared, not per voice.
static constexpr uint32_t SD_STREAM_SECTORS =
    ((READ_BUF_SECTORS * BYTES_PER_SECTOR / sizeof(PcmFrame) + 1) * 8
     + 2 * BYTES_PER_SECTOR - 2) / BYTES_PER_SECTOR;
inline uint8_t* sdStreamWindow() {
    static uint8_t* window = static_cast<uint8_t*>(heap_caps_aligned_alloc(
#if defined(CONFIG_IDF_TARGET_ESP32P4)
        CONFIG_CACHE_L1_CACHE_LINE_SIZE,
#else
        BYTE_ALIGN
#endif
        SD_STREAM_SECTORS * BYTES_PER_SECTOR, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    return window;
}
struct SdSectorRead {
    SDMMC_FAT32* card;
    bool operator()(uint8_t* out, uint32_t first, uint32_t count) const {
        return card->read_block(out, first, count) == ESP_OK;
    }
};
using SdStreamReader = SectorReader<std::vector<chain_t>, SdSectorRead>;
