#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mfobjects.h>
#include <stdio.h>
#include <string>
#include <map>

static std::string guidStr(const GUID& g){ wchar_t buf[64]; StringFromGUID2(g, buf, 64);
  char out[128]; WideCharToMultiByte(CP_ACP,0,buf,-1,out,128,0,0); return std::string(out); }

static const char* subtypeName(const GUID& g){
  struct E { const GUID* g; const char* n; };
  static E tbl[] = {
    {&MFMediaType_Audio, "Audio"}, {&MFAudioFormat_PCM,"PCM"}, {&MFAudioFormat_Float,"Float"},
    {&MFAudioFormat_MP3,"MP3"}, {&MFAudioFormat_AAC,"AAC"}, {&MFAudioFormat_ADTS,"ADTS"},
    {&MFAudioFormat_WMAudioV8,"WMAv8"}, {&MFAudioFormat_WMAudioV9,"WMAv9"},
    {&MFAudioFormat_WMASPDIF,"WMAspdif"}, {&MFAudioFormat_FLAC,"FLAC"},
    {&MFAudioFormat_ALAC,"ALAC"}, {&MFAudioFormat_AMR_NB,"AMR_NB"}, {&MFAudioFormat_AMR_WB,"AMR_WB"},
    {&MFAudioFormat_AMR_WP,"AMR_WP"}, {&MFAudioFormat_Dolby_AC3,"AC3"}, {&MFAudioFormat_Dolby_AC3_SPDIF,"AC3spdif"},
    {&MFAudioFormat_Dolby_DDPlus,"DDPlus"}, {&MFAudioFormat_MPEG,"MPEG"}, {&MFAudioFormat_DRM,"DRM"},
    {&MFAudioFormat_DTS,"DTS"}, {&MFAudioFormat_MSP1,"MSP1"}, {&MFAudioFormat_Opus,"Opus"},
    {&MFAudioFormat_Vorbis,"Vorbis"}, {&MFAudioFormat_Float,"Float"},
  };
  for (auto& e : tbl) if (IsEqualGUID(*e.g, g)) return e.n;
  return NULL;
}

int main(){
  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  printf("MFStartup hr=0x%08lx\n", (unsigned long)hr);
  if (FAILED(hr)) return 1;

  IMFActivate** acts = NULL; UINT32 count = 0;
  hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, MFT_ENUM_FLAG_ALL | MFT_ENUM_FLAG_SORTANDFILTER, NULL, NULL, &acts, &count);
  printf("Audio decoder MFTs: %u (hr=0x%08lx)\n", count, (unsigned long)hr);
  for (UINT32 i=0;i<count;i++){
    LPWSTR name=NULL; acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, NULL);
    char nb[512]; nb[0]=0; if(name) WideCharToMultiByte(CP_ACP,0,name,-1,nb,512,0,0);
    printf("[%2u] %s\n", i, nb);
    if (name) CoTaskMemFree(name);
    IMFTransform* tr=NULL;
    if (SUCCEEDED(acts[i]->ActivateObject(IID_IMFTransform,(void**)&tr)) && tr){
      for (DWORD k=0;k<40;k++){
        IMFMediaType* mt=NULL;
        if (FAILED(tr->GetInputAvailableType(0,k,&mt)) || !mt) break;
        GUID sub = GUID_NULL; mt->GetGUID(MF_MT_SUBTYPE,&sub);
        const char* nm = subtypeName(sub);
        printf("      in[%lu] subtype=%s %s\n", k, nm?nm:"?", nm?"":guidStr(sub).c_str());
        mt->Release();
      }
      tr->Release();
    } else printf("      (activate failed)\n");
    acts[i]->Release();
  }
  if (acts) CoTaskMemFree(acts);
  MFShutdown();
  return 0;
}