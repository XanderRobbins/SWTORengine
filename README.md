# Chimera Overlay

Non-invasive, real-time game graphics enhancement overlay for Windows 10 (1903+) / Windows 11.

Chimera captures a target game window's pixel output via **Windows.Graphics.Capture** (OS compositor level — never touching the game's process memory, never injecting a DLL, never hooking its rendering), enhances it on the GPU (FSR 1.0 upscale + sharpen), and presents the result in a borderless, click-through, always-on-top window pinned exactly over the game. All input passes straight through to the real game window.

**v1 validation target:** Star Wars: The Old Republic (SWTOR), in **Borderless Windowed** mode. Exclusive fullscreen is fundamentally unsupported.

## Building

Prerequisites (all free):

- Visual Studio 2022 (Community or Build Tools) with the C++ workload
- CMake 3.21+
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set

```powershell
cmake --preset default
cmake --build --preset release
```

Output: `build/Release/chimera.exe`

## Usage

1. Run the target game in **Borderless Windowed** mode (SWTOR: Preferences → Graphics → Window Mode).
2. Launch `chimera.exe`. It finds the target window by the title/exe substring in
   `config.json` (default: `Star Wars`), or pass `--target "<substring>"`.
3. The enhanced image appears in a click-through overlay pinned over the game.
   Play normally — all input goes to the real game window.

**Hotkeys**

| Keys | Action |
|---|---|
| `Alt+Shift+O` | Toggle settings panel (target picker, sharpness, FPS/latency readout) |
| `Alt+Shift+B` | Bypass — instantly hide the overlay (safety valve) |

The overlay auto-hides when the game is minimized or loses foreground, and
re-acquires the game window automatically if the game restarts.

## Test / verification modes

```powershell
.\build\Release\chimera.exe --smoke-test            # open+close cleanly, exit 0
.\build\Release\chimera.exe --test-find "Notepad"   # print live target rect (chimera-test.log)
.\build\Release\chimera.exe --test-capture "Notepad" # dump capture_raw.png + capture_fsr.png (1.5x)
```

## Project status

Built phase-by-phase per spec:

- [x] Phase 0 — Environment & scaffolding (minimal Win32 window smoke test)
- [x] Phase 1 — Window discovery & tracking (WinEvent hooks + DWM frame bounds)
- [x] Phase 2 — Capture module (Windows.Graphics.Capture, zero-copy GPU textures)
- [x] Phase 3 — GPU processing pipeline (FSR1 EASU + RCAS compute, AMD reference HLSL)
- [x] Phase 4 — Click-through presenter window (verified: hit-test passes through to target)
- [x] Phase 5 — Main loop (event-driven via FrameArrived), ImGui settings, hotkeys, config
- [ ] Phase 6 — Frame interpolation (optional)
- [ ] Phase 7 — Optional AI super-resolution (DirectML)
- [ ] Phase 8 — Packaging & distribution

Remaining v1 validation (needs a live SWTOR session): 15+ min play test,
input-lag feel check, multi-monitor DPI drag, 1hr+ memory stability.

## License

MIT — see [LICENSE](LICENSE). Bundled third-party components (FSR, Dear ImGui) retain their own MIT licenses; attribution files ship with binary distributions.
