#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <stdio.h>
int main(){
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_FULL);
  const char* exts[] = {".ogg",".oga",".mp3",".flac",".m4a",".mp4",".opus",".wav",".wma",".aac",".webm",".ape"};
  for (int i=0;i<12;i++){
    char key[512]; sprintf(key, "MediaFoundation\\ByteStreamHandlers\\%s", exts[i]);
    HKEY h=NULL;
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &h)==ERROR_SUCCESS){
      char sub[256]; DWORD idx=0; printf("ByteStreamHandlers %-6s:", exts[i]);
      while (RegEnumKeyA(h, idx++, sub, sizeof(sub))==ERROR_SUCCESS) printf("  %s", sub);
      printf("\n"); RegCloseKey(h);
    } else printf("ByteStreamHandlers %-6s: (none)\n", exts[i]);
  }
  printf("--- handler friendly names ---\n");
  const char* cls[] = {"{b98c6c5d-8a0d-4e2f-9f0f-1e0a8e3d3a5d}"};
  MFShutdown();
  return 0;
}