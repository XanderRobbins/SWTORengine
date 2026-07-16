#pragma once

#include <string>
#include <vector>

namespace ui {

// Reads the target game's UI layout from SWTOR's GUIProfiles XML (a user
// data file the game writes — never the game process) and computes screen
// rects for themed frames drawn around the action bars.
class UiTheme {
public:
    struct BarRect {
        float x, y, w, h;        // native game pixels
        int cols = 0, rows = 0;  // slot grid (0 = no grid, frame only)
        float cellW = 0, cellH = 0;
    };

    // Loads the most recently written profile XML. Returns false if none.
    bool LoadProfile();
    // Re-loads if the profile file changed on disk since the last load.
    bool MaybeReload();
    bool Loaded() const { return loaded_; }

    std::vector<BarRect> ComputeRects(unsigned gameW, unsigned gameH) const;

private:
    struct Bar {
        float xOff = 0, yOff = 0;
        int align = 8;
        float scale = 1.0f;
        int numVisible = 0, numPerRow = 1;
        bool enabled = false;
    };

    std::wstring profilePath_;
    long long lastWriteTime_ = 0;
    float globalScale_ = 1.0f;
    std::vector<Bar> bars_;
    bool loaded_ = false;
};

} // namespace ui
