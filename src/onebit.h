// onebit.h - the actual "destroy the audio into 1 bit" DSP
#pragma once
#include "common.h"

enum class QuantMode { Hard = 0, SD1 = 1, SD2 = 2, SD3 = 3, SD4 = 4 };
enum class DitherLevel { None = 0, Light = 1, Strong = 2 };

struct ConvertOptions {
    QuantMode mode = QuantMode::Hard;      // hard sign slice or sigma-delta orders 1..4
    DitherLevel dither = DitherLevel::None;
    double gain = 1.0;                     // input gain
    int targetRate = 0;                    // 0 = keep source rate, else resample first
};

// Convert PCM into a 1-bit stream (one byte per sample, values 0/1).
bool convertTo1Bit(const AudioBuffer& in, const ConvertOptions& opt, BitStream& out,
                   std::wstring& err, const ProgressFn& progress);

// Simple (and deliberately destructive) box/linear resampler.
bool resampleBuffer(const AudioBuffer& in, int outRate, AudioBuffer& out, std::wstring& err);

// Noise shaped 1-bit modulation at a DSD rate; returns DSF block interleaved bytes.
bool modulateToDsfData(const AudioBuffer& in, int dsdRate, int order, std::vector<uint8_t>& outBytes,
                       int& outChannels, std::wstring& err, const ProgressFn& progress);

std::wstring quantModeName(QuantMode m);
