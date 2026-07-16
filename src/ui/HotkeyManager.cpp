#include "HotkeyManager.h"

namespace ui {

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

bool HotkeyManager::Register(HWND owner, int id, UINT modifiers, UINT vk,
                             std::function<void()> action) {
    if (!RegisterHotKey(owner, id, modifiers | MOD_NOREPEAT, vk)) return false;
    owner_ = owner;
    actions_[id] = std::move(action);
    return true;
}

void HotkeyManager::UnregisterAll() {
    for (const auto& [id, action] : actions_) {
        UnregisterHotKey(owner_, id);
    }
    actions_.clear();
}

bool HotkeyManager::Dispatch(int id) {
    auto it = actions_.find(id);
    if (it == actions_.end()) return false;
    it->second();
    return true;
}

} // namespace ui
