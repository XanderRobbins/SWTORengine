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

add_executable(chimera WIN32
    src/main.cpp
)

target_compile_definitions(chimera PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# C++/WinRT (used from Phase 2 onward) requires /await-compatible coroutines
# and permissive- conformance mode under MSVC.
if(MSVC)
    target_compile_options(chimera PRIVATE /W4 /permissive-)
endif()

target_link_libraries(chimera PRIVATE
    user32
)
