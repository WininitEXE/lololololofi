// mfdec.h - top level audio decoding entry point
#pragma once
#include "common.h"

// Decodes any supported audio file into a normalized float buffer.
// Order of attempts: Media Foundation (mp3/flac/m4a/aac/wma/wav/...)
// -> built-in WAV reader -> built-in Ogg reader (vorbis/opus).
bool decodeAudioFile(const std::wstring& path, AudioBuffer& out, std::wstring& err, const ProgressFn& progress);
bool decodeWithMediaFoundation(const std::wstring& path, AudioBuffer& out, std::wstring& err, const ProgressFn& progress);
