// common.h - shared types and utilities for the 1-bit audio destroyer
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// Decoded audio: normalized float samples, interleaved.
struct AudioBuffer {
    std::vector<float> data;
    int channels = 0;
    int sampleRate = 0;

    size_t frames() const { return channels > 0 ? data.size() / (size_t)channels : 0; }
    double seconds() const { return sampleRate > 0 ? (double)frames() / (double)sampleRate : 0.0; }
    void reset() { data.clear(); channels = 0; sampleRate = 0; }
    bool empty() const { return data.empty() || channels <= 0 || sampleRate <= 0; }
};

// One byte per 1-bit sample (0 or 1), interleaved by channel.
struct BitStream {
    std::vector<uint8_t> bits;
    int channels = 0;
    int sampleRate = 0;

    size_t frames() const { return channels > 0 ? bits.size() / (size_t)channels : 0; }
    double seconds() const { return sampleRate > 0 ? (double)frames() / (double)sampleRate : 0.0; }
    void reset() { bits.clear(); channels = 0; sampleRate = 0; }
};

// Progress callback: fraction 0..1 plus a human readable status. Return false to cancel.
typedef std::function<bool(double, const std::wstring&)> ProgressFn;

// Minimal COM smart pointer (MinGW friendly, no ATL).
template <class T>
class ComPtr {
public:
    ComPtr() : p_(nullptr) {}
    ~ComPtr() { if (p_) p_->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** operator&() { return &p_; }
    T* operator->() const { return p_; }
    T* get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T* detach() { T* t = p_; p_ = nullptr; return t; }
    void attach(T* p) { if (p_) p_->Release(); p_ = p; }

private:
    T* p_;
};

std::string  toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);
std::wstring baseName(const std::wstring& path);
std::wstring dirName(const std::wstring& path);
std::wstring stripExt(const std::wstring& path);
std::wstring extLower(const std::wstring& path);
std::wstring formatSeconds(double sec);
std::wstring formatBytes(unsigned long long bytes);
bool fileExists(const std::wstring& path);
bool dirExists(const std::wstring& path);

// Win32 error text, e.g. L"拒绝访问 (错误 5)"
std::wstring win32ErrorText(unsigned long err);
// Creates the directory (and parents) if needed.
bool ensureDirectoryExists(const std::wstring& dir, std::wstring& err);
// True when a temporary file can actually be created in that directory.
bool isDirectoryWritable(const std::wstring& dir, std::wstring& err);
// Known folder (FOLDERID_Music / Desktop / Documents); empty when unavailable.
std::wstring knownFolderPath(int which);   // 0=Music 1=Desktop 2=Documents 3=Temp
std::wstring tempDirPath();
