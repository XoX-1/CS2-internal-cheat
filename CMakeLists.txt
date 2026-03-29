cmake_minimum_required(VERSION 3.20)
project(mindcheat VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Build MinHook as a static lib via its own CMakeLists
set(MINHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/minhook EXCLUDE_FROM_ALL)

# Shared library (DLL)
add_library(mindcheat SHARED
    src/dllmain.cpp
    src/hooks/hooks.cpp
    src/hooks/dx11_hook.cpp
    src/features/esp.cpp
    src/features/aimbot.cpp
    src/features/player_fov.cpp
    src/features/bhop.cpp
    src/features/noflash.cpp
    src/features/nosmoke.cpp
    src/features/killsound.cpp
    src/features/spectator_list.cpp
    src/features/bomb_timer.cpp
    src/features/triggerbot.cpp
    src/features/radar.cpp
    src/features/keybind_manager.cpp
    src/features/inventory_changer.cpp
    src/features/texture_manager.cpp
    src/features/skychanger.cpp
    src/features/third_person.cpp
    src/features/silent_aim.cpp
    src/features/bullet_tracer.cpp
    src/features/config_manager.cpp
    # ImGui source
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_demo.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
)

# Includes
target_include_directories(mindcheat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/../output          # CS2 dumper output — drop new offsets here and rebuild
    ${CMAKE_CURRENT_SOURCE_DIR}/output             # CS2 dumper output (local copy)
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# Definitions
target_compile_definitions(mindcheat PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Link Libraries — minhook target comes from add_subdirectory
target_link_libraries(mindcheat PRIVATE
    minhook
    comdlg32
    d3d11
    dxgi
    winmm
    wininet
)

# =========================================================================
# Injector executable — Manual Map injector with stealth features
# =========================================================================
add_executable(mindcheat_injector
    injector/main.cpp
)

target_compile_definitions(mindcheat_injector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Embed admin manifest for UAC elevation
set_target_properties(mindcheat_injector PROPERTIES
    LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
)

set_target_properties(mindcheat_injector PROPERTIES
    OUTPUT_NAME "mindcheat_injector"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Copy the DLL next to the injector after build
add_custom_command(TARGET mindcheat_injector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:mindcheat>"
        "$<TARGET_FILE_DIR:mindcheat_injector>/mindcheat.dll"
    COMMENT "Copying mindcheat.dll next to injector"
)
