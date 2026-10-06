// winflac.cpp - encodes a WAV into a real .flac using the Windows FLAC encoder MFT.
// Only used to create trustworthy test material for the converter.
#include "../src/wavio.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static std::wstring hex(HRESULT hr){ wchar_t b[32]; swprintf(b,32,L"0x%08X",(unsigned)hr); return b; }

int wmain(int argc, wchar_t** argv){
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { printf("usage: winflac in.wav out.flac\n"); return 1; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    AudioBuffer buf; std::wstring err;
    if (!decodeWavFile(argv[1], buf, err)) { printf("wav read failed: %s\n", toUtf8(err).c_str()); return 1; }
    const int ch = buf.channels, rate = buf.sampleRate;
    printf("source: %d ch, %d Hz, %llu frames\n", ch, rate, (unsigned long long)buf.frames());

    MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Audio, MFAudioFormat_FLAC };
    IMFActivate** acts = nullptr; UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_ALL | MFT_ENUM_FLAG_SORTANDFILTER,
                           nullptr, &outInfo, &acts, &count);
    if (FAILED(hr) || !count) { printf("no flac encoder (hr=%s)\n", toUtf8(hex(hr)).c_str()); return 1; }
    printf("flac encoders: %u\n", count);
    IMFTransform* tr = nullptr;
    if (FAILED(acts[0]->ActivateObject(IID_IMFTransform, (void**)&tr)) || !tr) { printf("activate failed\n"); return 1; }

    // output type
    IMFMediaType* outType = nullptr;
    for (DWORD i = 0; ; i++) {
        IMFMediaType* mt = nullptr;
        if (FAILED(tr->GetOutputAvailableType(0, i, &mt)) || !mt) break;
        mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)ch);
        mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
        mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        hr = tr->SetOutputType(0, mt, 0);
        printf("set output type %u hr=%s\n", i, toUtf8(hex(hr)).c_str());
        if (SUCCEEDED(hr)) { outType = mt; break; }
        mt->Release();
    }
    if (!outType) { printf("no output type\n"); return 1; }

    IMFMediaType* inType = nullptr;
    MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)ch);
    inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
    inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32)(ch * 2));
    inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(rate * ch * 2));
    inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = tr->SetInputType(0, inType, 0);
    printf("set input type hr=%s\n", toUtf8(hex(hr)).c_str());
    if (FAILED(hr)) return 1;

    UINT32 blobSize = 0;
    std::vector<uint8_t> extra;
    if (SUCCEEDED(outType->GetBlobSize(MF_MT_USER_DATA, &blobSize)) && blobSize) {
        extra.resize(blobSize);
        outType->GetBlob(MF_MT_USER_DATA, extra.data(), blobSize, nullptr);
        printf("extradata from encoder: %u bytes\n", blobSize);
    } else printf("encoder gave no MF_MT_USER_DATA\n");

    tr->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    tr->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    MFT_OUTPUT_STREAM_INFO si = {};
    tr->GetOutputStreamInfo(0, &si);

    std::vector<uint8_t> flacData;   // encoded frames
    auto drain = [&]() {
        for (;;) {
            IMFSample* out = nullptr;
            IMFMediaBuffer* mb = nullptr;
            MFT_OUTPUT_DATA_BUFFER ob = {};
            ob.dwStreamID = 0;
            if (!(si.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                MFCreateSample(&out);
                MFCreateMemoryBuffer(si.cbSize ? si.cbSize : (1 << 18), &mb);
                out->AddBuffer(mb);
                ob.pSample = out;
            }
            DWORD status = 0;
            HRESULT r = tr->ProcessOutput(0, 1, &ob, &status);
            if (ob.pEvents) ob.pEvents->Release();
            if (r == MF_E_TRANSFORM_NEED_MORE_INPUT) { if (out) { out->Release(); mb->Release(); } return; }
            if (FAILED(r)) { printf("ProcessOutput hr=%s\n", toUtf8(hex(r)).c_str()); if (out) { out->Release(); if (mb) mb->Release(); } return; }
            IMFSample* s = ob.pSample ? ob.pSample : out;
            if (s) {
                IMFMediaBuffer* cb = nullptr;
                if (SUCCEEDED(s->ConvertToContiguousBuffer(&cb))) {
                    BYTE* p = nullptr; DWORD len = 0;
                    if (SUCCEEDED(cb->Lock(&p, nullptr, &len))) {
                        flacData.insert(flacData.end(), p, p + len);
                        cb->Unlock();
                    }
                    cb->Release();
                }
            }
            if (ob.pSample && ob.pSample != out) ob.pSample->Release();
            if (out) { out->Release(); mb->Release(); }
        }
    };

    const size_t total = buf.frames();
    const size_t chunk = 4096;
    int64_t pos = 0;
    while ((size_t)pos < total) {
        size_t n = (size_t)std::min<size_t>(chunk, total - (size_t)pos);
        std::vector<int16_t> pcm(n * (size_t)ch);
        for (size_t i = 0; i < pcm.size(); i++) {
            double v = buf.data[(size_t)pos * ch + i];
            if (v > 1) v = 1; if (v < -1) v = -1;
            pcm[i] = (int16_t)lrint(v * 32767.0);
        }
        IMFSample* s = nullptr; IMFMediaBuffer* mb = nullptr;
        MFCreateSample(&s);
        MFCreateMemoryBuffer((DWORD)(pcm.size() * 2), &mb);
        BYTE* p = nullptr;
        mb->Lock(&p, nullptr, nullptr);
        memcpy(p, pcm.data(), pcm.size() * 2);
        mb->Unlock();
        mb->SetCurrentLength((DWORD)(pcm.size() * 2));
        s->AddBuffer(mb);
        s->SetSampleTime(pos * 10000000LL / rate);
        s->SetSampleDuration(n * 10000000LL / rate);
        HRESULT r = tr->ProcessInput(0, s, 0);
        if (r == MF_E_NOTACCEPTING) { drain(); r = tr->ProcessInput(0, s, 0); }
        if (FAILED(r)) { printf("ProcessInput hr=%s at %lld\n", toUtf8(hex(r)).c_str(), (long long)pos); return 1; }
        drain();
        pos += (int64_t)n;
        mb->Release(); s->Release();
    }
    tr->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    drain();
    printf("encoded frames payload: %u bytes\n", (unsigned)flacData.size());

    // assemble the .flac file
    std::vector<uint8_t> file;
    const char* magic = "fLaC";
    bool payloadHasHeader = flacData.size() >= 4 && memcmp(flacData.data(), "fLaC", 4) == 0;
    if (!payloadHasHeader) file.insert(file.end(), magic, magic + 4);
    if (payloadHasHeader) {
        // the encoder already emitted a complete FLAC stream
    } else if (extra.size() >= 4) {
        file.insert(file.end(), extra.begin(), extra.end());
    } else if (extra.size() == 34) {
        uint8_t hdr[4] = {0x80, 0x00, 0x00, 0x22};
        file.insert(file.end(), hdr, hdr + 4);
        file.insert(file.end(), extra.begin(), extra.end());
    } else {
        printf("unexpected extradata size %u, writing bare stream\n", (unsigned)extra.size());
        file.insert(file.end(), extra.begin(), extra.end());
    }
    file.insert(file.end(), flacData.begin(), flacData.end());
    HANDLE h = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot write %ls\n", argv[2]); return 1; }
    DWORD wrote = 0;
    WriteFile(h, file.data(), (DWORD)file.size(), &wrote, nullptr);
    CloseHandle(h);
    printf("wrote %ls (%u bytes)\n", argv[2], (unsigned)file.size());
    MFShutdown();
    return 0;
}