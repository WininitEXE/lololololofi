#include "onebit.h"
#include <cmath>
#include <algorithm>

namespace {

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x1234567u) {}
    // xorshift32, returns [-1, 1)
    float nextSym() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return ((float)(s & 0xFFFFFF) / 8388608.0f) - 1.0f;
    }
};

const double* feedbackCoeffs(int order, int& count) {
    static const double c1[] = { 1.0 };
    static const double c2[] = { 2.0, -1.0 };
    static const double c3[] = { 3.0, -3.0, 1.0 };
    static const double c4[] = { 4.0, -6.0, 4.0, -1.0 };
    switch (order) {
        case 1: count = 1; return c1;
        case 2: count = 2; return c2;
        case 3: count = 3; return c3;
        default: count = 4; return c4;
    }
}

double ditherAmp(DitherLevel level) {
    switch (level) {
        case DitherLevel::Light:  return 0.02;
        case DitherLevel::Strong: return 0.2;
        default: return 0.0;
    }
}

} // namespace

std::wstring quantModeName(QuantMode m) {
    switch (m) {
        case QuantMode::Hard: return L"硬切 1bit (方波)";
        case QuantMode::SD1:  return L"一阶 ΣΔ 噪声整形";
        case QuantMode::SD2:  return L"二阶 ΣΔ 噪声整形";
        case QuantMode::SD3:  return L"三阶 ΣΔ 噪声整形";
        default:              return L"四阶 ΣΔ 噪声整形";
    }
}

bool resampleBuffer(const AudioBuffer& in, int outRate, AudioBuffer& out, std::wstring& err) {
    if (in.empty()) { err = L"输入音频为空"; return false; }
    if (outRate <= 0) { err = L"无效的目标采样率"; return false; }
    if (outRate == in.sampleRate) { out = in; return true; }

    const int ch = in.channels;
    const size_t inFrames = in.frames();
    const double ratio = (double)in.sampleRate / (double)outRate;
    size_t outFrames = (size_t)std::floor((double)inFrames / ratio);
    if (outFrames < 1) { err = L"目标采样率过高"; return false; }

    out.reset();
    out.channels = ch;
    out.sampleRate = outRate;
    out.data.resize(outFrames * (size_t)ch);

    if (ratio <= 1.0) {
        // Upsampling-ish: linear interpolation between neighbours.
        for (size_t i = 0; i < outFrames; i++) {
            double pos = (double)i * ratio;
            size_t i0 = (size_t)pos;
            size_t i1 = std::min(i0 + 1, inFrames - 1);
            double f = pos - (double)i0;
            for (int c = 0; c < ch; c++) {
                double a = in.data[i0 * ch + c];
                double b = in.data[i1 * ch + c];
                out.data[i * ch + c] = (float)(a + (b - a) * f);
            }
        }
    } else {
        // Downsampling: average the source window (crude but honest low pass).
        for (size_t i = 0; i < outFrames; i++) {
            size_t s0 = (size_t)((double)i * ratio);
            size_t s1 = (size_t)((double)(i + 1) * ratio);
            if (s1 <= s0) s1 = s0 + 1;
            if (s1 > inFrames) s1 = inFrames;
            for (int c = 0; c < ch; c++) {
                double sum = 0.0;
                for (size_t s = s0; s < s1; s++) sum += in.data[s * ch + c];
                out.data[i * ch + c] = (float)(sum / (double)(s1 - s0));
            }
        }
    }
    return true;
}

bool convertTo1Bit(const AudioBuffer& in, const ConvertOptions& opt, BitStream& out,
                   std::wstring& err, const ProgressFn& progress) {
    if (in.empty()) { err = L"输入音频为空"; return false; }

    AudioBuffer work;
    if (opt.targetRate > 0 && opt.targetRate != in.sampleRate) {
        if (!resampleBuffer(in, opt.targetRate, work, err)) return false;
    } else {
        work = in;
    }

    const int ch = work.channels;
    const size_t frames = work.frames();

    out.reset();
    out.channels = ch;
    out.sampleRate = work.sampleRate;
    out.bits.resize(frames * (size_t)ch);

    const int order = opt.mode == QuantMode::Hard ? 0 : (int)opt.mode;
    int nCoeff = 0;
    const double* coeff = order > 0 ? feedbackCoeffs(order, nCoeff) : nullptr;

    std::vector<double> errHist((size_t)ch * 4, 0.0);
    std::vector<float> prevIn((size_t)ch, 0.0f);
    std::vector<double> dcState((size_t)ch, 0.0);
    Rng rng(0x9E3779B9u);

    const double amp = ditherAmp(opt.dither);
    const double gain = opt.gain;

    int lastPercent = -1;
    for (size_t i = 0; i < frames; i++) {
        for (int c = 0; c < ch; c++) {
            double x = (double)work.data[i * ch + c] * gain;

            // Gentle DC blocker: keeps the square wave centred.
            double y = x - prevIn[c] + 0.995 * dcState[c];
            prevIn[c] = (float)x;
            dcState[c] = y;
            x = y;

            if (amp > 0.0) x += (double)rng.nextSym() * amp;
            if (x > 2.0) x = 2.0;
            if (x < -2.0) x = -2.0;

            int bit;
            if (order == 0) {
                bit = (x >= 0.0) ? 1 : 0;
            } else {
                double v = x;
                double* e = &errHist[(size_t)c * 4];
                for (int k = 0; k < nCoeff; k++) v += coeff[k] * e[k];
                if (v > 4.0) v = 4.0;
                if (v < -4.0) v = -4.0;
                double q = (v >= 0.0) ? 1.0 : -1.0;
                bit = (v >= 0.0) ? 1 : 0;
                double eNew = v - q;
                for (int k = nCoeff - 1; k > 0; k--) e[k] = e[k - 1];
                e[0] = eNew;
            }
            out.bits[i * ch + c] = (uint8_t)bit;
        }

        if (progress && (i % 8192) == 0) {
            int percent = (int)((double)i * 100.0 / (double)frames);
            if (percent != lastPercent) {
                lastPercent = percent;
                wchar_t buf[128];
                swprintf(buf, 128, L"正在砸成 1bit... %d%%", percent);
                if (!progress((double)i / (double)frames, buf)) { err = L"已取消"; return false; }
            }
        }
    }
    return true;
}

bool modulateToDsfData(const AudioBuffer& in, int dsdRate, int order, std::vector<uint8_t>& outBytes,
                       int& outChannels, std::wstring& err, const ProgressFn& progress) {
    if (in.empty()) { err = L"输入音频为空"; return false; }
    if (dsdRate <= 0) dsdRate = 2822400;
    if (order < 1) order = 1;
    if (order > 4) order = 4;

    // DSF is mono/stereo only in practice: downmix anything wider.
    AudioBuffer src = in;
    if (src.channels > 2) {
        AudioBuffer mix;
        mix.channels = 2;
        mix.sampleRate = src.sampleRate;
        size_t n = src.frames();
        mix.data.resize(n * 2);
        for (size_t i = 0; i < n; i++) {
            double l = 0, r = 0;
            for (int c = 0; c < src.channels; c++) {
                double v = src.data[i * src.channels + c];
                if (c % 2 == 0) l += v; else r += v;
            }
            mix.data[i * 2 + 0] = (float)(l / (src.channels / 2.0));
            mix.data[i * 2 + 1] = (float)(r / (src.channels / 2.0));
        }
        src = mix;
    }
    const int ch = src.channels;
    const size_t frames = src.frames();
    const double ratio = (double)dsdRate / (double)src.sampleRate;
    const size_t dsdFrames = (size_t)((double)frames * ratio);
    if (dsdFrames < 8) { err = L"音频太短"; return false; }

    outChannels = ch;
    int nCoeff = 0;
    const double* coeff = feedbackCoeffs(order, nCoeff);

    // Per channel: modulate and pack LSB first.
    std::vector<std::vector<uint8_t>> packed((size_t)ch);
    for (int c = 0; c < ch; c++) packed[c].assign((dsdFrames + 7) / 8, 0);

    std::vector<double> errHist((size_t)ch * 4, 0.0);
    size_t srcPos = 0;
    int lastPercent = -1;

    for (size_t i = 0; i < dsdFrames; i++) {
        // Zero order hold / linear interpolation from the PCM source.
        double srcIndex = (double)i / ratio;
        size_t i0 = (size_t)srcIndex;
        if (i0 >= frames) i0 = frames - 1;
        size_t i1 = std::min(i0 + 1, frames - 1);
        double frac = srcIndex - (double)i0;

        for (int c = 0; c < ch; c++) {
            double a = src.data[i0 * ch + c];
            double b = src.data[i1 * ch + c];
            double x = a + (b - a) * frac;

            double v = x;
            double* e = &errHist[(size_t)c * 4];
            for (int k = 0; k < nCoeff; k++) v += coeff[k] * e[k];
            if (v > 4.0) v = 4.0;
            if (v < -4.0) v = -4.0;
            double q = (v >= 0.0) ? 1.0 : -1.0;
            double eNew = v - q;
            for (int k = nCoeff - 1; k > 0; k--) e[k] = e[k - 1];
            e[0] = eNew;

            if (q > 0.0) packed[c][i >> 3] |= (uint8_t)(1u << (i & 7));
        }

        if (progress && (i % 262144) == 0) {
            int percent = (int)((double)i * 100.0 / (double)dsdFrames);
            if (percent != lastPercent) {
                lastPercent = percent;
                wchar_t buf[128];
                swprintf(buf, 128, L"正在调制 DSD... %d%%", percent);
                if (!progress((double)i / (double)dsdFrames, buf)) { err = L"已取消"; return false; }
            }
        }
        srcPos = i0;
    }
    (void)srcPos;

    // DSF block interleaving: 4096 byte blocks per channel, channel after channel.
    const size_t blockSize = 4096;
    size_t perChannel = packed[0].size();
    size_t blocks = (perChannel + blockSize - 1) / blockSize;
    outBytes.clear();
    outBytes.reserve(blocks * blockSize * (size_t)ch);
    for (size_t b = 0; b < blocks; b++) {
        for (int c = 0; c < ch; c++) {
            size_t start = b * blockSize;
            std::vector<uint8_t> block(blockSize, 0);
            size_t avail = start < perChannel ? std::min(blockSize, perChannel - start) : 0;
            if (avail) memcpy(block.data(), packed[c].data() + start, avail);
            outBytes.insert(outBytes.end(), block.begin(), block.end());
        }
    }
    return true;
}
