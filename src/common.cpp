#include "common.h"
#include <algorithm>
#include <shlobj.h>
#include <objbase.h>

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::wstring baseName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring dirName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring stripExt(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return path;
    if (slash != std::wstring::npos && dot < slash) return path;
    return path.substr(0, dot);
}

std::wstring extLower(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return std::wstring();
    std::wstring e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), towlower);
    return e;
}

std::wstring formatSeconds(double sec) {
    if (sec < 0) sec = 0;
    int total = (int)(sec + 0.5);
    wchar_t buf[64];
    swprintf(buf, 64, L"%d:%02d", total / 60, total % 60);
    return buf;
}

std::wstring formatBytes(unsigned long long bytes) {
    wchar_t buf[64];
    double b = (double)bytes;
    if (b >= 1024.0 * 1024.0 * 1024.0) swprintf(buf, 64, L"%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    else if (b >= 1024.0 * 1024.0)      swprintf(buf, 64, L"%.2f MB", b / (1024.0 * 1024.0));
    else if (b >= 1024.0)               swprintf(buf, 64, L"%.1f KB", b / 1024.0);
    else                                swprintf(buf, 64, L"%llu B", bytes);
    return buf;
}

bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring win32ErrorText(unsigned long err) {
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, nullptr);
    std::wstring text;
    if (n && msg) {
        text.assign(msg, n);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    }
    if (msg) LocalFree(msg);
    if (text.empty()) text = L"未知错误";
    wchar_t buf[64];
    swprintf(buf, 64, L" (错误 %lu)", err);
    return text + buf;
}

bool ensureDirectoryExists(const std::wstring& dir, std::wstring& err) {
    if (dir.empty()) return true;
    if (dirExists(dir)) return true;
    // create parents first
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos && pos > 2) {   // keep "C:\" intact
        std::wstring parent = dir.substr(0, pos);
        if (!ensureDirectoryExists(parent, err)) return false;
    }
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    DWORD e = GetLastError();
    if (e == ERROR_ALREADY_EXISTS) return true;
    err = L"无法创建目录 " + dir + L": " + win32ErrorText(e);
    return false;
}

bool isDirectoryWritable(const std::wstring& dir, std::wstring& err) {
    std::wstring probeDir = dir.empty() ? std::wstring(L".") : dir;
    wchar_t name[64];
    swprintf(name, 64, L"__onebit_probe_%lu_%lu.tmp", GetCurrentProcessId(), GetTickCount());
    std::wstring probe = probeDir + L"\\" + name;
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"目录不可写: " + probeDir + L"  " + win32ErrorText(GetLastError());
        return false;
    }
    CloseHandle(h);
    return true;
}

std::wstring tempDirPath() {
    wchar_t buf[MAX_PATH + 1] = {0};
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0) return std::wstring();
    std::wstring s = buf;
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

std::wstring knownFolderPath(int which) {
    KNOWNFOLDERID id = FOLDERID_Music;
    if (which == 1) id = FOLDERID_Desktop;
    else if (which == 2) id = FOLDERID_Documents;
    else if (which == 3) id = FOLDERID_LocalAppData;
    PWSTR p = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) result = p;
    if (p) CoTaskMemFree(p);
    return result;
}
