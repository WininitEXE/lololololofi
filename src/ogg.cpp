// ogg.cpp - built-in Ogg container demuxer plus decoder-MFT driving.
//
// Windows has no Ogg byte stream handler, so the container is parsed here and
// the compressed packets are pushed straight into a Media Foundation decoder
// MFT (Vorbis / Opus / FLAC).
#include "ogg.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <mferror.h>
#include <algorithm>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {

bool g_debug = false;
void dbg(const char* fmt, ...) {
    if (!g_debug) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

bool mfReady() {
    static bool started = false;
    if (!started) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) return false;
        started = true;
    }
    return true;
}

std::wstring hrHex(HRESULT hr) {
    wchar_t buf[32];
    swprintf(buf, 32, L"0x%08X", (unsigned)hr);
    return buf;
}

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

bool readWholeFile(const std::wstring& path, std::vector<uint8_t>& data, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = L"无法打开文件 " + path + L": " + win32ErrorText(GetLastError()); return false; }
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > ((LONGLONG)1 << 31)) { CloseHandle(h); err = L"文件为空或过大"; return false; }
    data.resize((size_t)sz.QuadPart);
    size_t done = 0;
    while (done < data.size()) {
        DWORD want = (DWORD)std::min<size_t>(data.size() - done, 1u << 20), got = 0;
        if (!ReadFile(h, data.data() + done, want, &got, nullptr) || got == 0) { CloseHandle(h); err = L"读取文件失败"; return false; }
        done += got;
    }
    CloseHandle(h);
    return true;
}

struct OggLogicalStream {
    uint32_t serial = 0;
    std::vector<OggPacket> packets;
};

bool parseOggPages(const std::vector<uint8_t>& d, std::vector<OggLogicalStream>& streams, std::wstring& err) {
    size_t pos = 0;
    std::vector<uint8_t> partial;
    uint32_t partialSerial = 0;
    bool inPartial = false;

    while (pos + 27 <= d.size()) {
        if (memcmp(d.data() + pos, "OggS", 4) != 0) {
            size_t next = pos + 1;
            while (next + 4 <= d.size() && memcmp(d.data() + next, "OggS", 4) != 0) next++;
            pos = next;
            continue;
        }
        const uint8_t* p = d.data() + pos;
        uint8_t headerType = p[5];
        int64_t granule = (int64_t)rd64(p + 6);
        uint32_t serial = rd32(p + 14);
        uint8_t segCount = p[26];
        if (pos + 27 + (size_t)segCount > d.size()) break;
        const uint8_t* segTable = p + 27;
        size_t bodySize = 0;
        for (int i = 0; i < segCount; i++) bodySize += segTable[i];
        if (pos + 27 + (size_t)segCount + bodySize > d.size()) break;
        const uint8_t* body = segTable + segCount;

        OggLogicalStream* stream = nullptr;
        for (auto& s : streams) if (s.serial == serial) { stream = &s; break; }
        if (!stream) {
            streams.push_back(OggLogicalStream());
            streams.back().serial = serial;
            stream = &streams.back();
        }

        size_t bodyPos = 0;
        for (int i = 0; i < segCount; i++) {
            size_t len = segTable[i];
            if (inPartial && partialSerial != serial) { partial.clear(); inPartial = false; }
            partial.insert(partial.end(), body + bodyPos, body + bodyPos + len);
            bodyPos += len;
            if (len < 255) {
                OggPacket pkt;
                pkt.data = partial;
                pkt.serial = serial;
                pkt.granulePos = granule;
                pkt.bos = ((headerType & 0x02) != 0) && stream->packets.empty();
                pkt.eos = (headerType & 0x04) != 0;
                stream->packets.push_back(pkt);
                partial.clear();
                inPartial = false;
            } else {
                inPartial = true;
                partialSerial = serial;
            }
        }
        pos += 27 + (size_t)segCount + bodySize;
    }

    size_t total = 0;
    for (auto& s : streams) total += s.packets.size();
    if (total == 0) { err = L"不是有效的 Ogg 文件 (没有找到数据页)"; return false; }
    return true;
}

bool startsWith(const std::vector<uint8_t>& v, const char* sig, size_t n) {
    return v.size() >= n && memcmp(v.data(), sig, n) == 0;
}

std::vector<uint8_t> xiphLace(const std::vector<const std::vector<uint8_t>*>& packets) {
    std::vector<uint8_t> out;
    out.push_back((uint8_t)(packets.size() - 1));
    for (size_t i = 0; i + 1 < packets.size(); i++) {
        size_t n = packets[i]->size();
        while (n >= 255) { out.push_back(255); n -= 255; }
        out.push_back((uint8_t)n);
    }
    for (auto* p : packets) out.insert(out.end(), p->begin(), p->end());
    return out;
}

} // namespace

bool decodeOggFile(const std::wstring& path, AudioBuffer& out, std::wstring& err, const ProgressFn& progress) {
    g_debug = getenv("ONEBIT_DEBUG") != nullptr;
    if (!mfReady()) { err = L"Media Foundation 初始化失败"; return false; }

    std::vector<uint8_t> data;
    if (!readWholeFile(path, data, err)) return false;

    std::vector<OggLogicalStream> streams;
    if (!parseOggPages(data, streams, err)) return false;

    for (auto& s : streams) {
        if (s.packets.empty()) continue;
        const std::vector<uint8_t>& first = s.packets[0].data;

        if (startsWith(first, "OpusHead", 8) && first.size() >= 19) {
            int channels = first[9];
            int rate = (int)rd32(first.data() + 12);
            if (rate <= 0) rate = 48000;
            std::vector<OggPacket> audio(s.packets.begin() + 2, s.packets.end());
            std::vector<int64_t> granules;
            for (auto& p : audio) granules.push_back(p.granulePos);
            return decodePacketsWithMFT(MFAudioFormat_Opus, first, channels, rate, audio, granules, out, err, progress);
        }

        if (first.size() >= 30 && first[0] == 0x01 && memcmp(first.data() + 1, "vorbis", 6) == 0) {
            if (s.packets.size() < 4) { err = L"Ogg Vorbis 文件不完整 (缺少头部)"; return false; }
            int channels = first[11];
            int rate = (int)rd32(first.data() + 12);
            std::vector<const std::vector<uint8_t>*> headers;
            headers.push_back(&s.packets[0].data);
            headers.push_back(&s.packets[1].data);
            headers.push_back(&s.packets[2].data);
            std::vector<OggPacket> audio(s.packets.begin() + 3, s.packets.end());
            std::vector<int64_t> granules;
            for (auto& p : audio) granules.push_back(p.granulePos);

            std::wstring e1;
            if (decodePacketsWithMFT(MFAudioFormat_Vorbis, xiphLace(headers), channels, rate, audio, granules, out, e1, progress))
                return true;
            std::vector<uint8_t> plain;
            for (auto* h : headers) plain.insert(plain.end(), h->begin(), h->end());
            std::wstring e2;
            if (decodePacketsWithMFT(MFAudioFormat_Vorbis, plain, channels, rate, audio, granules, out, e2, progress))
                return true;
            err = e1;
            return false;
        }

        if (first.size() > 13 && first[0] == 0x7F && memcmp(first.data() + 1, "FLAC", 4) == 0) {
            // Ogg FLAC mapping: 0x7F "FLAC" major minor nheaders(2) then metadata blocks.
            size_t off = 9;
            if (first.size() < off + 4) { err = L"Ogg FLAC 头部无效"; return false; }
            uint32_t blockLen = ((uint32_t)first[off + 1] << 16) | ((uint32_t)first[off + 2] << 8) | first[off + 3];
            std::vector<uint8_t> extradata;
            if (first.size() >= off + 4 + blockLen) extradata.assign(first.begin() + off, first.begin() + off + 4 + blockLen);
            else extradata.assign(first.begin() + off, first.end());
            std::vector<OggPacket> audio(s.packets.begin() + 1, s.packets.end());
            std::vector<int64_t> granules;
            for (auto& p : audio) granules.push_back(p.granulePos);
            return decodePacketsWithMFT(MFAudioFormat_FLAC, extradata, 0, 0, audio, granules, out, err, progress);
        }
    }
    err = L"Ogg 文件中没有找到支持的音频编码 (支持 Vorbis / Opus / FLAC)";
    return false;
}

bool decodePacketsWithMFT(const GUID& subtype, const std::vector<uint8_t>& extradata,
                          int channels, int sampleRate,
                          const std::vector<OggPacket>& packets,
                          const std::vector<int64_t>& granulePositions,
                          AudioBuffer& out, std::wstring& err, const ProgressFn& progress) {
    if (!mfReady()) { err = L"Media Foundation 初始化失败"; return false; }
    // Decoder MFTs refuse samples without a duration (MF_E_NO_SAMPLE_DURATION),
    // so every packet gets a plausible timestamp/duration pair.
    const int nominalSamples = sampleRate > 0 ? std::max(64, sampleRate / 50) : 1024;
    if (packets.empty()) { err = L"没有可解码的音频数据"; return false; }

    struct OutFormat { bool isFloat = false; int bits = 16; int channels = 0; int rate = 0; } fmt;

    // ---- locate and create the decoder MFT ------------------------------
    MFT_REGISTER_TYPE_INFO wanted = { MFMediaType_Audio, subtype };

    IMFActivate** acts = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, MFT_ENUM_FLAG_ALL | MFT_ENUM_FLAG_SORTANDFILTER,
                           &wanted, nullptr, &acts, &count);
    if (FAILED(hr) || count == 0 || !acts) {
        if (acts) CoTaskMemFree(acts);
        err = L"系统中没有安装解码该格式所需的解码器";
        return false;
    }
    ComPtr<IMFActivate> activate;
    activate.attach(acts[0]);
    for (UINT32 i = 1; i < count; i++) acts[i]->Release();
    CoTaskMemFree(acts);

    UINT32 isAsync = 0;
    if (SUCCEEDED(activate->GetUINT32(MF_TRANSFORM_ASYNC, &isAsync)) && isAsync)
        activate->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    if (g_debug) {
        LPWSTR nm = nullptr;
        activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &nm, nullptr);
        char nb[256] = {0};
        if (nm) WideCharToMultiByte(CP_UTF8, 0, nm, -1, nb, 256, nullptr, nullptr);
        dbg("decoder MFT: %s async=%u count=%u\n", nb, isAsync, count);
        if (nm) CoTaskMemFree(nm);
    }

    ComPtr<IMFTransform> transform;
    hr = activate->ActivateObject(IID_IMFTransform, (void**)&transform);
    if (FAILED(hr) || !transform) { err = L"无法创建解码器 (" + hrHex(hr) + L")"; return false; }

    // ---- negotiate the input type ---------------------------------------
    ComPtr<IMFMediaType> input;
    MFCreateMediaType(&input);
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    input->SetGUID(MF_MT_SUBTYPE, subtype);
    if (channels > 0) input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
    if (sampleRate > 0) input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)sampleRate);
    if (!extradata.empty()) input->SetBlob(MF_MT_USER_DATA, extradata.data(), (UINT32)extradata.size());

    hr = transform->SetInputType(0, input.get(), 0);
    dbg("SetInputType hr=0x%08X extradata=%u bytes ch=%d rate=%d\n", (unsigned)hr, (unsigned)extradata.size(), channels, sampleRate);
    if (FAILED(hr)) {
        for (DWORD i = 0; ; i++) {
            ComPtr<IMFMediaType> avail;
            if (FAILED(transform->GetInputAvailableType(0, i, &avail))) break;
            UINT32 ch = 0, rate = 0;
            if (channels > 0) avail->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
            if (sampleRate > 0) avail->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)sampleRate);
            (void)ch; (void)rate;
            if (!extradata.empty()) avail->SetBlob(MF_MT_USER_DATA, extradata.data(), (UINT32)extradata.size());
            if (SUCCEEDED(transform->SetInputType(0, avail.get(), 0))) { hr = S_OK; break; }
        }
    }
    if (FAILED(hr)) { err = L"解码器不接受该音频数据 (" + hrHex(hr) + L")"; return false; }

    auto pickOutputType = [&](void) -> HRESULT {
        for (DWORD i = 0; ; i++) {
            ComPtr<IMFMediaType> avail;
            if (FAILED(transform->GetOutputAvailableType(0, i, &avail))) break;
            GUID sub = GUID_NULL;
            avail->GetGUID(MF_MT_SUBTYPE, &sub);
            UINT32 bits = 0;
            avail->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
            if (sub == MFAudioFormat_PCM && bits == 16) {
                if (SUCCEEDED(transform->SetOutputType(0, avail.get(), 0))) return S_OK;
            }
            if (sub == MFAudioFormat_Float && bits == 32) {
                if (SUCCEEDED(transform->SetOutputType(0, avail.get(), 0))) return S_OK;
            }
        }
        return E_FAIL;
    };
    hr = pickOutputType();
    if (FAILED(hr)) { err = L"解码器没有可用的 PCM 输出格式"; return false; }

    auto refreshFormat = [&](void) -> bool {
        ComPtr<IMFMediaType> cur;
        if (FAILED(transform->GetOutputCurrentType(0, &cur))) return false;
        GUID sub = GUID_NULL; cur->GetGUID(MF_MT_SUBTYPE, &sub);
        UINT32 v = 0;
        fmt.isFloat = (sub == MFAudioFormat_Float);
        fmt.bits = 16; fmt.channels = 0; fmt.rate = 0;
        if (SUCCEEDED(cur->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &v))) fmt.bits = (int)v;
        if (SUCCEEDED(cur->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &v))) fmt.channels = (int)v;
        if (SUCCEEDED(cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &v))) fmt.rate = (int)v;
        return fmt.channels > 0 && fmt.rate > 0;
    };
    if (!refreshFormat()) { err = L"无法读取解码器的输出格式"; return false; }

    out.reset();
    out.channels = fmt.channels;
    out.sampleRate = fmt.rate;

    MFT_OUTPUT_STREAM_INFO streamInfo = {};
    transform->GetOutputStreamInfo(0, &streamInfo);

    auto appendPcm = [&](IMFSample* sample) {
        if (!sample) return;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return;
        BYTE* pdata = nullptr; DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&pdata, &maxLen, &curLen))) return;
        if (fmt.isFloat) {
            size_t n = curLen / 4;
            const float* s = (const float*)pdata;
            for (size_t i = 0; i < n; i++) out.data.push_back(s[i]);
        } else if (fmt.bits == 32) {
            size_t n = curLen / 4;
            const int32_t* s = (const int32_t*)pdata;
            for (size_t i = 0; i < n; i++) out.data.push_back((float)(s[i] / 2147483648.0));
        } else if (fmt.bits == 24) {
            size_t n = curLen / 3;
            for (size_t i = 0; i < n; i++) {
                int32_t v = (pdata[i * 3] << 8) | (pdata[i * 3 + 1] << 16) | (pdata[i * 3 + 2] << 24);
                out.data.push_back((float)(v / 2147483648.0));
            }
        } else {
            size_t n = curLen / 2;
            const int16_t* s = (const int16_t*)pdata;
            for (size_t i = 0; i < n; i++) out.data.push_back((float)(s[i] / 32768.0));
        }
        buffer->Unlock();
    };

    auto drain = [&](void) -> bool {
        for (;;) {
            ComPtr<IMFSample> allocated;
            MFT_OUTPUT_DATA_BUFFER outBuf = {};
            outBuf.dwStreamID = 0;
            if (!(streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                ComPtr<IMFMediaBuffer> buf;
                DWORD size = streamInfo.cbSize ? streamInfo.cbSize : (1u << 16);
                if (FAILED(MFCreateSample(&allocated)) || FAILED(MFCreateMemoryBuffer(size, &buf))) return false;
                allocated->AddBuffer(buf.get());
                outBuf.pSample = allocated.get();
            }
            DWORD status = 0;
            HRESULT r = transform->ProcessOutput(0, 1, &outBuf, &status);
            if (outBuf.pEvents) { outBuf.pEvents->Release(); outBuf.pEvents = nullptr; }
            IMFSample* produced = outBuf.pSample ? outBuf.pSample : allocated.get();
            if (r == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
            if (r == MF_E_TRANSFORM_STREAM_CHANGE) {
                for (DWORD i = 0; ; i++) {
                    ComPtr<IMFMediaType> avail;
                    if (FAILED(transform->GetOutputAvailableType(0, i, &avail))) break;
                    if (SUCCEEDED(transform->SetOutputType(0, avail.get(), 0))) break;
                }
                refreshFormat();
                out.channels = fmt.channels;
                out.sampleRate = fmt.rate;
                transform->GetOutputStreamInfo(0, &streamInfo);
                if (produced && produced != allocated.get()) produced->Release();
                continue;
            }
            if (SUCCEEDED(r)) { appendPcm(produced); dbg("  ProcessOutput ok pcm=%u\n", (unsigned)out.data.size()); }
            if (produced && produced != allocated.get()) produced->Release();
            if (FAILED(r)) { err = L"解码失败 (" + hrHex(r) + L")"; return false; }
        }
    };

    int lastPercent = -1;
    int64_t prevGranule = 0;
    int64_t samplePos = 0;
    for (size_t i = 0; i < packets.size(); i++) {
        const std::vector<uint8_t>& pkt = packets[i].data;

        int64_t durSamples = nominalSamples;
        if (i < granulePositions.size()) {
            int64_t g = granulePositions[i];
            if (g > prevGranule && (g - prevGranule) <= (int64_t)fmt.rate * 2) durSamples = g - prevGranule;
            if (g >= 0) prevGranule = g;
            else prevGranule += durSamples;
        } else {
            prevGranule += durSamples;
        }
        if (durSamples <= 0) durSamples = nominalSamples;
        ComPtr<IMFSample> sample;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateSample(&sample)) ||
            FAILED(MFCreateMemoryBuffer((DWORD)std::max<size_t>(pkt.size(), 1), &buffer))) {
            err = L"内存分配失败";
            return false;
        }
        BYTE* dst = nullptr;
        DWORD maxLen = 0;
        if (FAILED(buffer->Lock(&dst, &maxLen, nullptr))) { err = L"内存锁定失败"; return false; }
        if (!pkt.empty()) memcpy(dst, pkt.data(), pkt.size());
        buffer->Unlock();
        buffer->SetCurrentLength((DWORD)pkt.size());
        sample->AddBuffer(buffer.get());
        sample->SetSampleTime(samplePos * 10000000LL / (fmt.rate > 0 ? fmt.rate : 48000));
        sample->SetSampleDuration(durSamples * 10000000LL / (fmt.rate > 0 ? fmt.rate : 48000));
        samplePos += durSamples;

        HRESULT r = transform->ProcessInput(0, sample.get(), 0);
        if (r == MF_E_NOTACCEPTING) {
            dbg("packet %u: NOTACCEPTING, draining\n", (unsigned)i);
            if (!drain()) return false;
            r = transform->ProcessInput(0, sample.get(), 0);
        }
        if (i < 3 || FAILED(r)) dbg("packet %u size=%u ProcessInput hr=0x%08X pcm=%u\n", (unsigned)i, (unsigned)pkt.size(), (unsigned)r, (unsigned)out.data.size());
        if (FAILED(r)) { err = L"解码器拒绝数据 (" + hrHex(r) + L")"; return false; }
        if (!drain()) return false;

        if (progress && (i % 16) == 0) {
            int percent = (int)((double)i * 100.0 / (double)packets.size());
            if (percent != lastPercent) {
                lastPercent = percent;
                wchar_t buf[128];
                swprintf(buf, 128, L"解码 Ogg 音频... %d%%", percent);
                if (!progress((double)i / (double)packets.size(), buf)) { err = L"已取消"; return false; }
            }
        }
    }

    if (out.data.empty()) { err = L"解码结果为空"; return false; }
    return true;
}
