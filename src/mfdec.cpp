#include "mfdec.h"
#include "wavio.h"
#include "ogg.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>

namespace {

bool g_mfStarted = false;

bool ensureMF() {
    if (!g_mfStarted) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)hr; // already-initialized / changed-mode is fine
        hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) return false;
        g_mfStarted = true;
    }
    return true;
}

std::wstring hrHex(HRESULT hr) {
    wchar_t buf[32];
    swprintf(buf, 32, L"0x%08X", (unsigned)hr);
    return buf;
}

struct SampleFormat {
    GUID subtype = GUID_NULL;
    int channels = 0;
    int rate = 0;
    int bits = 0;
    bool isFloat = false;
};

SampleFormat queryFormat(IMFMediaType* type) {
    SampleFormat f;
    type->GetGUID(MF_MT_SUBTYPE, &f.subtype);
    UINT32 v = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &v))) f.channels = (int)v;
    if (SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &v))) f.rate = (int)v;
    if (SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &v))) f.bits = (int)v;
    f.isFloat = (f.subtype == MFAudioFormat_Float);
    return f;
}

bool setType(IMFSourceReader* reader, DWORD stream, const GUID& sub, int channels, int rate) {
    ComPtr<IMFMediaType> t;
    if (FAILED(MFCreateMediaType(&t))) return false;
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    t->SetGUID(MF_MT_SUBTYPE, sub);
    t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)channels);
    t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
    if (sub == MFAudioFormat_PCM) {
        t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32)(channels * 2));
        t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(rate * channels * 2));
        t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    } else if (sub == MFAudioFormat_Float) {
        t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32)(channels * 4));
        t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(rate * channels * 4));
        t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    }
    return SUCCEEDED(reader->SetCurrentMediaType(stream, nullptr, t.get()));
}

void appendSamples(const uint8_t* p, DWORD bytes, const SampleFormat& f, AudioBuffer& out) {
    if (f.channels <= 0 || f.isFloat) {
        size_t n = bytes / 4;
        const float* s = (const float*)p;
        for (size_t i = 0; i < n; i++) out.data.push_back(s[i]);
        return;
    }
    switch (f.bits) {
        case 8: {
            for (DWORD i = 0; i < bytes; i++) out.data.push_back(((int)p[i] - 128) / 128.0f);
            break;
        }
        case 24: {
            size_t n = bytes / 3;
            for (size_t i = 0; i < n; i++) {
                int32_t v = (p[i * 3] << 8) | (p[i * 3 + 1] << 16) | (p[i * 3 + 2] << 24);
                out.data.push_back((float)(v / 2147483648.0));
            }
            break;
        }
        case 32: {
            size_t n = bytes / 4;
            const int32_t* s = (const int32_t*)p;
            for (size_t i = 0; i < n; i++) out.data.push_back((float)(s[i] / 2147483648.0));
            break;
        }
        case 16:
        default: {
            size_t n = bytes / 2;
            const int16_t* s = (const int16_t*)p;
            for (size_t i = 0; i < n; i++) out.data.push_back((float)(s[i] / 32768.0));
            break;
        }
    }
}

} // namespace

bool decodeWithMediaFoundation(const std::wstring& path, AudioBuffer& out, std::wstring& err, const ProgressFn& progress) {
    if (!ensureMF()) { err = L"Media Foundation 初始化失败"; return false; }

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = E_FAIL;
    {
        // Open through an explicit byte stream: this works even where the
        // URL scheme resolver is unavailable (locked down accounts, sandboxes).
        ComPtr<IMFByteStream> stream;
        if (SUCCEEDED(MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, path.c_str(), &stream))) {
            hr = MFCreateSourceReaderFromByteStream(stream.get(), nullptr, &reader);
        }
        if (FAILED(hr) || !reader) {
            reader.reset();
            hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
        }
    }
    if (FAILED(hr) || !reader) {
        err = L"无法打开该文件 (Media Foundation " + hrHex(hr) + L")";
        return false;
    }

    const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;
    ComPtr<IMFMediaType> native;
    hr = reader->GetNativeMediaType(stream, 0, &native);
    if (FAILED(hr)) {
        err = L"文件中没有可解码的音频流 (" + hrHex(hr) + L")";
        return false;
    }
    SampleFormat nativeFmt = queryFormat(native.get());
    if (nativeFmt.channels <= 0 || nativeFmt.rate <= 0) {
        err = L"无法识别音频格式参数";
        return false;
    }

    SampleFormat use;
    bool ok = false;
    if (setType(reader.get(), stream, MFAudioFormat_Float, nativeFmt.channels, nativeFmt.rate)) {
        ComPtr<IMFMediaType> cur;
        if (SUCCEEDED(reader->GetCurrentMediaType(stream, &cur))) {
            use = queryFormat(cur.get());
            ok = true;
        }
    }
    if (!ok && setType(reader.get(), stream, MFAudioFormat_PCM, nativeFmt.channels, nativeFmt.rate)) {
        ComPtr<IMFMediaType> cur;
        if (SUCCEEDED(reader->GetCurrentMediaType(stream, &cur))) {
            use = queryFormat(cur.get());
            ok = true;
        }
    }
    if (!ok) {
        // Last resort: keep whatever the file already provides (uncompressed sources).
        ComPtr<IMFMediaType> cur;
        if (SUCCEEDED(reader->GetCurrentMediaType(stream, &cur))) {
            SampleFormat f = queryFormat(cur.get());
            if (f.subtype == MFAudioFormat_PCM || f.subtype == MFAudioFormat_Float) {
                use = f;
                ok = true;
            }
        }
    }
    if (!ok) {
        err = L"该文件的音频格式无法转换为 PCM (可能需要额外解码器)";
        return false;
    }

    LONGLONG duration = 0;
    {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
            duration = (LONGLONG)var.uhVal.QuadPart;
        PropVariantClear(&var);
    }

    out.reset();
    out.channels = use.channels;
    out.sampleRate = use.rate;

    int lastPercent = -1;
    std::wstring lastStatus;
    while (true) {
        DWORD actualStream = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(stream, 0, &actualStream, &flags, &ts, &sample);
        if (FAILED(hr)) {
            err = L"读取音频数据失败 (" + hrHex(hr) + L")";
            return false;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            ComPtr<IMFMediaType> cur;
            if (SUCCEEDED(reader->GetCurrentMediaType(stream, &cur))) {
                SampleFormat f = queryFormat(cur.get());
                if (f.channels > 0 && f.rate > 0) {
                    use = f;
                    out.channels = f.channels;
                    out.sampleRate = f.rate;
                }
            }
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK) continue;
        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
        BYTE* pdata = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&pdata, &maxLen, &curLen))) continue;
        appendSamples(pdata, curLen, use, out);
        buffer->Unlock();

        if (progress && duration > 0) {
            double frac = (double)ts / (double)duration;
            if (frac > 1.0) frac = 1.0;
            int percent = (int)(frac * 100);
            if (percent != lastPercent) {
                lastPercent = percent;
                wchar_t buf[128];
                swprintf(buf, 128, L"解码中... %d%%", percent);
                if (!progress(frac, buf)) { err = L"已取消"; return false; }
            }
        }
    }

    if (out.data.empty()) { err = L"解码结果为空"; return false; }
    return true;
}

bool decodeAudioFile(const std::wstring& path, AudioBuffer& out, std::wstring& err, const ProgressFn& progress) {
    std::wstring ext = extLower(path);

    // Media Foundation first: it covers mp3/flac/m4a/aac/wma/wav/...
    if (decodeWithMediaFoundation(path, out, err, progress)) return true;
    std::wstring mfErr = err;

    // Built-in fallbacks.
    if (ext == L".wav" || ext == L".wave") {
        if (decodeWavFile(path, out, err)) return true;
    }
    if (ext == L".ogg" || ext == L".oga" || ext == L".opus" || ext == L".spx") {
        if (decodeOggFile(path, out, err, progress)) return true;
    }
    // Unknown extension: still try the built-in readers once.
    if (ext != L".wav" && ext != L".wave") {
        if (decodeWavFile(path, out, err)) return true;
    }
    if (err.empty()) err = mfErr;
    else if (!mfErr.empty()) err += L"  (系统的 Media Foundation 解码器报错: " + mfErr + L")";
    return false;
}
