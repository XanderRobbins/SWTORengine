#include "Config.h"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace app {

namespace {

std::wstring ConfigPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos) dir.resize(slash + 1);
    return dir + L"config.json";
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

} // namespace

Config Config::Load() {
    Config config;
    std::ifstream file(ConfigPath());
    if (!file) return config;

    try {
        nlohmann::json j = nlohmann::json::parse(file);
        config.targetTitle = Widen(j.value("targetTitle", Narrow(config.targetTitle)));
        config.sharpness = j.value("sharpness", config.sharpness);
        config.enabled = j.value("enabled", config.enabled);
        config.passthrough = j.value("passthrough", config.passthrough);
        config.fitToMonitor = j.value("fitToMonitor", config.fitToMonitor);
        config.fxaa = j.value("fxaa", config.fxaa);
        config.fxaaTextProtect = j.value("fxaaTextProtect", config.fxaaTextProtect);
        config.debandStrength = j.value("debandStrength", config.debandStrength);
        config.clarity = j.value("clarity", config.clarity);
        config.bloom = j.value("bloom", config.bloom);
        config.bloomThreshold = j.value("bloomThreshold", config.bloomThreshold);
        config.vibrance = j.value("vibrance", config.vibrance);
        config.saturation = j.value("saturation", config.saturation);
        config.contrast = j.value("contrast", config.contrast);
        config.gamma = j.value("gamma", config.gamma);
        config.exposure = j.value("exposure", config.exposure);
        config.filmic = j.value("filmic", config.filmic);
        config.vignette = j.value("vignette", config.vignette);
        config.grain = j.value("grain", config.grain);
    } catch (...) {
        // malformed config — fall back to defaults
    }
    return config;
}

void Config::Save() const {
    nlohmann::json j{
        {"targetTitle", Narrow(targetTitle)},
        {"sharpness", sharpness},
        {"enabled", enabled},
        {"passthrough", passthrough},
        {"fitToMonitor", fitToMonitor},
        {"fxaa", fxaa},
        {"fxaaTextProtect", fxaaTextProtect},
        {"debandStrength", debandStrength},
        {"clarity", clarity},
        {"bloom", bloom},
        {"bloomThreshold", bloomThreshold},
        {"vibrance", vibrance},
        {"saturation", saturation},
        {"contrast", contrast},
        {"gamma", gamma},
        {"exposure", exposure},
        {"filmic", filmic},
        {"vignette", vignette},
        {"grain", grain},
    };
    std::ofstream file(ConfigPath());
    if (file) file << j.dump(2);
}

} // namespace app
