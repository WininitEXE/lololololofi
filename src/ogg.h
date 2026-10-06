// ogg.h - built-in Ogg container demuxer (Vorbis / Opus / FLAC payload)
#pragma once
#include "common.h"

// Low level: one reassembled packet from an Ogg page.
struct OggPacket {
    std::vector<uint8_t> data;
    bool bos = false, eos = false;
    int64_t granulePos = -1;
    uint32_t serial = 0;
};

// Decodes .ogg/.oga/.opus with the built-in demuxer plus a Media Foundation
// decoder MFT for the codec payload.
bool decodeOggFile(const std::wstring& path, AudioBuffer& out, std::wstring& err, const ProgressFn& progress);

// Pushes compressed packets through a decoder MFT and collects float PCM.
bool decodePacketsWithMFT(const GUID& subtype, const std::vector<uint8_t>& extradata,
                          int channels, int sampleRate,
                          const std::vector<OggPacket>& packets,
                          const std::vector<int64_t>& granulePositions,
                          AudioBuffer& out, std::wstring& err, const ProgressFn& progress);
