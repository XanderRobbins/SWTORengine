#pragma once

#include <windows.h>
#include <functional>

namespace presenter {

// Tracks a target window's screen rect and lifecycle via WinEvent hooks
// (EVENT_OBJECT_LOCATIONCHANGE etc. — cheaper than per-frame polling).
// All callbacks fire on the thread that called Attach (requires a message loop).
class WindowTracker {
public:
    struct Callbacks {
        std::function<void()> onRectChanged;
        std::function<void(bool minimized)> onMinimizedChanged;
        std::function<void()> onDestroyed;
        std::function<void(HWND foreground)> onForegroundChanged;
    };

    ~WindowTracker();

    bool Attach(HWND target, Callbacks callbacks);
    void Detach();

    HWND Target() const { return target_; }
    bool IsAttached() const { return target_ != nullptr; }
    bool IsMinimized() const { return target_ && IsIconic(target_); }

    // Screen rect of the visible window content (DWM extended frame bounds,
    // which excludes the invisible resize-border margin GetWindowRect includes).
    RECT GetRect() const;

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild,
                                      DWORD eventThread, DWORD eventTime);
    void HandleEvent(DWORD event, HWND hwnd, LONG idObject);

    HWND target_ = nullptr;
    Callbacks callbacks_;
    HWINEVENTHOOK locationHook_ = nullptr;
    HWINEVENTHOOK destroyHook_ = nullptr;
    HWINEVENTHOOK minimizeHook_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;

    static WindowTracker* instance_; // single tracker per process (v1)
};

} // namespace presenter
