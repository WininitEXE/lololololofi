// gui.cpp - Win32 GUI for the 1bit destroyer.
#include "gui.h"
#include "engine.h"
#include "wavio.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace {

enum {
    IDC_INPUT = 1000, IDC_BROWSE_IN, IDC_OUTPUT, IDC_BROWSE_OUT,
    IDC_FORMAT, IDC_MODE, IDC_DITHER, IDC_RATE, IDC_LEVEL,
    IDC_CONVERT, IDC_PLAY_ORIG, IDC_PLAY_BIT, IDC_STOP, IDC_OPEN_DIR,
    IDC_STATUS, IDC_PROGRESS, IDC_HINT
};

const UINT WM_APP_PROGRESS = WM_APP + 1;
const UINT WM_APP_DONE = WM_APP + 2;

HWND g_hwnd = nullptr;
std::wstring g_initialFile;
bool g_autoStart = false;
int g_wantW = 0, g_wantH = 0;
HFONT g_font = nullptr;
HFONT g_fontBold = nullptr;
std::atomic<bool> g_busy(false);
std::atomic<bool> g_cancel(false);
std::thread g_worker;
ConvertResult g_result;
std::wstring g_error;
bool g_ok = false;
std::vector<uint8_t> g_previewOrig;
std::vector<uint8_t> g_previewBits;
RECT g_waveRect = {0, 0, 0, 0};
int g_dpi = 96;

int S(int v) { return MulDiv(v, g_dpi, 96); }

struct ProgressMsg {
    double frac;
    std::wstring text;
};

HWND ctl(int id) { return GetDlgItem(g_hwnd, id); }

std::wstring getText(int id) {
    HWND h = ctl(id);
    int len = GetWindowTextLengthW(h);
    std::wstring s((size_t)len + 1, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    s.resize((size_t)len);
    return s;
}

void setText(int id, const std::wstring& s) {
    SetWindowTextW(ctl(id), s.c_str());
}

void setStatus(const std::wstring& s) {
    setText(IDC_STATUS, s);
}

int comboIndex(int id) {
    return (int)SendMessageW(ctl(id), CB_GETCURSEL, 0, 0);
}

void comboAdd(int id, const wchar_t* text) {
    SendMessageW(ctl(id), CB_ADDSTRING, 0, (LPARAM)text);
}

void playWavMemory(std::vector<uint8_t>& data) {
    if (data.empty()) return;
    PlaySoundW(nullptr, nullptr, 0);
    PlaySoundW((LPCWSTR)data.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void stopSound() { PlaySoundW(nullptr, nullptr, 0); }

// ---------------------------------------------------------------------------
// worker
// ---------------------------------------------------------------------------
void startConvert() {
    if (g_busy.load()) return;
    std::wstring input = getText(IDC_INPUT);
    if (input.empty()) {
        MessageBoxW(g_hwnd, L"请先选择要转换的音频文件。", L"缺少输入文件", MB_ICONINFORMATION);
        return;
    }
    if (!fileExists(input)) {
        MessageBoxW(g_hwnd, L"找不到这个文件，请重新选择。", L"文件不存在", MB_ICONWARNING);
        return;
    }

    ConvertRequest req;
    req.input = input;
    req.output = getText(IDC_OUTPUT);
    req.format = (OutFormat)comboIndex(IDC_FORMAT);
    req.opt.mode = (QuantMode)comboIndex(IDC_MODE);
    req.opt.dither = (DitherLevel)comboIndex(IDC_DITHER);
    switch (comboIndex(IDC_RATE)) {
        case 1: req.opt.targetRate = 22050; break;
        case 2: req.opt.targetRate = 11025; break;
        case 3: req.opt.targetRate = 8000; break;
        case 4: req.opt.targetRate = 5512; break;
        default: req.opt.targetRate = 0; break;
    }
    static const double levels[] = {1.0, 0.5, 0.25, 0.1};
    int li = comboIndex(IDC_LEVEL);
    req.level = (li >= 0 && li < 4) ? levels[li] : 1.0;
    req.keepPreview = true;

    if (req.output.empty()) {
        req.output = defaultOutputPath(req.input, req.format);
        setText(IDC_OUTPUT, req.output);
    }

    stopSound();
    g_cancel.store(false);
    g_busy.store(true);
    g_ok = false;
    g_error.clear();
    g_result = ConvertResult();
    g_previewOrig.clear();
    g_previewBits.clear();
    EnableWindow(ctl(IDC_CONVERT), FALSE);
    SendMessageW(ctl(IDC_PROGRESS), PBM_SETPOS, 0, 0);
    setStatus(L"开始处理...");
    InvalidateRect(g_hwnd, &g_waveRect, FALSE);

    HWND hwnd = g_hwnd;
    if (g_worker.joinable()) g_worker.join();
    g_worker = std::thread([hwnd, req]() {
        ConvertResult result;
        std::wstring err;
        bool ok = runConversion(req, result, err, [hwnd](double f, const std::wstring& s) {
            if (g_cancel.load()) return false;
            ProgressMsg* msg = new ProgressMsg();
            msg->frac = f;
            msg->text = s;
            PostMessageW(hwnd, WM_APP_PROGRESS, 0, (LPARAM)msg);
            return true;
        });
        {
            g_result = std::move(result);
            g_error = err;
            g_ok = ok;
        }
        PostMessageW(hwnd, WM_APP_DONE, 0, 0);
    });
}

void onDone() {
    if (g_worker.joinable()) g_worker.join();
    g_busy.store(false);
    EnableWindow(ctl(IDC_CONVERT), TRUE);
    SendMessageW(ctl(IDC_PROGRESS), PBM_SETPOS, g_ok ? 1000 : 0, 0);

    if (!g_ok) {
        if (g_cancel.load()) setStatus(L"已取消");
        else setStatus(L"转换失败: " + g_error);   // precise reason included
        if (!g_cancel.load()) {
            std::wstring msg = g_error.empty() ? L"转换失败。" : g_error;
            MessageBoxW(g_hwnd, msg.c_str(), L"转换失败", MB_ICONERROR);
        }
        return;
    }

    if (g_result.redirected) {
        setText(IDC_OUTPUT, g_result.outputPath);
        std::wstring msg = L"原输出位置写不进去（可能是只读盘、没有权限、磁盘已满或被安全软件拦截）。\n\n"
                           L"程序已自动把结果保存到:\n" + g_result.outputPath +
                           L"\n\n下次可以先用“输出文件 - 浏览...”换一个位置。";
        MessageBoxW(g_hwnd, msg.c_str(), L"输出位置已自动更换", MB_ICONWARNING);
    }
    g_previewOrig = buildPreviewWav(g_result.original);
    g_previewBits = buildPreviewWavFromBits(g_result.bits);
    if (g_result.previewTruncated) {
        setStatus(L"完成 (试听/波形仅保留前 45 秒)");
    } else {
        setStatus(L"完成");
    }
    std::wstring info = L"已输出: " + baseName(g_result.outputPath);
    if (!g_result.seconds) info = L"已输出: " + baseName(g_result.outputPath);
    setStatus(info + L"   |   直接点“试听 1bit”感受全损音质");
    InvalidateRect(g_hwnd, &g_waveRect, FALSE);
}

// ---------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------
void drawWave(HDC hdc) {
    RECT r = g_waveRect;
    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 26));
    FillRect(hdc, &r, bg);
    DeleteObject(bg);

    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    HPEN border = CreatePen(PS_SOLID, 1, RGB(60, 66, 80));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);

    int midTop = r.top + h / 4;
    int midBot = r.top + (h * 3) / 4;
    HPEN axis = CreatePen(PS_SOLID, 1, RGB(48, 54, 66));
    HGDIOBJ oldAxis = SelectObject(hdc, axis);
    MoveToEx(hdc, r.left, midTop, nullptr);
    LineTo(hdc, r.right, midTop);
    MoveToEx(hdc, r.left, midBot, nullptr);
    LineTo(hdc, r.right, midBot);
    SelectObject(hdc, oldAxis);
    DeleteObject(axis);

    const AudioBuffer& orig = g_result.original;
    const BitStream& bits = g_result.bits;

    if (orig.data.empty() && bits.bits.empty()) {
        SetTextColor(hdc, RGB(90, 98, 116));
        RECT cr = r;
        DrawTextW(hdc, L"选择文件并点击“开始砸成 1bit”后，这里会显示抛弃前后的波形对比", -1, &cr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(border);
        return;
    }

    const int ch = orig.channels > 0 ? orig.channels : 1;
    const size_t frames = orig.frames();
    if (frames > 0) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(90, 170, 255));
        HGDIOBJ oldTrace = SelectObject(hdc, pen);
        int halfTop = (h / 2 - 6) / 2;
        for (int x = 0; x < w; x++) {
            size_t f0 = (size_t)((double)x * frames / w);
            size_t f1 = (size_t)((double)(x + 1) * frames / w);
            if (f1 <= f0) f1 = f0 + 1;
            if (f1 > frames) f1 = frames;
            float mn = 0, mx = 0;
            bool first = true;
            for (size_t f = f0; f < f1; f++) {
                float v = 0;
                for (int c = 0; c < ch; c++) v += orig.data[f * ch + c];
                v /= ch;
                if (first) { mn = mx = v; first = false; }
                else { if (v < mn) mn = v; if (v > mx) mx = v; }
            }
            int y0 = midTop - (int)(mx * halfTop);
            int y1 = midTop - (int)(mn * halfTop);
            if (y0 == y1) y1 = y0 + 1;
            MoveToEx(hdc, r.left + x, y0, nullptr);
            LineTo(hdc, r.left + x, y1);
        }
        SelectObject(hdc, oldTrace);
        DeleteObject(pen);
    }

    const size_t bframes = bits.frames();
    if (bframes > 0 && bits.channels > 0) {
        // 1 bit per sample is a square wave: to show something meaningful we run
        // the stream through two one-pole low passes (what a speaker/ear does).
        const int bc = bits.channels;
        const double sr = bits.sampleRate > 0 ? (double)bits.sampleRate : 44100.0;
        const double alpha = std::min(1.0, 1.0 - std::exp(-2.0 * 3.14159265358979 * 2500.0 / sr));
        std::vector<double> s1((size_t)bc, 0.0), s2((size_t)bc, 0.0);
        std::vector<double> colSum((size_t)w, 0.0), colPeak((size_t)w, 0.0);
        std::vector<double> colCount((size_t)w, 0.0);
        for (size_t f = 0; f < bframes; f++) {
            double mono = 0;
            for (int c = 0; c < bc; c++) {
                double x = bits.bits[f * bc + c] ? 1.0 : -1.0;
                s1[c] += (x - s1[c]) * alpha;
                s2[c] += (s1[c] - s2[c]) * alpha;
                mono += s2[c];
            }
            mono /= bc;
            if (mono > 1.0) mono = 1.0;
            if (mono < -1.0) mono = -1.0;
            size_t col = (size_t)((double)f * w / bframes);
            if (col >= (size_t)w) col = (size_t)w - 1;
            double a = mono < 0 ? -mono : mono;
            colSum[col] += a;
            if (a > colPeak[col]) colPeak[col] = a;
            colCount[col] += 1.0;
        }
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 90, 90));
        HGDIOBJ oldTrace2 = SelectObject(hdc, pen);
        int halfBot = (h / 2 - 6) / 2;
        // Loudness envelope of the demodulated 1bit signal (average rectified
        // value per pixel column) - shows how much music survived.
        for (int x = 0; x < w; x++) {
            if (colCount[x] <= 0) continue;
            double level = colSum[x] / colCount[x];
            if (level > 1.0) level = 1.0;
            int half = (int)(level * halfBot);
            if (half < 1) half = 1;
            MoveToEx(hdc, r.left + x, midBot - half, nullptr);
            LineTo(hdc, r.left + x, midBot + half);
        }
        SelectObject(hdc, oldTrace2);
        DeleteObject(pen);
    }

    // Labels last so they stay readable on top of the traces.
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, RGB(18, 20, 26));
    SetTextColor(hdc, RGB(140, 150, 170));
    RECT tr = r;
    tr.left += 8; tr.top += 4; tr.right = tr.left + 260; tr.bottom = tr.top + 18;
    DrawTextW(hdc, L"原始波形", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    tr.left = r.left + 8; tr.right = tr.left + 320;
    tr.top = r.top + h / 2 + 4; tr.bottom = tr.top + 18;
    DrawTextW(hdc, L"1bit 输出 (低通后 ≈ 实际听感)", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(border);
}

// ---------------------------------------------------------------------------
// controls
// ---------------------------------------------------------------------------
void addLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    HWND s = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                             S(x), S(y), S(w), S(h), parent, nullptr, nullptr, nullptr);
    SendMessageW(s, WM_SETFONT, (WPARAM)g_font, TRUE);
}

HWND addEdit(HWND parent, int id, int x, int y, int w, int h) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_font, TRUE);
    return e;
}

HWND addButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    return b;
}

HWND addCombo(HWND parent, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                             S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

void buildUi(HWND hwnd) {
    addLabel(hwnd, L"输入音频 (可拖放文件到窗口):", 16, 14, 260, 18);
    addEdit(hwnd, IDC_INPUT, 16, 34, 600, 26);
    addButton(hwnd, IDC_BROWSE_IN, L"浏览...", 624, 34, 90, 26);

    addLabel(hwnd, L"输出文件:", 16, 68, 260, 18);
    addEdit(hwnd, IDC_OUTPUT, 16, 88, 600, 26);
    addButton(hwnd, IDC_BROWSE_OUT, L"浏览...", 624, 88, 90, 26);

    addLabel(hwnd, L"输出格式", 16, 126, 90, 18);
    HWND fmt = addCombo(hwnd, IDC_FORMAT, 16, 146, 200, 200);
    comboAdd(IDC_FORMAT, L"WAV 8bit (1bit 方波)");
    comboAdd(IDC_FORMAT, L"WAV 16bit (1bit 内容)");
    comboAdd(IDC_FORMAT, L"RAW 1bit 位流 (.bit)");
    comboAdd(IDC_FORMAT, L"DSF / DSD64 (真 1bit)");
    SendMessageW(fmt, CB_SETCURSEL, 0, 0);

    addLabel(hwnd, L"量化模式", 232, 126, 90, 18);
    HWND mode = addCombo(hwnd, IDC_MODE, 232, 146, 200, 200);
    comboAdd(IDC_MODE, L"硬切 1bit (经典全损)");
    comboAdd(IDC_MODE, L"一阶 ΣΔ 噪声整形");
    comboAdd(IDC_MODE, L"二阶 ΣΔ 噪声整形");
    comboAdd(IDC_MODE, L"三阶 ΣΔ 噪声整形");
    comboAdd(IDC_MODE, L"四阶 ΣΔ 噪声整形");
    SendMessageW(mode, CB_SETCURSEL, 0, 0);

    addLabel(hwnd, L"抖动", 448, 126, 90, 18);
    HWND dith = addCombo(hwnd, IDC_DITHER, 448, 146, 120, 200);
    comboAdd(IDC_DITHER, L"不抖动");
    comboAdd(IDC_DITHER, L"轻微抖动");
    comboAdd(IDC_DITHER, L"强烈抖动");
    SendMessageW(dith, CB_SETCURSEL, 0, 0);

    addLabel(hwnd, L"降采样", 584, 126, 90, 18);
    HWND rate = addCombo(hwnd, IDC_RATE, 584, 146, 130, 300);
    comboAdd(IDC_RATE, L"保持原采样率");
    comboAdd(IDC_RATE, L"22050 Hz");
    comboAdd(IDC_RATE, L"11025 Hz");
    comboAdd(IDC_RATE, L"8000 Hz");
    comboAdd(IDC_RATE, L"5512 Hz");
    SendMessageW(rate, CB_SETCURSEL, 0, 0);

    addLabel(hwnd, L"输出电平", 16, 182, 90, 18);
    HWND lvl = addCombo(hwnd, IDC_LEVEL, 16, 202, 130, 200);
    comboAdd(IDC_LEVEL, L"100% (最吵)");
    comboAdd(IDC_LEVEL, L"50%");
    comboAdd(IDC_LEVEL, L"25%");
    comboAdd(IDC_LEVEL, L"10%");
    SendMessageW(lvl, CB_SETCURSEL, 0, 0);

    addButton(hwnd, IDC_CONVERT, L"开始砸成 1bit", 160, 200, 150, 30);
    addButton(hwnd, IDC_PLAY_ORIG, L"试听原曲", 318, 200, 100, 30);
    addButton(hwnd, IDC_PLAY_BIT, L"试听 1bit", 426, 200, 100, 30);
    addButton(hwnd, IDC_STOP, L"停止播放", 534, 200, 100, 30);
    addButton(hwnd, IDC_OPEN_DIR, L"打开输出目录", 642, 200, 132, 30);

    HWND pb = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
                              S(16), S(244), S(758), S(18), hwnd, (HMENU)(INT_PTR)IDC_PROGRESS, nullptr, nullptr);
    SendMessageW(pb, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));

    HWND st = CreateWindowExW(0, L"STATIC", L"就绪：选一个音频文件，然后把它砸成 1bit。",
                              WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
                              S(16), S(268), S(758), S(20), hwnd, (HMENU)(INT_PTR)IDC_STATUS, nullptr, nullptr);
    SendMessageW(st, WM_SETFONT, (WPARAM)g_font, TRUE);

    addLabel(hwnd, L"支持格式: mp3 / flac / wav / ogg / opus / m4a(aac) / wma / alac / webm ... (拖放文件即可)",
             16, 292, 700, 18);
    addLabel(hwnd, L"1bit = 每个采样只有 0 或 1 → 方波，噪声明亮、动态全灭，这就是全损音质。",
             16, 310, 700, 18);
}

void layoutWave(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    g_waveRect.left = S(16);
    g_waveRect.top = S(334);
    g_waveRect.right = rc.right - S(16);
    g_waveRect.bottom = rc.bottom - S(16);
}

void pickInputFile() {
    wchar_t file[MAX_PATH * 2] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"音频文件\0*.mp3;*.flac;*.wav;*.ogg;*.oga;*.opus;*.m4a;*.aac;*.wma;*.alac;*.webm;*.mkv;*.aiff;*.aif\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"选择要砸成 1bit 的音频文件";
    if (!GetOpenFileNameW(&ofn)) return;
    setText(IDC_INPUT, file);
    setText(IDC_OUTPUT, defaultOutputPath(file, (OutFormat)comboIndex(IDC_FORMAT)));
    setStatus(L"已选择: " + baseName(file));
    InvalidateRect(g_hwnd, &g_waveRect, FALSE);
}

void pickOutputFile() {
    wchar_t file[MAX_PATH * 2] = {0};
    std::wstring cur = getText(IDC_OUTPUT);
    if (!cur.empty()) wcsncpy(file, cur.c_str(), MAX_PATH * 2 - 1);
    OutFormat f = (OutFormat)comboIndex(IDC_FORMAT);
    const wchar_t* filter = L"WAV 音频\0*.wav\0所有文件\0*.*\0";
    if (f == OutFormat::Raw1Bit) filter = L"1bit 位流\0*.bit\0所有文件\0*.*\0";
    else if (f == OutFormat::Dsf) filter = L"DSD 音频\0*.dsf\0所有文件\0*.*\0";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    std::wstring defExt = outFormatExtension(f);
    if (!defExt.empty()) defExt = defExt.substr(1);
    ofn.lpstrDefExt = defExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"选择输出文件";
    if (!GetSaveFileNameW(&ofn)) return;
    setText(IDC_OUTPUT, file);
}

void openOutputDir() {
    std::wstring out = getText(IDC_OUTPUT);
    if (out.empty()) return;
    std::wstring dir = dirName(out);
    if (dir.empty()) return;
    ShellExecuteW(g_hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = hwnd;
            {
                // Per-monitor DPI: the screen DC reports the system DPI while the
                // window DC reports the real one, so the window is resized here to
                // exactly fit the scaled layout.
                HDC hdc = GetDC(hwnd);
                g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
                ReleaseDC(hwnd, hdc);
                if (g_dpi <= 0) g_dpi = 96;

                // Remember the size the scaled layout needs; applied after
                // CreateWindowEx returns (it would otherwise override us).
                RECT rc = {0, 0, S(790), S(552)};
                AdjustWindowRect(&rc, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE);
                g_wantW = rc.right - rc.left;
                g_wantH = rc.bottom - rc.top;
            }
            g_font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            g_fontBold = g_font;
            buildUi(hwnd);
            layoutWave(hwnd);
            DragAcceptFiles(hwnd, TRUE);
            if (!g_initialFile.empty()) {
                setText(IDC_INPUT, g_initialFile);
                setText(IDC_OUTPUT, defaultOutputPath(g_initialFile, (OutFormat)comboIndex(IDC_FORMAT)));
                setStatus(L"已选择: " + baseName(g_initialFile));
            }
            if (g_autoStart) SetTimer(hwnd, 1, 400, nullptr);
            return 0;

        case WM_SIZE:
            layoutWave(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            drawWave(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t file[MAX_PATH * 2] = {0};
            if (DragQueryFileW(drop, 0, file, MAX_PATH * 2)) {
                setText(IDC_INPUT, file);
                setText(IDC_OUTPUT, defaultOutputPath(file, (OutFormat)comboIndex(IDC_FORMAT)));
                setStatus(L"已选择: " + baseName(file));
            }
            DragFinish(drop);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_BROWSE_IN) pickInputFile();
            else if (id == IDC_BROWSE_OUT) pickOutputFile();
            else if (id == IDC_CONVERT) startConvert();
            else if (id == IDC_PLAY_ORIG) {
                if (g_previewOrig.empty()) {
                    if (g_result.original.empty()) setStatus(L"还没有可试听的内容");
                    else { g_previewOrig = buildPreviewWav(g_result.original); playWavMemory(g_previewOrig); }
                } else playWavMemory(g_previewOrig);
            } else if (id == IDC_PLAY_BIT) {
                if (g_previewBits.empty() && !g_result.bits.bits.empty()) g_previewBits = buildPreviewWavFromBits(g_result.bits);
                if (g_previewBits.empty()) setStatus(L"还没有生成 1bit 结果");
                else playWavMemory(g_previewBits);
            } else if (id == IDC_STOP) {
                stopSound();
            } else if (id == IDC_OPEN_DIR) {
                openOutputDir();
            } else if (id == IDC_FORMAT && code == CBN_SELCHANGE) {
                std::wstring in = getText(IDC_INPUT);
                if (!in.empty()) setText(IDC_OUTPUT, defaultOutputPath(in, (OutFormat)comboIndex(IDC_FORMAT)));
            }
            return 0;
        }

        case WM_APP_PROGRESS: {
            ProgressMsg* m = (ProgressMsg*)lp;
            if (m) {
                SendMessageW(ctl(IDC_PROGRESS), PBM_SETPOS, (WPARAM)(int)(m->frac * 1000), 0);
                setStatus(m->text);
                delete m;
            }
            return 0;
        }

        case WM_APP_DONE:
            onDone();
            return 0;

        case WM_TIMER:
            if (wp == 1) {
                KillTimer(hwnd, 1);
                startConvert();
            }
            return 0;

        case WM_CLOSE:
            g_cancel.store(true);
            stopSound();
            if (g_worker.joinable()) g_worker.join();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int runGui(const std::wstring& initialFile, bool autoStart) {
    g_initialFile = initialFile;
    g_autoStart = autoStart;
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(246, 247, 250));
    wc.lpszClassName = L"OneBitConverterWindow";
    wc.hIcon = LoadIconW(inst, L"APPICON");
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // Sized properly in WM_CREATE once the real monitor DPI is known.
    HWND hwnd = CreateWindowExW(0, L"OneBitConverterWindow", L"1bit 全损音质转换器 v1.0",
                                (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 820, 600, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    if (g_wantW > 0 && g_wantH > 0) {
        int x = (GetSystemMetrics(SM_CXSCREEN) - g_wantW) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - g_wantH) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        SetWindowPos(hwnd, nullptr, x, y, g_wantW, g_wantH, SWP_NOZORDER | SWP_NOACTIVATE);
        g_waveRect.left = 0; // force relayout by faking a size message
        SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(g_wantW, g_wantH));
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_worker.joinable()) g_worker.join();
    return 0;
}

void guiMessage(const std::wstring& text, const std::wstring& caption, bool isError) {
    MessageBoxW(nullptr, text.c_str(), caption.c_str(), isError ? MB_ICONERROR : MB_ICONINFORMATION);
}
