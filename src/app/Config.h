#pragma once

#include <string>

namespace app {

struct Config {
    std::wstring targetTitle = L"Star Wars"; // substring match on title or exe
    float sharpness = 0.25f;                 // RCAS stops: 0 = max sharp, 2 = subtle
    bool enabled = true;
    bool passthrough = false;    // debug: bilinear instead of FSR
    bool fitToMonitor = false;   // presenter covers the monitor (true upscale)

    // Loaded from / saved to config.json next to the exe.
    static Config Load();
    void Save() const;
};

} // namespace app
