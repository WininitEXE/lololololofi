#include "../src/ogg.h"
#include "../src/wavio.h"
#include <cstdio>
int wmain(int argc, wchar_t** argv){
    setvbuf(stdout, nullptr, _IONBF, 0);
    AudioBuffer buf; std::wstring err;
    bool ok = decodeOggFile(argv[1], buf, err, nullptr);
    printf("ogg decode ok=%d ch=%d rate=%d frames=%llu err=%s\n", (int)ok, buf.channels, buf.sampleRate,
           (unsigned long long)buf.frames(), toUtf8(err).c_str());
    if (ok) {
        double peak=0; for (float v : buf.data) { double a = v<0?-v:v; if (a>peak) peak=a; }
        printf("peak=%.4f\n", peak);
        std::vector<int16_t> pcm(buf.data.size());
        for (size_t k=0;k<buf.data.size();k++){ double v = buf.data[k]; if(v>1)v=1; if(v<-1)v=-1; pcm[k]=(int16_t)(v*32767.0); }
        std::wstring werr;
        writeWavPcm16(std::wstring(argv[1]) + L".oggdec.wav", pcm, buf.channels, buf.sampleRate, werr);
    }
    return 0;
}