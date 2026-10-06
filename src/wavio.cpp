#include "wavio.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace {

#pragma pack(push, 1)
struct RiffHeader { char riff[4]; uint32_t size; char wave[4]; };
struct ChunkHeader { char id[4]; uint32_t size; };
struct FmtChunk {
    uint16_t audioFormat;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
struct DsfHeader {
    char  dsdId[4];      uint64_t dsdSize;   uint64_t fileSize;  uint64_t metadataPtr;
    char  fmtId[4];      uint64_t fmtSize;   uint32_t formatVersion; uint32_t formatId;
    uint32_t channelType; uint32_t channelNum; uint32_t sampleRate; uint32_t bitsPerSample;
    uint64_t sampleCount; uint32_t blockSize; uint32_t reserved;
    char  dataId[4];     uint64_t dataSize;
};
#pragma pack(pop)

bool readAll(const std::wstring& path, std::vector<uint8_t>& data, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"无法打开文件 " + path + L": " + win32ErrorText(GetLastError()); return false; }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); err = L"无法读取文件大小"; return false; }
    if (sz.QuadPart <= 0 || sz.QuadPart > (LONGLONG)2 * 1024 * 1024 * 1024) { CloseHandle(h); err = L"文件为空或过大"; return false; }
    data.resize((size_t)sz.QuadPart);
    size_t total = 0;
    while (total < data.size()) {
        DWORD want = (DWORD)std::min<size_t>(data.size() - total, 1u << 20);
        DWORD got = 0;
        if (!ReadFile(h, data.data() + total, want, &got, nullptr) || got == 0) { CloseHandle(h); err = L"读取文件失败"; return false; }
        total += got;
    }
    CloseHandle(h);
    return true;
}

bool writeAll(const std::wstring& path, const void* data, size_t size, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"无法创建文件 " + path + L": " + win32ErrorText(GetLastError()); return false; }
    const uint8_t* p = (const uint8_t*)data;
    size_t total = 0;
    while (total < size) {
        DWORD want = (DWORD)std::min<size_t>(size - total, 1u << 20);
        DWORD wrote = 0;
        if (!WriteFile(h, p + total, want, &wrote, nullptr) || wrote == 0) { CloseHandle(h); err = L"写入文件失败: " + path; return false; }
        total += wrote;
    }
    CloseHandle(h);
    return true;
}

double decodeLaw(int16_t v, bool alaw) {
    if (alaw) {
        v ^= 0x55;
        int sign = (v & 0x80) ? -1 : 1;
        int exponent = (v >> 4) & 0x07;
        int mantissa = v & 0x0F;
        int magnitude = exponent == 0 ? (mantissa << 4) + 8 : ((mantissa << 4) + 0x108) << (exponent - 1);
        return (double)(sign * magnitude) / 32768.0;
    }
    v = ~v;
    int sign = (v & 0x80) ? -1 : 1;
    int exponent = (v >> 4) & 0x07;
    int mantissa = v & 0x0F;
    int magnitude = ((mantissa << 3) + 0x84) << exponent;
    magnitude -= 0x84;
    return (double)(sign * magnitude) / 32768.0;
}

} // namespace

bool decodeWavFile(const std::wstring& path, AudioBuffer& out, std::wstring& err) {
    std::vector<uint8_t> file;
    if (!readAll(path, file, err)) return false;
    if (file.size() < 44 || memcmp(file.data(), "RIFF", 4) != 0 || memcmp(file.data() + 8, "WAVE", 4) != 0) {
        err = L"不是有效的 WAV 文件";
        return false;
    }

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();
    FmtChunk fmt = {};
    bool haveFmt = false;
    uint16_t subFormat = 0;

    const uint8_t* dataPtr = nullptr;
    size_t dataSize = 0;

    while (p + 8 <= end) {
        ChunkHeader ch;
        memcpy(&ch, p, 8);
        const uint8_t* body = p + 8;
        size_t size = ch.size;
        if (body + size > end) size = (size_t)(end - body);
        if (memcmp(ch.id, "fmt ", 4) == 0 && size >= 16) {
            memcpy(&fmt, body, 16);
            haveFmt = true;
            if (fmt.audioFormat == 0xFFFE && size >= 40) {
                subFormat = *(const uint16_t*)(body + 24);
                fmt.audioFormat = subFormat;
            }
        } else if (memcmp(ch.id, "data", 4) == 0) {
            dataPtr = body;
            dataSize = size;
        }
        p = body + size + (size & 1);
    }

    if (!haveFmt || !dataPtr) { err = L"WAV 文件缺少 fmt/data 块"; return false; }
    int channels = fmt.channels;
    int rate = (int)fmt.sampleRate;
    if (channels <= 0 || channels > 64 || rate <= 0) { err = L"WAV 格式参数无效"; return false; }

    out.reset();
    out.channels = channels;
    out.sampleRate = rate;

    const int bps = fmt.bitsPerSample;
    if (fmt.audioFormat == 1) { // PCM
        if (bps == 8) {
            size_t n = dataSize;
            out.data.resize(n);
            for (size_t i = 0; i < n; i++) out.data[i] = ((int)dataPtr[i] - 128) / 128.0f;
        } else if (bps == 16) {
            size_t n = dataSize / 2;
            out.data.resize(n);
            const int16_t* s = (const int16_t*)dataPtr;
            for (size_t i = 0; i < n; i++) out.data[i] = (float)(s[i] / 32768.0);
        } else if (bps == 24) {
            size_t n = dataSize / 3;
            out.data.resize(n);
            for (size_t i = 0; i < n; i++) {
                int32_t v = (dataPtr[i * 3] << 8) | (dataPtr[i * 3 + 1] << 16) | (dataPtr[i * 3 + 2] << 24);
                out.data[i] = (float)(v / 2147483648.0);
            }
        } else if (bps == 32) {
            size_t n = dataSize / 4;
            out.data.resize(n);
            const int32_t* s = (const int32_t*)dataPtr;
            for (size_t i = 0; i < n; i++) out.data[i] = (float)(s[i] / 2147483648.0);
        } else {
            err = L"不支持的 WAV 位深 (仅支持 8/16/24/32 位)";
            return false;
        }
    } else if (fmt.audioFormat == 3) { // IEEE float
        size_t n = dataSize / 4;
        out.data.resize(n);
        const float* s = (const float*)dataPtr;
        for (size_t i = 0; i < n; i++) out.data[i] = s[i];
    } else if (fmt.audioFormat == 6 || fmt.audioFormat == 7) { // A-law / mu-law
        size_t n = dataSize;
        out.data.resize(n);
        for (size_t i = 0; i < n; i++) out.data[i] = (float)decodeLaw(dataPtr[i], fmt.audioFormat == 6);
    } else {
        err = L"不支持的 WAV 编码格式 (code " + std::to_wstring(fmt.audioFormat) + L")";
        return false;
    }
    return true;
}

bool writeWavPcm16(const std::wstring& path, const std::vector<int16_t>& samples, int channels, int rate, std::wstring& err) {
    RiffHeader rh = { {'R','I','F','F'}, 0, {'W','A','V','E'} };
    ChunkHeader fh = { {'f','m','t',' '}, 16 };
    FmtChunk fmt = {};
    fmt.audioFormat = 1;
    fmt.channels = (uint16_t)channels;
    fmt.sampleRate = (uint32_t)rate;
    fmt.bitsPerSample = 16;
    fmt.blockAlign = (uint16_t)(channels * 2);
    fmt.byteRate = (uint32_t)(rate * channels * 2);
    ChunkHeader dh = { {'d','a','t','a'}, (uint32_t)(samples.size() * 2) };
    rh.size = 4 + (8 + 16) + (8 + (uint32_t)(samples.size() * 2));

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"无法创建文件 " + path + L": " + win32ErrorText(GetLastError()); return false; }
    DWORD wrote = 0;
    bool ok = WriteFile(h, &rh, sizeof(rh), &wrote, nullptr) &&
              WriteFile(h, &fh, sizeof(fh), &wrote, nullptr) &&
              WriteFile(h, &fmt, sizeof(fmt), &wrote, nullptr) &&
              WriteFile(h, &dh, sizeof(dh), &wrote, nullptr);
    if (ok) {
        const uint8_t* p = (const uint8_t*)samples.data();
        size_t total = samples.size() * 2;
        size_t done = 0;
        while (done < total) {
            DWORD want = (DWORD)std::min<size_t>(total - done, 1u << 20);
            if (!WriteFile(h, p + done, want, &wrote, nullptr) || wrote == 0) { ok = false; break; }
            done += wrote;
        }
    }
    CloseHandle(h);
    if (!ok) err = L"写入 WAV 失败: " + path;
    return ok;
}

bool writeWavPcm8(const std::wstring& path, const std::vector<uint8_t>& samples, int channels, int rate, std::wstring& err) {
    RiffHeader rh = { {'R','I','F','F'}, 0, {'W','A','V','E'} };
    ChunkHeader fh = { {'f','m','t',' '}, 16 };
    FmtChunk fmt = {};
    fmt.audioFormat = 1;
    fmt.channels = (uint16_t)channels;
    fmt.sampleRate = (uint32_t)rate;
    fmt.bitsPerSample = 8;
    fmt.blockAlign = (uint16_t)channels;
    fmt.byteRate = (uint32_t)(rate * channels);
    ChunkHeader dh = { {'d','a','t','a'}, (uint32_t)samples.size() };
    rh.size = 4 + (8 + 16) + (8 + (uint32_t)samples.size());

    std::vector<uint8_t> blob;
    blob.reserve(sizeof(rh) + sizeof(fh) + sizeof(fmt) + sizeof(dh) + samples.size());
    blob.insert(blob.end(), (uint8_t*)&rh, (uint8_t*)&rh + sizeof(rh));
    blob.insert(blob.end(), (uint8_t*)&fh, (uint8_t*)&fh + sizeof(fh));
    blob.insert(blob.end(), (uint8_t*)&fmt, (uint8_t*)&fmt + sizeof(fmt));
    blob.insert(blob.end(), (uint8_t*)&dh, (uint8_t*)&dh + sizeof(dh));
    blob.insert(blob.end(), samples.begin(), samples.end());
    return writeAll(path, blob.data(), blob.size(), err);
}

bool writeRaw1Bit(const std::wstring& path, const std::vector<uint8_t>& packed, std::wstring& err) {
    return writeAll(path, packed.data(), packed.size(), err);
}

bool writeDsf(const std::wstring& path, const std::vector<uint8_t>& packedPerChannel, int channels, int dsdRate, std::wstring& err) {
    // packedPerChannel holds the already block-interleaved DSD data.
    DsfHeader hd = {};
    memcpy(hd.dsdId, "DSD ", 4);
    hd.dsdSize = 28;
    hd.fileSize = 28 + 52 + 12 + packedPerChannel.size();
    hd.metadataPtr = 0;
    memcpy(hd.fmtId, "fmt ", 4);
    hd.fmtSize = 52;
    hd.formatVersion = 1;
    hd.formatId = 0;
    hd.channelType = (channels == 1) ? 1 : (channels == 2 ? 2 : 0);
    hd.channelNum = (uint32_t)channels;
    hd.sampleRate = (uint32_t)dsdRate;
    hd.bitsPerSample = 1;
    hd.sampleCount = (uint64_t)(packedPerChannel.size() / (size_t)(channels > 0 ? channels : 1)) * 8ull;
    hd.blockSize = 4096;
    hd.reserved = 0;
    memcpy(hd.dataId, "data", 4);
    hd.dataSize = 12 + packedPerChannel.size();

    std::vector<uint8_t> blob;
    blob.reserve(sizeof(hd) + packedPerChannel.size());
    blob.insert(blob.end(), (uint8_t*)&hd, (uint8_t*)&hd + sizeof(hd));
    blob.insert(blob.end(), packedPerChannel.begin(), packedPerChannel.end());
    return writeAll(path, blob.data(), blob.size(), err);
}

std::vector<uint8_t> packBitsLsbFirst(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); i++) {
        if (bits[i] & 1) out[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    return out;
}

std::vector<uint8_t> makeWavPcm16InMemory(const std::vector<int16_t>& samples, int channels, int rate) {
    RiffHeader rh = { {'R','I','F','F'}, 0, {'W','A','V','E'} };
    ChunkHeader fh = { {'f','m','t',' '}, 16 };
    FmtChunk fmt = {};
    fmt.audioFormat = 1;
    fmt.channels = (uint16_t)channels;
    fmt.sampleRate = (uint32_t)rate;
    fmt.bitsPerSample = 16;
    fmt.blockAlign = (uint16_t)(channels * 2);
    fmt.byteRate = (uint32_t)(rate * channels * 2);
    ChunkHeader dh = { {'d','a','t','a'}, (uint32_t)(samples.size() * 2) };
    rh.size = 4 + (8 + 16) + (8 + (uint32_t)(samples.size() * 2));

    std::vector<uint8_t> blob;
    blob.reserve(sizeof(rh) + sizeof(fh) + sizeof(fmt) + sizeof(dh) + samples.size() * 2);
    blob.insert(blob.end(), (uint8_t*)&rh, (uint8_t*)&rh + sizeof(rh));
    blob.insert(blob.end(), (uint8_t*)&fh, (uint8_t*)&fh + sizeof(fh));
    blob.insert(blob.end(), (uint8_t*)&fmt, (uint8_t*)&fmt + sizeof(fmt));
    blob.insert(blob.end(), (uint8_t*)&dh, (uint8_t*)&dh + sizeof(dh));
    blob.insert(blob.end(), (const uint8_t*)samples.data(), (const uint8_t*)samples.data() + samples.size() * 2);
    return blob;
}
