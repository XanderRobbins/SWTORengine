#include "UiTheme.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace ui {

namespace {

// SWTOR quickslot base size in UI units (scaled by element scale and
// GlobalScale). Calibrated against a live 2560x1600 capture.
constexpr float kCellBase = 52.0f;

std::wstring ProfilesDir() {
    wchar_t path[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) return L"";
    return std::wstring(path) + L"\\SWTOR\\swtor\\settings\\GUIProfiles";
}

// Minimal extraction: value of <name ... Value="..." /> inside a block.
float GetValue(const std::string& block, const std::string& name, float fallback) {
    size_t at = block.find("<" + name);
    if (at == std::string::npos) return fallback;
    size_t v = block.find("Value=\"", at);
    if (v == std::string::npos) return fallback;
    v += 7;
    return static_cast<float>(atof(block.c_str() + v));
}

std::string GetBlock(const std::string& xml, const std::string& element) {
    const std::string open = "<" + element + ">";
    const std::string close = "</" + element + ">";
    size_t a = xml.find(open);
    if (a == std::string::npos) return {};
    size_t b = xml.find(close, a);
    if (b == std::string::npos) return {};
    return xml.substr(a, b - a);
}

long long FileWriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           data.ftLastWriteTime.dwLowDateTime;
}

} // namespace

bool UiTheme::LoadProfile() {
    loaded_ = false;
    bars_.clear();

    // Most recently written profile is the active one (good-enough heuristic).
    const std::wstring dir = ProfilesDir();
    WIN32_FIND_DATAW find{};
    HANDLE h = FindFirstFileW((dir + L"\\*.xml").c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::wstring best;
    long long bestTime = 0;
    do {
        long long t = (static_cast<long long>(find.ftLastWriteTime.dwHighDateTime) << 32) |
                      find.ftLastWriteTime.dwLowDateTime;
        if (t > bestTime) {
            bestTime = t;
            best = dir + L"\\" + find.cFileName;
        }
    } while (FindNextFileW(h, &find));
    FindClose(h);
    if (best.empty()) return false;

    std::ifstream file(best.c_str());
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string xml = buffer.str();

    profilePath_ = best;
    lastWriteTime_ = bestTime;

    const std::string global = GetBlock(xml, "Global");
    globalScale_ = GetValue(global, "GlobalScale", 1.0f);

    for (int i = 1; i <= 6; ++i) {
        const std::string block = GetBlock(xml, "QuickBar" + std::to_string(i));
        if (block.empty()) continue;
        Bar bar;
        bar.xOff = GetValue(block, "anchorXOffset", 0.0f);
        bar.yOff = GetValue(block, "anchorYOffset", 0.0f);
        bar.align = static_cast<int>(GetValue(block, "anchorAlignment", 8.0f));
        bar.scale = GetValue(block, "scale", 1.0f);
        bar.numVisible = static_cast<int>(GetValue(block, "NumVisible", 12.0f));
        bar.numPerRow = static_cast<int>(GetValue(block, "NumPerRow", 12.0f));
        bar.enabled = GetValue(block, "enabled", 0.0f) != 0.0f;
        if (bar.enabled && bar.numVisible > 0) bars_.push_back(bar);
    }

    loaded_ = !bars_.empty();
    return loaded_;
}

bool UiTheme::MaybeReload() {
    if (profilePath_.empty()) return LoadProfile();
    const long long t = FileWriteTime(profilePath_);
    if (t != lastWriteTime_) return LoadProfile();
    return false;
}

std::vector<UiTheme::BarRect> UiTheme::ComputeRects(unsigned gameW, unsigned gameH) const {
    std::vector<BarRect> rects;
    if (!loaded_) return rects;

    const float G = globalScale_;
    for (const Bar& bar : bars_) {
        const float cell = kCellBase * bar.scale * G;
        const int cols = bar.numVisible < bar.numPerRow ? bar.numVisible : bar.numPerRow;
        const int rows = (bar.numVisible + bar.numPerRow - 1) / bar.numPerRow;
        const float w = cols * cell;
        const float h = rows * cell;
        const float ax = bar.xOff * G;
        const float ay = bar.yOff * G;

        BarRect rect{};
        // anchorAlignment: element's own matching point pinned to the screen's
        // matching point, plus the scaled offset. Verified: 7 = top-center,
        // 8 = bottom-center, 2 = bottom-left. Others fall back to centers.
        switch (bar.align) {
        case 7: // top-center
            rect.x = gameW * 0.5f + ax - w * 0.5f;
            rect.y = ay;
            break;
        case 8: // bottom-center
            rect.x = gameW * 0.5f + ax - w * 0.5f;
            rect.y = gameH + ay - h;
            break;
        case 2: // bottom-left
            rect.x = ax;
            rect.y = gameH + ay - h;
            break;
        default:
            continue; // unknown anchor — skip rather than misdraw
        }
        rect.w = w;
        rect.h = h;
        rect.cols = cols;
        rect.rows = rows;
        rect.cellW = cell;
        rect.cellH = cell;
        rects.push_back(rect);
    }
    return rects;
}

} // namespace ui
