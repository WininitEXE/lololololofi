// engine.h - shared conversion pipeline used by both the GUI and the CLI.
#pragma once
#include "common.h"
#include "onebit.h"

enum class OutFormat { Wav8 = 0, Wav16 = 1, Raw1Bit = 2, Dsf = 3 };

struct ConvertRequest {
    std::wstring input;
    std::wstring output;                      // empty -> derived from input
    OutFormat format = OutFormat::Wav8;
    ConvertOptions opt;                       // quantiser settings
    double level = 1.0;                       // output level (0..1)
    bool keepPreview = false;                 // retain PCM / bits for the GUI
};

struct ConvertResult {
    std::wstring outputPath;
    std::wstring requestedPath;               // what the user asked for
    bool redirected = false;                  // output had to move somewhere writable
    std::wstring redirectNote;                // explanation for the GUI/CLI
    AudioBuffer original;                     // for waveform + A/B preview
    BitStream bits;                           // 1-bit result (WAV/RAW outputs)
    int channels = 0;
    int sampleRate = 0;
    double seconds = 0;
    bool previewTruncated = false;
};

std::wstring defaultOutputPath(const std::wstring& input, OutFormat format);
std::wstring outFormatName(OutFormat f);
std::wstring outFormatExtension(OutFormat f);

bool runConversion(const ConvertRequest& req, ConvertResult& result, std::wstring& err, const ProgressFn& progress);

// Builds a small in-memory 16 bit WAV for SoundPlay preview (max 2 channels).
std::vector<uint8_t> buildPreviewWav(const AudioBuffer& buf);
std::vector<uint8_t> buildPreviewWavFromBits(const BitStream& bits);
