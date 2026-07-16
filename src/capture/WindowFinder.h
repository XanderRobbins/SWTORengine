#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace capture {

struct WindowInfo {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring exeName; // e.g. "swtor.exe"
};

class WindowFinder {
public:
    // All visible, titled, non-cloaked top-level windows (excluding our own process).
    static std::vector<WindowInfo> List();

    // Case-insensitive substring match against window title or process exe name.
    // Returns nullptr HWND if not found.
    static WindowInfo FindByTitleOrExe(const std::wstring& needle);
};

} // namespace capture
