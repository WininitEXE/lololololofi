#include "engine.h"
#include "wavio.h"
#include "mfdec.h"
#include <algorithm>
#include <cmath>
#include <new>

std::wstring outFormatName(OutFormat f) {
    switch (f) {
        case OutFormat::Wav8:   return L"WAV 8bit (1bit 方波, 最通用)";
        case OutFormat::Wav16:  return L"WAV 16bit (1bit 内容)";
        case OutFormat::Raw1Bit:return L"RAW 1bit 位流 (.bit)";
        default:                return L"DSF / DSD64 (真 1bit 容器)";
    }
}

std::wstring outFormatExtension(OutFormat f) {
    switch (f) {
        case OutFormat::Raw1Bit: return L".bit";
        case OutFormat::Dsf:     return L".dsf";
        default:                 return L".wav";
    }
}

std::wstring defaultOutputPath(const std::wstring& input, OutFormat format) {
    std::wstring dir = dirName(input);
    std::wstring base = stripExt(baseName(input));
    std::wstring name = base + L"_1bit" + outFormatExtension(format);
    if (dir.empty()) return name;
    return dir + L"\\" + name;
}

namespace {

bool freeSpaceFor(const std::wstring& dir, unsigned long long& freeBytes) {
    ULARGE_INTEGER avail = {}, total = {}, free = {};
    std::wstring d = dir.empty() ? std::wstring(L".") : dir;
    if (!GetDiskFreeSpaceExW(d.c_str(), &avail, &total, &free)) return false;
    freeBytes = (unsigned long long)avail.QuadPart;
    return true;
}

std::wstring exeDirectory() {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (n == 0) return std::wstring();
    std::wstring s(buf, n);
    return dirName(s);
}

// Picks an output path that can actually be created. Falls back to Music /
// Desktop / Documents / temp / the exe folder when the requested location is
// read-only, protected, or out of space.
std::wstring resolveOutputPath(const ConvertRequest& req, ConvertResult& result,
                               unsigned long long needBytes, std::wstring& note) {
    std::wstring wanted = req.output.empty() ? defaultOutputPath(req.input, req.format) : req.output;
    if (!req.output.empty() && extLower(wanted).empty()) wanted += outFormatExtension(req.format);
    result.requestedPath = wanted;

    std::wstring fileName = baseName(wanted);
    std::wstring dir = dirName(wanted);
    if (dir.empty()) dir = L".";

    std::vector<std::wstring> candidates;
    wchar_t envBuf[MAX_PATH * 4] = {0};
    if (GetEnvironmentVariableW(L"ONEBIT_OUTDIR", envBuf, MAX_PATH * 4) > 0) candidates.push_back(envBuf);
    candidates.push_back(dir);
    candidates.push_back(knownFolderPath(0));   // Music
    candidates.push_back(knownFolderPath(1));   // Desktop
    candidates.push_back(knownFolderPath(2));   // Documents
    candidates.push_back(tempDirPath());
    candidates.push_back(exeDirectory());

    std::vector<std::wstring> problems;
    for (size_t i = 0; i < candidates.size(); i++) {
        const std::wstring c = candidates[i];
        if (c.empty()) continue;
        std::wstring cerr;
        if (!ensureDirectoryExists(c, cerr)) { problems.push_back(cerr); continue; }
        std::wstring werr;
        if (!isDirectoryWritable(c, werr)) { problems.push_back(werr); continue; }
        if (needBytes > 0) {
            unsigned long long freeBytes = 0;
            if (freeSpaceFor(c, freeBytes) && freeBytes < needBytes) {
                problems.push_back(L"空间不足: " + c + L" 需要 " + formatBytes(needBytes));
                continue;
            }
        }
        std::wstring path = c + L"\\" + fileName;
        if (i > 0 || c != dir) {
            result.redirected = true;
            note = L"原输出位置不可写";
            if (!problems.empty()) note += L" (" + problems[0] + L")";
            note += L"，已自动改存到: " + path;
        }
        return path;
    }
    note = problems.empty() ? std::wstring() : problems[0];
    return wanted;
}

bool writeOutput(const ConvertRequest& req, const BitStream& bits, const AudioBuffer& original,
                 ConvertResult& result, std::wstring& err) {
    const std::wstring& path = result.outputPath;
    double level = req.level;
    if (level < 0) level = 0;
    if (level > 1) level = 1;

    switch (req.format) {
        case OutFormat::Wav8: {
            std::vector<uint8_t> bytes(bits.bits.size());
            int hi = (int)std::lround(128.0 + 127.0 * level);
            int lo = (int)std::lround(128.0 - 127.0 * level);
            if (hi > 255) hi = 255;
            if (lo < 0) lo = 0;
            for (size_t i = 0; i < bits.bits.size(); i++) bytes[i] = bits.bits[i] ? (uint8_t)hi : (uint8_t)lo;
            return writeWavPcm8(path, bytes, bits.channels, bits.sampleRate, err);
        }
        case OutFormat::Wav16: {
            std::vector<int16_t> pcm(bits.bits.size());
            int hi = (int)std::lround(32767.0 * level);
            int lo = (int)std::lround(-32768.0 * level);
            for (size_t i = 0; i < bits.bits.size(); i++) pcm[i] = (int16_t)(bits.bits[i] ? hi : lo);
            return writeWavPcm16(path, pcm, bits.channels, bits.sampleRate, err);
        }
        case OutFormat::Raw1Bit: {
            std::vector<uint8_t> packed = packBitsLsbFirst(bits.bits);
            return writeRaw1Bit(path, packed, err);
        }
        default: {
            // DSF: modulate straight from the source at the DSD rate.
            std::vector<uint8_t> dsd;
            int channels = 0;
            std::wstring derr;
            int order = 2;
            if (req.opt.mode == QuantMode::SD3 || req.opt.mode == QuantMode::SD4) order = 3;
            if (!modulateToDsfData(original, 2822400, order, dsd, channels, derr, nullptr)) {
                err = derr;
                return false;
            }
            return writeDsf(path, dsd, channels, 2822400, err);
        }
    }
    (void)result;
}

std::vector<int16_t> toPcm16(const AudioBuffer& buf, size_t maxFrames) {
    size_t frames = std::min(buf.frames(), maxFrames);
    std::vector<int16_t> out(frames * (size_t)buf.channels);
    for (size_t i = 0; i < frames * (size_t)buf.channels; i++) {
        double v = buf.data[i];
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        out[i] = (int16_t)std::lround(v * 32767.0);
    }
    return out;
}

} // namespace

std::vector<uint8_t> buildPreviewWav(const AudioBuffer& buf) {
    if (buf.empty()) return std::vector<uint8_t>();
    AudioBuffer use = buf;
    if (use.channels > 2) {
        size_t frames = use.frames();
        std::vector<float> mixed(frames * 2, 0.0f);
        for (size_t i = 0; i < frames; i++) {
            float l = 0, r = 0;
            for (int c = 0; c < use.channels; c++) {
                if (c % 2 == 0) l += use.data[i * use.channels + c];
                else r += use.data[i * use.channels + c];
            }
            mixed[i * 2 + 0] = l / (use.channels / 2.0f);
            mixed[i * 2 + 1] = r / (use.channels / 2.0f);
        }
        use.data.swap(mixed);
        use.channels = 2;
    }
    // previews stay short so SoundPlay feels instant
    size_t maxFrames = (size_t)use.sampleRate * 60;
    std::vector<int16_t> pcm = toPcm16(use, maxFrames);
    return makeWavPcm16InMemory(pcm, use.channels, use.sampleRate);
}

std::vector<uint8_t> buildPreviewWavFromBits(const BitStream& bits) {
    if (bits.bits.empty() || bits.channels <= 0) return std::vector<uint8_t>();
    size_t frames = bits.frames();
    size_t maxFrames = (size_t)bits.sampleRate * 60;
    if (frames > maxFrames) frames = maxFrames;
    if (bits.channels > 2) {
        std::vector<int16_t> pcm(frames * 2, 0);
        for (size_t i = 0; i < frames; i++) {
            int l = 0, r = 0, nl = 0, nr = 0;
            for (int c = 0; c < bits.channels; c++) {
                int s = bits.bits[i * bits.channels + c] ? 32767 : -32768;
                if (c % 2 == 0) { l += s; nl++; } else { r += s; nr++; }
            }
            pcm[i * 2 + 0] = (int16_t)(nl ? l / nl : 0);
            pcm[i * 2 + 1] = (int16_t)(nr ? r / nr : 0);
        }
        return makeWavPcm16InMemory(pcm, 2, bits.sampleRate);
    }
    std::vector<int16_t> pcm(frames * (size_t)bits.channels);
    for (size_t i = 0; i < pcm.size(); i++) pcm[i] = bits.bits[i] ? 32767 : -32768;
    return makeWavPcm16InMemory(pcm, bits.channels, bits.sampleRate);
}

namespace {

// Rough size of the file we are about to write, used for the free space check.
unsigned long long estimateOutputBytes(const ConvertRequest& req, const BitStream& bits,
                                       const AudioBuffer& decoded) {
    const size_t frames = bits.channels > 0 ? bits.frames() : decoded.frames();
    const int ch = bits.channels > 0 ? bits.channels : decoded.channels;
    switch (req.format) {
        case OutFormat::Wav16:  return (unsigned long long)frames * ch * 2 + 64;
        case OutFormat::Raw1Bit:return ((unsigned long long)frames * ch + 7) / 8;
        case OutFormat::Dsf:    return (unsigned long long)(decoded.seconds() * 2822400.0 / 8.0) * (ch > 2 ? 2 : ch) + 256;
        default:                return (unsigned long long)frames * ch + 64;
    }
}

} // namespace

bool runConversion(const ConvertRequest& req, ConvertResult& result, std::wstring& err, const ProgressFn& progress) {
    result = ConvertResult();
    try {
    if (!fileExists(req.input)) { err = L"输入文件不存在: " + req.input; return false; }

    // Fail fast when the requested output location cannot be used at all and
    // move to a writable folder right away (see resolveOutputPath).
    {
        std::wstring note;
        result.outputPath = resolveOutputPath(req, result, 0, note);
        result.redirectNote = note;
    }

    auto report = [&](double frac, const wchar_t* text) -> bool {
        if (!progress) return true;
        return progress(frac, text);
    };

    // ---- 1. decode ------------------------------------------------------
    if (!report(0.02, L"正在读取并解码音频...")) { err = L"已取消"; return false; }
    AudioBuffer decoded;
    {
        ProgressFn sub = nullptr;
        if (progress) {
            sub = [&](double f, const std::wstring& s) { return progress(0.02 + f * 0.48, s); };
        }
        if (!decodeAudioFile(req.input, decoded, err, sub)) return false;
    }
    if (decoded.empty()) { err = L"解码结果为空"; return false; }
    result.channels = decoded.channels;
    result.sampleRate = decoded.sampleRate;
    result.seconds = decoded.seconds();
    if (!report(0.52, L"解码完成")) { err = L"已取消"; return false; }

    // ---- 2. smash into 1 bit -------------------------------------------
    BitStream bits;
    if (req.format != OutFormat::Dsf) {
        ProgressFn sub = nullptr;
        if (progress) sub = [&](double f, const std::wstring& s) { return progress(0.52 + f * 0.33, s); };
        if (!convertTo1Bit(decoded, req.opt, bits, err, sub)) return false;
        if (!report(0.86, L"1bit 量化完成")) { err = L"已取消"; return false; }
    }

    // ---- 3. write -------------------------------------------------------
    if (!report(0.9, req.format == OutFormat::Dsf ? L"正在调制 DSD 并写出..." : L"正在写出文件...")) {
        err = L"已取消";
        return false;
    }
    // Now that the real size is known, make sure the target still has room.
    {
        unsigned long long need = estimateOutputBytes(req, bits, decoded);
        std::wstring note;
        std::wstring better = resolveOutputPath(req, result, need, note);
        if (!better.empty()) {
            result.outputPath = better;
            if (!note.empty()) result.redirectNote = note;
        }
    }
    if (!writeOutput(req, bits, decoded, result, err)) return false;

    if (req.keepPreview) {
        const double kPreviewSeconds = 45.0;
        if (decoded.seconds() <= kPreviewSeconds) {
            result.original = decoded;
            result.bits = bits;
        } else {
            result.previewTruncated = true;
            size_t frames = (size_t)(kPreviewSeconds * decoded.sampleRate);
            if (frames > decoded.frames()) frames = decoded.frames();
            result.original.channels = decoded.channels;
            result.original.sampleRate = decoded.sampleRate;
            result.original.data.assign(decoded.data.begin(),
                                        decoded.data.begin() + frames * (size_t)decoded.channels);
            if (!bits.bits.empty() && bits.channels > 0) {
                size_t bframes = std::min(frames, bits.frames());
                result.bits.channels = bits.channels;
                result.bits.sampleRate = bits.sampleRate;
                result.bits.bits.assign(bits.bits.begin(), bits.bits.begin() + bframes * (size_t)bits.channels);
            }
        }
    }

    if (!report(1.0, L"转换完成")) { err = L"已取消"; return false; }
    return true;

    } catch (const std::bad_alloc&) {
        err = L"内存不足：这个音频太长/太大了，换短一点的试试，或先用其他工具切成小段。";
        return false;
    } catch (const std::exception& e) {
        err = L"内部错误: " + fromUtf8(e.what());
        return false;
    } catch (...) {
        err = L"内部错误（未知异常）";
        return false;
    }
}
