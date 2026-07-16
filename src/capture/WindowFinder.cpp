#include "WindowFinder.h"

#include <dwmapi.h>
#include <algorithm>
#include <cwctype>

namespace capture {

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool IsCloaked(HWND hwnd) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != 0;
    }
    return false;
}

std::wstring ExeNameForWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return L"";

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"";

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        std::wstring full(path, size);
        size_t slash = full.find_last_of(L'\\');
        result = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
    }
    CloseHandle(process);
    return result;
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<WindowInfo>*>(lparam);

    if (!IsWindowVisible(hwnd) || IsCloaked(hwnd)) return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    wchar_t title[512]{};
    if (GetWindowTextW(hwnd, title, 512) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;

    out->push_back(WindowInfo{hwnd, title, ExeNameForWindow(hwnd)});
    return TRUE;
}

} // namespace

std::vector<WindowInfo> WindowFinder::List() {
    std::vector<WindowInfo> result;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&result));
    return result;
}

WindowInfo WindowFinder::FindByTitleOrExe(const std::wstring& needle) {
    const std::wstring lowered = ToLower(needle);
    for (const auto& info : List()) {
        if (ToLower(info.title).find(lowered) != std::wstring::npos ||
            ToLower(info.exeName).find(lowered) != std::wstring::npos) {
            return info;
        }
    }
    return {};
}

} // namespace capture
