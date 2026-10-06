#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <string>
static const char* nm(const GUID& g){
  struct E{const GUID* g;const char* n;};
  static E t[]={{&MFAudioFormat_PCM,"PCM"},{&MFAudioFormat_Float,"Float"},{&MFAudioFormat_MP3,"MP3"},
    {&MFAudioFormat_AAC,"AAC"},{&MFAudioFormat_FLAC,"FLAC"},{&MFAudioFormat_Vorbis,"Vorbis"},
    {&MFAudioFormat_Opus,"Opus"},{&MFAudioFormat_ALAC,"ALAC"},{&MFAudioFormat_Dolby_AC3,"AC3"}};
  for(auto&e:t) if(IsEqualGUID(*e.g,g)) return e.n;
  return NULL;
}
int wmain(int argc, wchar_t** argv){
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_FULL);
  for (int i=1;i<argc;i++){
    IMFSourceReader* rd=NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(argv[i], NULL, &rd);
    wprintf(L"%s\n  open hr=0x%08X\n", argv[i], (unsigned)hr);
    if (FAILED(hr)) continue;
    IMFMediaType* mt=NULL;
    hr = rd->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &mt);
    if (FAILED(hr)) { printf("  no audio stream (0x%08X)\n",(unsigned)hr); rd->Release(); continue; }
    GUID sub=GUID_NULL; UINT32 ch=0,rate=0;
    mt->GetGUID(MF_MT_SUBTYPE,&sub); mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch); mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);
    const char* n = nm(sub);
    printf("  audio: %s ch=%u rate=%u", n?n:"?", ch, rate);
    if(!n){ wchar_t s[64]; StringFromGUID2(sub,s,64); wprintf(L" guid=%s", s); }
    printf("\n");
    // try PCM conversion like the real decoder does
    IMFMediaType* outT=NULL; MFCreateMediaType(&outT);
    outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outT->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    outT->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
    outT->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    outT->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    outT->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, ch*2);
    outT->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate*ch*2);
    outT->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, outT);
    printf("  set PCM16: hr=0x%08X\n",(unsigned)hr);
    mt->Release(); outT->Release(); rd->Release();
  }
  MFShutdown();
  return 0;
}