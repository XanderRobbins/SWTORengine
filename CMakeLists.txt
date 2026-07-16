cmake_minimum_required(VERSION 3.21)

project(chimera-overlay
    VERSION 0.1.0
    DESCRIPTION "Non-invasive real-time game graphics enhancement overlay"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "chimera-overlay targets Windows only (Windows.Graphics.Capture / D3D11 / Win32)")
endif()

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(chimera WIN32
    src/main.cpp
    src/app/App.cpp
    src/app/Config.cpp
    src/capture/WindowFinder.cpp
    src/capture/CaptureSession.cpp
    src/gpu/D3DContext.cpp
    src/gpu/ShaderPipeline.cpp
    src/presenter/PresenterWindow.cpp
    src/presenter/WindowTracker.cpp
    src/ui/SettingsOverlay.cpp
    src/ui/HotkeyManager.cpp
    src/util/PngDump.cpp
)

target_compile_definitions(chimera PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    # /bigobj: C++/WinRT headers generate many sections.
    target_compile_options(chimera PRIVATE /W4 /permissive- /bigobj /await)
endif()

target_link_libraries(chimera PRIVATE
    imgui::imgui
    nlohmann_json::nlohmann_json
    user32
    gdi32
    shell32
    ole32
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    windowscodecs
    windowsapp
)

# Runtime-compiled shaders (incl. AMD FSR headers) ship next to the exe.
add_custom_command(TARGET chimera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/shaders
        $<TARGET_FILE_DIR:chimera>/shaders
)
