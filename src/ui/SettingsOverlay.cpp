#include "SettingsOverlay.h"

#include "../gpu/D3DContext.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ui {

namespace {

constexpr wchar_t kClassName[] = L"ChimeraSettingsWindow";

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace

LRESULT CALLBACK SettingsOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsOverlay::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (imguiReady_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) resizePending_ = true;
        return 0;
    case WM_CLOSE: // hide, don't destroy — panel is toggled via hotkey
        Hide();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool SettingsOverlay::Init(HINSTANCE instance, gpu::D3DContext& d3d, app::Config* config) {
    d3d_ = &d3d;
    config_ = config;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Chimera Overlay — Settings",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 480,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    // Keep the settings panel out of monitor capture (it floats over the
    // game and would otherwise echo into the enhanced image).
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);

    swapchain_ = d3d_->CreateSwapChainForWindow(hwnd_, 520, 480);
    EnsureRenderTarget();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(hwnd_)) return false;
    if (!ImGui_ImplDX11_Init(d3d_->Device(), d3d_->Context())) return false;
    imguiReady_ = true;
    return true;
}

void SettingsOverlay::Shutdown() {
    if (imguiReady_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    rtv_ = nullptr;
    swapchain_ = nullptr;
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void SettingsOverlay::EnsureRenderTarget() {
    if (resizePending_ && swapchain_) {
        rtv_ = nullptr;
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        swapchain_->ResizeBuffers(0, static_cast<UINT>(rc.right - rc.left),
                                  static_cast<UINT>(rc.bottom - rc.top), DXGI_FORMAT_UNKNOWN, 0);
        resizePending_ = false;
    }
    if (!rtv_ && swapchain_) {
        winrt::com_ptr<ID3D11Texture2D> backbuffer;
        swapchain_->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backbuffer.put_void());
        d3d_->Device()->CreateRenderTargetView(backbuffer.get(), nullptr, rtv_.put());
    }
}

void SettingsOverlay::Toggle() {
    visible_ = !visible_;
    if (visible_) {
        pickerWindows_ = capture::WindowFinder::List();
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void SettingsOverlay::Hide() {
    visible_ = false;
    ShowWindow(hwnd_, SW_HIDE);
}

void SettingsOverlay::Render(const Stats& stats) {
    if (!visible_ || !imguiReady_) return;
    EnsureRenderTarget();
    if (!rtv_) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("chimera", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    bool changed = false;

    ImGui::TextUnformatted("Chimera Overlay");
    ImGui::Separator();

    // --- Status ---
    if (stats.captureActive) {
        ImGui::Text("Capturing: %s", Narrow(stats.targetTitle).c_str());
        ImGui::Text("Input %ux%u  ->  Output %ux%u", stats.inW, stats.inH, stats.outW, stats.outH);
        ImGui::Text("%.1f fps presented   |   %.1f fps captured   |   %.2f ms CPU", stats.fps,
                    stats.captureFps, stats.processMs);
    } else {
        ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
                           "No capture active — waiting for target window");
    }
    if (stats.bypassed) {
        ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "BYPASSED (Alt+Shift+B)");
    }
    ImGui::Separator();

    // --- Processing controls ---
    changed |= ImGui::Checkbox("Enabled", &config_->enabled);
    changed |= ImGui::Checkbox("Fit presenter to monitor (upscale to full screen)",
                               &config_->fitToMonitor);

    if (ImGui::CollapsingHeader("Sharpness & Anti-aliasing", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Sharpness (stops, 0 = max)", &config_->sharpness,
                                      0.0f, 2.0f);
        changed |= ImGui::Checkbox("FXAA (smooth jagged edges)", &config_->fxaa);
        if (config_->fxaa) {
            changed |= ImGui::SliderFloat("Text protection (keep UI text crisp)",
                                          &config_->fxaaTextProtect, 0.0f, 1.0f);
        }
        changed |= ImGui::SliderFloat("Deband (smooth sky/fog gradients)",
                                      &config_->debandStrength, 0.0f, 3.0f);
        changed |= ImGui::SliderFloat("Clarity (texture depth)", &config_->clarity, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Bloom (light glow)", &config_->bloom, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Bloom threshold (lower = more glows)",
                                      &config_->bloomThreshold, 0.4f, 0.95f);
    }

    if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Vibrance", &config_->vibrance, -1.0f, 1.0f);
        changed |= ImGui::SliderFloat("Saturation", &config_->saturation, 0.0f, 2.0f);
        changed |= ImGui::SliderFloat("Contrast", &config_->contrast, 0.5f, 1.5f);
        changed |= ImGui::SliderFloat("Gamma", &config_->gamma, 0.5f, 2.0f);
        changed |= ImGui::SliderFloat("Exposure (stops)", &config_->exposure, -1.0f, 1.0f);
        changed |= ImGui::SliderFloat("Filmic tone", &config_->filmic, 0.0f, 1.0f);
        if (ImGui::Button("Reset colors")) {
            config_->vibrance = 0.0f;
            config_->saturation = 1.0f;
            config_->contrast = 1.0f;
            config_->gamma = 1.0f;
            config_->exposure = 0.0f;
            config_->filmic = 0.0f;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Cinematic extras")) {
        changed |= ImGui::SliderFloat("Vignette", &config_->vignette, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Film grain", &config_->grain, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Debug")) {
        changed |= ImGui::Checkbox("Passthrough (bilinear only, no enhancement)",
                                   &config_->passthrough);
    }
    ImGui::Separator();

    // --- Target picker ---
    ImGui::TextUnformatted("Target window");
    if (ImGui::Button("Refresh list")) {
        pickerWindows_ = capture::WindowFinder::List();
    }
    if (ImGui::BeginListBox("##windows", ImVec2(-FLT_MIN, 8 * ImGui::GetTextLineHeightWithSpacing()))) {
        for (const auto& info : pickerWindows_) {
            std::string label = Narrow(info.title) + "  (" + Narrow(info.exeName) + ")";
            if (ImGui::Selectable(label.c_str(), false)) {
                if (onTargetSelected) onTargetSelected(info.hwnd);
            }
        }
        ImGui::EndListBox();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Alt+Shift+O settings   Alt+Shift+B bypass");

    ImGui::End();

    ImGui::Render();
    const float clear[4] = {0.06f, 0.06f, 0.08f, 1.0f};
    ID3D11RenderTargetView* rtvs[] = {rtv_.get()};
    d3d_->Context()->OMSetRenderTargets(1, rtvs, nullptr);
    d3d_->Context()->ClearRenderTargetView(rtv_.get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    // No vsync wait: this render holds the GPU lock the frame worker needs,
    // and blocking here until vblank throttles game-frame presentation.
    // The ~60Hz UI timer paces the panel by itself.
    swapchain_->Present(0, 0);

    ID3D11RenderTargetView* nullRTV[] = {nullptr};
    d3d_->Context()->OMSetRenderTargets(1, nullRTV, nullptr);

    if (changed && onConfigChanged) onConfigChanged();
}

} // namespace ui
