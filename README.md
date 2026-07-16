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

## Smoke test (Phase 0)

```powershell
.\build\Release\chimera.exe --smoke-test
```

Opens an empty window that closes itself after 2 seconds and exits 0.

## Project status

Built phase-by-phase per spec:

- [x] Phase 0 — Environment & scaffolding (minimal Win32 window smoke test)
- [ ] Phase 1 — Window discovery & tracking
- [ ] Phase 2 — Capture module (Windows.Graphics.Capture)
- [ ] Phase 3 — GPU processing pipeline (FSR1 EASU + RCAS)
- [ ] Phase 4 — Click-through presenter window
- [ ] Phase 5 — Main loop, frame pacing & settings UI
- [ ] Phase 6 — Frame interpolation (optional)
- [ ] Phase 7 — Optional AI super-resolution (DirectML)
- [ ] Phase 8 — Packaging & distribution

## License

MIT — see [LICENSE](LICENSE). Bundled third-party components (FSR, Dear ImGui) retain their own MIT licenses; attribution files ship with binary distributions.
