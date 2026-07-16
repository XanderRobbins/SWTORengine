#pragma once

#include <windows.h>
#include <functional>
#include <unordered_map>

namespace ui {

// Global hotkeys via RegisterHotKey. Owner window receives WM_HOTKEY;
// route it to Dispatch().
class HotkeyManager {
public:
    ~HotkeyManager();

    bool Register(HWND owner, int id, UINT modifiers, UINT vk, std::function<void()> action);
    void UnregisterAll();

    // Call from the owner's WM_HOTKEY handler. Returns true if handled.
    bool Dispatch(int id);

private:
    HWND owner_ = nullptr;
    std::unordered_map<int, std::function<void()>> actions_;
};

} // namespace ui
