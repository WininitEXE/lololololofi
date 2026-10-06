// main.cpp - entry point: no arguments opens the GUI, "-convert" runs the CLI.
#include "gui.h"
#include "engine.h"
#include "mfdec.h"
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const wchar_t* kAppName = L"1bit 全损音质转换器";
const wchar_t* kVersion = L"1.0";

bool isConsoleOwner() {
    DWORD pids[4] = {0};
    DWORD n = GetConsoleProcessList(pids, 4);
    return n <= 1;
}

void setupConsoleEncoding() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void printHelp() {
    printf("1bit 全损音质转换器 v%s - 把任何音频砸成 1bit\n\n", toUtf8(kVersion).c_str());
    printf("用法:\n");
    printf("  1bit.exe                                打开图形界面\n");
    printf("  1bit.exe -gui [文件] [--auto]           打开图形界面并预载文件 (--auto 立即开始)\n");
    printf("  1bit.exe -convert <输入> [更多输入...]   命令行转换\n\n");
    printf("参数:\n");
    printf("  -o, --out <文件>      输出文件 (仅单个输入时可用, 默认 <原名>_1bit.<后缀>)\n");
    printf("  -d, --outdir <目录>   输出到指定目录\n");
    printf("  --format <类型>       输出格式: wav8 (默认) | wav16 | raw | dsf\n");
    printf("  --mode <模式>         量化模式: hard (默认 硬切方波) | sd1 | sd2 | sd3 | sd4\n");
    printf("  --dither <程度>       抖动: none (默认) | light | strong\n");
    printf("  --rate <Hz>           先降采样到该采样率再量化, 例如 8000\n");
    printf("  --level <0-1>         输出电平, 默认 1.0\n");
    printf("  --gain <倍数>         输入增益, 默认 1.0\n");
    printf("  -q, --quiet           不显示进度条\n");
    printf("  --check <路径>        自检: 目录能不能写 / 文件能不能解码, 并给出精确原因\n");
    printf("  -h, --help            显示本帮助\n\n");
    printf("示例:\n");
    printf("  1bit.exe --check E:\\\n");
    printf("  1bit.exe --check \"E:\\某首歌.wav\"\n");
    printf("  1bit.exe -convert music.flac\n");
    printf("  1bit.exe -convert a.mp3 b.ogg --format wav8 --mode sd2 --rate 11025\n");
    printf("  1bit.exe -convert song.m4a -o song_destroyed.wav --dither strong --level 0.8\n\n");
    printf("提示: 若原目录不可写（只读盘/权限/杀软拦截），程序会自动改存到 音乐/桌面/文档/临时目录\n");
    printf("      并在结果里说明；也可以用 --outdir 指定一个可写目录。\n\n");
    printf("支持格式: mp3 / flac / wav / ogg / opus / m4a(aac) / wma / alac / webm 等\n");
    printf("         (mp3/flac/aac/wma 等由 Windows Media Foundation 解码,\n");
    printf("          ogg/opus 由内置 Ogg 解析器 + 系统解码器处理)\n");
}

int runCheck(const std::wstring& target) {
    printf("检查目标: %s\n", toUtf8(target).c_str());
    if (target.empty()) { printf("  用法: 1bit.exe --check <目录或音频文件>\n"); return 2; }

    if (dirExists(target)) {
        unsigned long long avail = 0, total = 0, freeBytes = 0;
        ULARGE_INTEGER a = {}, t = {}, f = {};
        if (GetDiskFreeSpaceExW(target.c_str(), &a, &t, &f)) {
            avail = (unsigned long long)a.QuadPart;
            total = (unsigned long long)t.QuadPart;
            freeBytes = (unsigned long long)f.QuadPart;
        }
        std::wstring err;
        bool writable = isDirectoryWritable(target, err);
        printf("  类型      : 目录\n");
        printf("  可写      : %s\n", writable ? "是" : "否");
        if (!writable) printf("  失败原因  : %s\n", toUtf8(err).c_str());
        if (total) {
            printf("  剩余空间  : %s / %s\n", toUtf8(formatBytes(avail)).c_str(), toUtf8(formatBytes(total)).c_str());
            if (avail < 50ull * 1024 * 1024) printf("  提示      : 剩余空间偏小，转换可能写不下\n");
        }
        return writable ? 0 : 1;
    }

    if (!fileExists(target)) { printf("  错误      : 路径不存在\n"); return 2; }

    HANDLE h = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  可读      : 否 - %s\n", toUtf8(win32ErrorText(GetLastError())).c_str());
        return 1;
    }
    LARGE_INTEGER sz = {};
    GetFileSizeEx(h, &sz);
    CloseHandle(h);
    printf("  类型      : 音频文件 (%s)\n", toUtf8(formatBytes((unsigned long long)sz.QuadPart)).c_str());
    printf("  可读      : 是\n");

    AudioBuffer buf;
    std::wstring err;
    ULONGLONG t0 = GetTickCount64();
    bool ok = decodeAudioFile(target, buf, err, nullptr);
    if (!ok) {
        printf("  可解码    : 否 - %s\n", toUtf8(err).c_str());
        return 1;
    }
    printf("  可解码    : 是 (%d Hz, %d 声道, %.1f 秒, 耗时 %.2f 秒)\n",
           buf.sampleRate, buf.channels, buf.seconds(), (GetTickCount64() - t0) / 1000.0);

    std::wstring dir = dirName(target);
    if (dir.empty() || !dirExists(dir)) { printf("  同目录可写: 否 - 目录无效\n"); return 1; }
    std::wstring werr;
    bool writable = isDirectoryWritable(dir, werr);
    printf("  同目录可写: %s\n", writable ? "是" : "否");
    if (!writable) {
        printf("  失败原因  : %s\n", toUtf8(werr).c_str());
        printf("  建议      : 用 -o 指定其它位置，或用 --outdir 指定一个可写目录\n");
    }
    return writable ? 0 : 1;
}

struct CliOptions {
    std::vector<std::wstring> inputs;
    std::wstring output;
    std::wstring outDir;
    std::wstring checkTarget;
    OutFormat format = OutFormat::Wav8;
    ConvertOptions opt;
    double level = 1.0;
    bool quiet = false;
    bool help = false;
};

bool parseArgs(int argc, wchar_t** argv, CliOptions& o, std::wstring& err) {
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto needValue = [&](std::wstring& out) -> bool {
            if (i + 1 >= argc) { err = L"参数 " + a + L" 缺少取值"; return false; }
            out = argv[++i];
            return true;
        };
        if (a == L"-h" || a == L"--help" || a == L"-?") { o.help = true; return true; }
        else if (a == L"-convert" || a == L"--convert" || a == L"-c") {
            // everything that is not an option and not consumed by one is an input
            continue;
        } else if (a == L"-o" || a == L"--out") {
            if (!needValue(o.output)) return false;
        } else if (a == L"--outdir" || a == L"-d") {
            if (!needValue(o.outDir)) return false;
        } else if (a == L"--check") {
            if (!needValue(o.checkTarget)) return false;
        } else if (a == L"--format") {
            std::wstring v;
            if (!needValue(v)) return false;
            if (v == L"wav8" || v == L"wav" || v == L"8") o.format = OutFormat::Wav8;
            else if (v == L"wav16" || v == L"16") o.format = OutFormat::Wav16;
            else if (v == L"raw" || v == L"bit") o.format = OutFormat::Raw1Bit;
            else if (v == L"dsf" || v == L"dsd") o.format = OutFormat::Dsf;
            else { err = L"未知的输出格式: " + v; return false; }
        } else if (a == L"--mode") {
            std::wstring v;
            if (!needValue(v)) return false;
            if (v == L"hard" || v == L"1bit" || v == L"slice") o.opt.mode = QuantMode::Hard;
            else if (v == L"sd1") o.opt.mode = QuantMode::SD1;
            else if (v == L"sd2") o.opt.mode = QuantMode::SD2;
            else if (v == L"sd3") o.opt.mode = QuantMode::SD3;
            else if (v == L"sd4") o.opt.mode = QuantMode::SD4;
            else { err = L"未知的量化模式: " + v; return false; }
        } else if (a == L"--dither") {
            std::wstring v;
            if (!needValue(v)) return false;
            if (v == L"none" || v == L"0") o.opt.dither = DitherLevel::None;
            else if (v == L"light" || v == L"low") o.opt.dither = DitherLevel::Light;
            else if (v == L"strong" || v == L"high") o.opt.dither = DitherLevel::Strong;
            else { err = L"未知的抖动程度: " + v; return false; }
        } else if (a == L"--rate") {
            std::wstring v;
            if (!needValue(v)) return false;
            o.opt.targetRate = _wtoi(v.c_str());
            if (o.opt.targetRate < 1000 || o.opt.targetRate > 384000) { err = L"采样率超出范围 (1000-384000)"; return false; }
        } else if (a == L"--level") {
            std::wstring v;
            if (!needValue(v)) return false;
            o.level = _wtof(v.c_str());
            if (o.level <= 0 || o.level > 1) { err = L"电平必须在 0 到 1 之间"; return false; }
        } else if (a == L"--gain") {
            std::wstring v;
            if (!needValue(v)) return false;
            o.opt.gain = _wtof(v.c_str());
            if (o.opt.gain <= 0 || o.opt.gain > 64) { err = L"增益必须在 0 到 64 之间"; return false; }
        } else if (a == L"-q" || a == L"--quiet") {
            o.quiet = true;
        } else if (!a.empty() && a[0] == L'-' && a.size() > 1 && !(a[1] >= L'0' && a[1] <= L'9')) {
            err = L"未知参数: " + a;
            return false;
        } else {
            o.inputs.push_back(a);
        }
    }
    return true;
}

void showProgress(double frac, const std::wstring& text) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int width = 36;
    int filled = (int)(frac * width + 0.5);
    std::string bar;
    for (int i = 0; i < width; i++) bar += (i < filled) ? '#' : '.';
    printf("\r  [%s] %3d%%  %-40s", bar.c_str(), (int)(frac * 100 + 0.5), toUtf8(text).c_str());
    fflush(stdout);
}

int runCli(const CliOptions& o) {
    int failures = 0;
    for (size_t i = 0; i < o.inputs.size(); i++) {
        ConvertRequest req;
        req.input = o.inputs[i];
        req.format = o.format;
        req.opt = o.opt;
        req.level = o.level;
        if (o.inputs.size() == 1) req.output = o.output;
        if (!o.outDir.empty()) {
            std::wstring name = baseName(defaultOutputPath(req.input, req.format));
            req.output = o.outDir;
            if (!req.output.empty() && req.output.back() != L'\\' && req.output.back() != L'/') req.output += L'\\';
            req.output += name;
        }

        printf("\n=== [%d/%d] %s\n", (int)i + 1, (int)o.inputs.size(), toUtf8(baseName(req.input)).c_str());
        fflush(stdout);

        ConvertResult result;
        std::wstring err;
        ProgressFn progress = nullptr;
        if (!o.quiet) progress = [](double f, const std::wstring& s) { showProgress(f, s); return true; };

        ULONGLONG t0 = GetTickCount64();
        bool ok = runConversion(req, result, err, progress);
        ULONGLONG ms = GetTickCount64() - t0;
        if (!o.quiet) { printf("\r%s\r", std::string(80, ' ').c_str()); }
        if (!ok) {
            printf("  [失败] %s\n", toUtf8(err).c_str());
            failures++;
            continue;
        }
        unsigned long long size = 0;
        {
            HANDLE h = CreateFileW(result.outputPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz;
                if (GetFileSizeEx(h, &sz)) size = (unsigned long long)sz.QuadPart;
                CloseHandle(h);
            }
        }
        if (result.redirected) {
            printf("  [注意] %s\n", toUtf8(result.redirectNote).c_str());
        }
        printf("  [完成] %s\n", toUtf8(result.outputPath).c_str());
        printf("         时长 %.2f 秒 | 输出 %s | 1bit @ %d Hz %d 声道 | 耗时 %.2f 秒\n",
               result.seconds,
               toUtf8(formatBytes(size)).c_str(),
               o.format == OutFormat::Dsf ? 2822400 : (o.opt.targetRate > 0 ? o.opt.targetRate : result.sampleRate),
               result.channels,
               ms / 1000.0);
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;

    CliOptions opts;
    std::wstring err;
    bool parsed = parseArgs(argc, argv, opts, err);

    bool guiMode = (argc <= 1);
    std::wstring guiFile;
    bool guiAuto = false;
    // "-gui [file] [--auto]" forces the window open and can preload a file
    // (also used by the automated GUI smoke test).
    {
        bool sawGui = false;
        for (int i = 1; i < argc; i++) {
            std::wstring a = argv[i];
            if (a == L"-gui" || a == L"--gui") { guiMode = true; sawGui = true; continue; }
            if (a == L"--auto") { guiAuto = true; continue; }
            if (sawGui && guiFile.empty() && !a.empty() && a[0] != L'-') guiFile = a;
        }
    }

    if (guiMode) {
        // Hide the console window when we own it (double click).
        if (isConsoleOwner()) {
            HWND con = GetConsoleWindow();
            if (con) ShowWindow(con, SW_HIDE);
            FreeConsole();
        }
        LocalFree(argv);
        return runGui(guiFile, guiAuto);
    }

    setupConsoleEncoding();
    if (!parsed) {
        printf("参数错误: %s\n\n", toUtf8(err).c_str());
        printHelp();
        LocalFree(argv);
        return 2;
    }
    if (opts.help) { printHelp(); LocalFree(argv); return 0; }
    if (!opts.checkTarget.empty()) {
        int rc = runCheck(opts.checkTarget);
        LocalFree(argv);
        return rc;
    }
    if (opts.inputs.empty()) {
        printf("没有指定输入文件。使用 -convert <文件> 或直接运行不带参数打开图形界面。\n\n");
        printHelp();
        LocalFree(argv);
        return 2;
    }
    if (!opts.output.empty() && opts.inputs.size() > 1) {
        printf("提示: -o 只在单个输入文件时生效, 已忽略。\n");
    }

    printf("%s v%s\n", toUtf8(kAppName).c_str(), toUtf8(kVersion).c_str());
    printf("输出格式: %s | 量化: %s\n", toUtf8(outFormatName(opts.format)).c_str(), toUtf8(quantModeName(opts.opt.mode)).c_str());

    int rc = runCli(opts);
    LocalFree(argv);
    return rc;
}
