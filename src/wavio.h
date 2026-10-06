// wavio.h - WAV reading/writing, raw 1-bit dump and DSF (DSD) container writer
#pragma once
#include "common.h"

bool decodeWavFile(const std::wstring& path, AudioBuffer& out, std::wstring& err);

// Write interleaved 16-bit PCM samples into a .wav file.
bool writeWavPcm16(const std::wstring& path, const std::vector<int16_t>& samples, int channels, int rate, std::wstring& err);
// Write 8-bit unsigned PCM samples (already encoded as bytes).
bool writeWavPcm8(const std::wstring& path, const std::vector<uint8_t>& samples, int channels, int rate, std::wstring& err);
// Write a raw packed 1-bit stream (no header).
bool writeRaw1Bit(const std::wstring& path, const std::vector<uint8_t>& packed, std::wstring& err);
// Write a DSF (DSD stream file) - true 1-bit container.
bool writeDsf(const std::wstring& path, const std::vector<uint8_t>& packedPerChannel, int channels, int dsdRate, std::wstring& err);

// Pack one-byte-per-bit (0/1) streams, LSB first, as used by raw/DSF output.
std::vector<uint8_t> packBitsLsbFirst(const std::vector<uint8_t>& bits);
// Build a playable 16-bit PCM wav image in memory (for SoundPlay preview).
std::vector<uint8_t> makeWavPcm16InMemory(const std::vector<int16_t>& samples, int channels, int rate);
