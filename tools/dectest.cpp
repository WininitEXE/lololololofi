#include "../src/mfdec.h"
#include "../src/wavio.h"
#include <cstdio>
#include <cmath>
static void trace(const char* s){ fputs(s, stdout); fflush(stdout); }
int wmain(int argc, wchar_t** argv){
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i=1;i<argc;i++){
        AudioBuffer buf; std::wstring err;
        trace("before decode\n");
        bool ok = decodeAudioFile(argv[i], buf, err, nullptr);
        printf("decode ok=%d ch=%d rate=%d frames=%llu err=%s\n", (int)ok, buf.channels, buf.sampleRate,
               (unsigned long long)buf.frames(), toUtf8(err).c_str());
        fflush(stdout);
        if (ok){
            double peak=0, sum=0;
            for (float v : buf.data){ double a = v<0?-v:v; if (a>peak) peak=a; sum += (double)v*v; }
            double rms = buf.data.empty()?0: sqrt(sum/buf.data.size());
            printf("     peak=%.4f rms=%.4f\n", peak, rms);
            fflush(stdout);
            std::vector<int16_t> pcm(buf.data.size());
            for (size_t k=0;k<buf.data.size();k++){ double v = buf.data[k]; if(v>1)v=1; if(v<-1)v=-1; pcm[k]=(int16_t)lrint(v*32767.0); }
            std::wstring outPath = std::wstring(argv[i]) + L".decoded.wav";
            std::wstring werr;
            trace("before write\n");
            bool w = writeWavPcm16(outPath, pcm, buf.channels, buf.sampleRate, werr);
            printf("     write=%d err=%s\n", (int)w, toUtf8(werr).c_str());
            fflush(stdout);
        }
    }
    trace("before exit\n");
    return 0;
}