// Dump what a decoder MFT expects (input/output types and attributes).
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <cstdio>
#include <string>

static std::string gs(const GUID& g){ wchar_t b[64]; StringFromGUID2(g,b,64); char o[128]; WideCharToMultiByte(CP_ACP,0,b,-1,o,128,0,0); return o; }
static std::string wide2(const wchar_t* w){ char o[512]={0}; if(w) WideCharToMultiByte(CP_UTF8,0,w,-1,o,512,0,0); return o; }

static const char* nameOf(const GUID& g){
  struct E{const GUID* g;const char* n;};
  static E t[]={{&MFMediaType_Audio,"Audio"},{&MFMediaType_Video,"Video"},{&MFAudioFormat_PCM,"PCM"},
   {&MFAudioFormat_Float,"Float"},{&MFAudioFormat_MP3,"MP3"},{&MFAudioFormat_AAC,"AAC"},{&MFAudioFormat_FLAC,"FLAC"},
   {&MFAudioFormat_Vorbis,"Vorbis"},{&MFAudioFormat_Opus,"Opus"},{&MF_MT_MAJOR_TYPE,"MF_MT_MAJOR_TYPE"},
   {&MF_MT_SUBTYPE,"MF_MT_SUBTYPE"},{&MF_MT_USER_DATA,"MF_MT_USER_DATA"},{&MF_MT_AUDIO_NUM_CHANNELS,"channels"},
   {&MF_MT_AUDIO_SAMPLES_PER_SECOND,"rate"},{&MF_MT_AUDIO_BITS_PER_SAMPLE,"bits"},
   {&MF_MT_AUDIO_BLOCK_ALIGNMENT,"blockalign"},{&MF_MT_AUDIO_AVG_BYTES_PER_SECOND,"avgbytes"},
   {&MF_MT_ALL_SAMPLES_INDEPENDENT,"allindep"},{&MF_MT_FIXED_SIZE_SAMPLES,"fixedsize"},
   {&MF_MT_COMPRESSED,"compressed"},{&MF_MT_AVG_BITRATE,"avgbitrate"},{&MF_MT_AAC_PAYLOAD_TYPE,"aacpayload"},
   {&MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,"aacprofile"}};
  for(auto&e:t) if(IsEqualGUID(*e.g,g)) return e.n;
  return nullptr;
}

static void dumpAttrs(IMFAttributes* a, const char* indent){
  UINT32 n=0; a->GetCount(&n);
  for(UINT32 i=0;i<n;i++){
    GUID k; PROPVARIANT v; PropVariantInit(&v);
    if(FAILED(a->GetItemByIndex(i,&k,&v))) continue;
    const char* nm = nameOf(k);
    printf("%s%s%s = ", indent, nm?nm:"", nm?"":"?");
    if(!nm) printf("%s ", gs(k).c_str());
    switch(v.vt){
      case VT_UI4: printf("%u\n", v.ulVal); break;
      case VT_UI8: printf("%llu\n", (unsigned long long)v.uhVal.QuadPart); break;
      case VT_LPWSTR: printf("\"%s\"\n", wide2(v.pwszVal).c_str()); break;
      case VT_BLOB: printf("blob %u bytes\n", v.blob.cbSize); break;
      case VT_VECTOR|VT_UI1: printf("bytes %u\n", v.caub.cElems); break;
      default: printf("vt=%u\n", v.vt); break;
    }
    PropVariantClear(&v);
  }
}

int wmain(int argc, wchar_t** argv){
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_FULL);
  if(argc<2){ printf("usage: mftdump vorbis|opus|flac|mp3\n"); return 1; }
  GUID sub = MFAudioFormat_Vorbis;
  if(!wcscmp(argv[1],L"opus")) sub = MFAudioFormat_Opus;
  else if(!wcscmp(argv[1],L"flac")) sub = MFAudioFormat_FLAC;
  else if(!wcscmp(argv[1],L"mp3")) sub = MFAudioFormat_MP3;

  MFT_REGISTER_TYPE_INFO info = { MFMediaType_Audio, sub };
  IMFActivate** acts=nullptr; UINT32 count=0;
  HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, MFT_ENUM_FLAG_ALL, &info, nullptr, &acts, &count);
  printf("enum hr=0x%08X count=%u\n",(unsigned)hr,count);
  for(UINT32 i=0;i<count;i++){
    LPWSTR nm=nullptr; acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,&nm,nullptr);
    printf("== MFT %u: %s\n", i, wide2(nm).c_str());
    if(nm) CoTaskMemFree(nm);
    printf("   activate attrs:\n"); dumpAttrs(acts[i], "     ");
    IMFTransform* tr=nullptr;
    if(SUCCEEDED(acts[i]->ActivateObject(IID_IMFTransform,(void**)&tr)) && tr){
      printf("   input available types:\n");
      for(DWORD k=0;k<8;k++){
        IMFMediaType* mt=nullptr;
        if(FAILED(tr->GetInputAvailableType(0,k,&mt))||!mt) break;
        printf("     in[%u]:\n",k); dumpAttrs(mt,"       "); mt->Release();
      }
      printf("   output available types:\n");
      for(DWORD k=0;k<8;k++){
        IMFMediaType* mt=nullptr;
        if(FAILED(tr->GetOutputAvailableType(0,k,&mt))||!mt) break;
        printf("     out[%u]:\n",k); dumpAttrs(mt,"       "); mt->Release();
      }
      tr->Release();
    } else printf("   (activate failed)\n");
    acts[i]->Release();
  }
  if(acts) CoTaskMemFree(acts);
  MFShutdown();
  return 0;
}