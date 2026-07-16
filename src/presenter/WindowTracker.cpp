#include "WindowTracker.h"

#include <dwmapi.h>

namespace presenter {

WindowTracker* WindowTracker::instance_ = nullptr;

WindowTracker::~WindowTracker() {
    Detach();
}

bool WindowTracker::Attach(HWND target, Callbacks callbacks) {
    Detach();
    if (!IsWindow(target)) return false;

    target_ = target;
    callbacks_ = std::move(callbacks);
    instance_ = this;

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);

    locationHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                    nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);
    destroyHook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
                                   nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);
    minimizeHook_ = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
                                    nullptr, WinEventProc, pid, 0, WINEVENT_OUTOFCONTEXT);
    // Foreground changes must be watched globally — any process can steal focus.
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                      nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    return locationHook_ != nullptr;
}

void WindowTracker::Detach() {
    for (HWINEVENTHOOK* hook : {&locationHook_, &destroyHook_, &minimizeHook_, &foregroundHook_}) {
        if (*hook) {
            UnhookWinEvent(*hook);
            *hook = nullptr;
        }
    }
    target_ = nullptr;
    callbacks_ = {};
    if (instance_ == this) instance_ = nullptr;
}

RECT WindowTracker::GetRect() const {
    RECT rect{};
    if (!target_) return rect;
    if (FAILED(DwmGetWindowAttribute(target_, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        GetWindowRect(target_, &rect);
    }
    return rect;
}

void CALLBACK WindowTracker::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                          LONG idObject, LONG, DWORD, DWORD) {
    if (instance_) instance_->HandleEvent(event, hwnd, idObject);
}

void WindowTracker::HandleEvent(DWORD event, HWND hwnd, LONG idObject) {
    if (!target_) return;

    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
        if (callbacks_.onForegroundChanged) callbacks_.onForegroundChanged(hwnd);
        break;
    case EVENT_OBJECT_LOCATIONCHANGE:
        if (hwnd == target_ && idObject == OBJID_WINDOW && callbacks_.onRectChanged) {
            callbacks_.onRectChanged();
        }
        break;
    case EVENT_SYSTEM_MINIMIZESTART:
        if (hwnd == target_ && callbacks_.onMinimizedChanged) callbacks_.onMinimizedChanged(true);
        break;
    case EVENT_SYSTEM_MINIMIZEEND:
        if (hwnd == target_ && callbacks_.onMinimizedChanged) callbacks_.onMinimizedChanged(false);
        break;
    case EVENT_OBJECT_DESTROY:
        if (hwnd == target_ && idObject == OBJID_WINDOW) {
            HWND destroyed = target_;
            auto onDestroyed = callbacks_.onDestroyed;
            Detach();
            (void)destroyed;
            if (onDestroyed) onDestroyed();
        }
        break;
    }
}

} // namespace presenter
