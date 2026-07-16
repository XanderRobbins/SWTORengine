#pragma once

#include <string>

namespace app {

struct Config {
    std::wstring targetTitle = L"Star Wars"; // substring match on title or exe
    bool enabled = true;
    bool passthrough = false;    // debug: bilinear instead of FSR
    bool fitToMonitor = false;   // presenter covers the monitor (true upscale)

    // Processing (neutral values disable the corresponding shader work)
    float sharpness = 0.25f;      // RCAS stops: 0 = max sharp, 2 = subtle
    bool fxaa = true;              // post-AA before upscale
    float fxaaTextProtect = 0.75f; // 0..1, keep UI text crisp under FXAA
    float debandStrength = 1.0f;   // 0 = off
    float vibrance = 0.15f;       // -1..1
    float saturation = 1.0f;      // 1 = neutral
    float contrast = 1.0f;        // 1 = neutral
    float gamma = 1.0f;           // 1 = neutral
    float exposure = 0.0f;        // stops
    float filmic = 0.0f;          // 0..1 S-curve blend
    float vignette = 0.0f;        // 0..1
    float grain = 0.0f;           // 0..1

    // Loaded from / saved to config.json next to the exe.
    static Config Load();
    void Save() const;
};

} // namespace app
