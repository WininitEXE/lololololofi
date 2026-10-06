// Independent validation of our generated .ogg using BASS (PotPlayer copy).
#include <windows.h>
#include <cstdio>
#include <vector>
typedef struct { DWORD freq, chans, flags, origres; void* plugin; void* sample; const char* filename; } BASS_CHANNELINFO;
typedef int HSTREAM;
int wmain(int argc, wchar_t** argv){
  setvbuf(stdout, nullptr, _IONBF, 0);
  HMODULE dll = LoadLibraryW(L"C:\\Program Files\\DAUM\\PotPlayer\\Module\\Bass64\\bass.dll");
  if(!dll){ printf("bass.dll load failed %lu\n", GetLastError()); return 1; }
  auto Init = (BOOL(WINAPI*)(int,DWORD,DWORD,HWND,const void*))GetProcAddress(dll,"BASS_Init");
  auto Create = (HSTREAM(WINAPI*)(BOOL,const void*,unsigned long long,unsigned long long,DWORD))GetProcAddress(dll,"BASS_StreamCreateFile");
  auto GetInfo = (BOOL(WINAPI*)(HSTREAM,BASS_CHANNELINFO*))GetProcAddress(dll,"BASS_ChannelGetInfo");
  auto GetData = (DWORD(WINAPI*)(HSTREAM,void*,DWORD))GetProcAddress(dll,"BASS_ChannelGetData");
  auto Free = (BOOL(WINAPI*)(void))GetProcAddress(dll,"BASS_Free");
  auto ErrGet = (int(WINAPI*)(void))GetProcAddress(dll,"BASS_ErrorGetCode");
  if(!Init||!Create||!GetInfo||!GetData){ printf("bass exports missing\n"); return 1; }
  printf("init=%d err=%d\n", (int)Init(-1, 48000, 0, nullptr, nullptr), ErrGet?ErrGet():0);
  const DWORD BASS_STREAM_DECODE = 0x200000, BASS_UNICODE = 0x80000000;
  HSTREAM h = Create(FALSE, argv[1], 0, 0, BASS_STREAM_DECODE|BASS_UNICODE);
  printf("stream=%d err=%d\n", h, ErrGet?ErrGet():0);
  if(!h) return 1;
  BASS_CHANNELINFO ci = {};
  if(GetInfo(h,&ci)) printf("freq=%u chans=%u flags=0x%X\n", ci.freq, ci.chans, ci.flags);
  std::vector<short> buf(48000*2*4);
  unsigned long long total = 0;
  double peak = 0;
  for(int i=0;i<200;i++){
    DWORD got = GetData(h, buf.data(), (DWORD)(buf.size()*sizeof(short)));
    if(got == (DWORD)-1){ printf("getdata err=%d\n", ErrGet?ErrGet():0); break; }
    if(got == 0) break;
    size_t n = got/2;
    for(size_t k=0;k<n;k++){ double a = buf[k]<0 ? -(double)buf[k] : (double)buf[k]; if(a>peak) peak=a; }
    total += n;
  }
  printf("decoded %llu samples (%.2f s at %u ch) peak=%.0f (of 32767)\n", total, ci.freq&&ci.chans? (double)total/(ci.freq*ci.chans):0.0, ci.chans, peak);
  if(Free) Free();
  return 0;
}