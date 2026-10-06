#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
int wmain(int argc, wchar_t** argv){
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_FULL);
  for (int i=1;i<argc;i++){
    HANDLE h = CreateFileW(argv[i], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    printf("CreateFileW err=%lu ok=%d\n", GetLastError(), h!=INVALID_HANDLE_VALUE);
    if (h!=INVALID_HANDLE_VALUE) CloseHandle(h);
    IMFSourceReader* rd=NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(argv[i], NULL, &rd);
    printf("  SourceReaderFromURL hr=0x%08X\n",(unsigned)hr);
    if (rd) rd->Release();
    IMFByteStream* bs=NULL;
    hr = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, argv[i], &bs);
    printf("  MFCreateFile hr=0x%08X\n",(unsigned)hr);
    if (SUCCEEDED(hr)){
      IMFSourceReader* rd2=NULL;
      hr = MFCreateSourceReaderFromByteStream(bs, NULL, &rd2);
      printf("  SourceReaderFromByteStream hr=0x%08X\n",(unsigned)hr);
      if (rd2){
        IMFMediaType* mt=NULL;
        HRESULT h2 = rd2->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,&mt);
        printf("  GetNativeMediaType hr=0x%08X\n",(unsigned)h2);
        if (SUCCEEDED(h2)){
          GUID sub; mt->GetGUID(MF_MT_SUBTYPE,&sub);
          UINT32 ch=0,rate=0; mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&ch); mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate);
          wchar_t s[64]; StringFromGUID2(sub,s,64);
          wprintf(L"  subtype=%s ch=%u rate=%u\n", s, ch, rate);
          mt->Release();
        }
        rd2->Release();
      }
      bs->Release();
    }
  }
  MFShutdown();
  return 0;
}