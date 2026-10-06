#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <cstdio>
#include <string>
static std::string gs(const GUID& g){ wchar_t b[64]; StringFromGUID2(g,b,64); char o[128]; WideCharToMultiByte(CP_ACP,0,b,-1,o,128,0,0); return o; }
int wmain(){
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_FULL);
  IMFActivate** acts=nullptr; UINT32 n=0;
  HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_ALL|MFT_ENUM_FLAG_SORTANDFILTER, nullptr, nullptr, &acts, &n);
  printf("audio encoders: %u hr=0x%08X\n", n, (unsigned)hr);
  for(UINT32 i=0;i<n;i++){
    LPWSTR nm=nullptr; acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,&nm,nullptr);
    char nb[256]={0}; if(nm) WideCharToMultiByte(CP_UTF8,0,nm,-1,nb,256,0,0);
    printf("[%u] %s\n", i, nb);
    if(nm) CoTaskMemFree(nm);
    IMFTransform* tr=nullptr;
    if(SUCCEEDED(acts[i]->ActivateObject(IID_IMFTransform,(void**)&tr)) && tr){
      for(DWORD k=0;k<12;k++){
        IMFMediaType* mt=nullptr;
        if(FAILED(tr->GetOutputAvailableType(0,k,&mt))||!mt) break;
        GUID sub; mt->GetGUID(MF_MT_SUBTYPE,&sub);
        printf("     out[%u] subtype=%s\n", k, gs(sub).c_str());
        mt->Release();
      }
      tr->Release();
    }
    acts[i]->Release();
  }
  if(acts) CoTaskMemFree(acts);
  MFShutdown();
  return 0;
}