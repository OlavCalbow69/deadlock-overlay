cmake_minimum_required(VERSION 3.24)
project(DeadlockOverlay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
file(GLOB_RECURSE SOLACE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER SOLACE_SOURCES EXCLUDE REGEX "/(main.cpp|runtime/desktop_app.cpp|overlay/.*|driver/.*)$")
add_library(solace STATIC ${SOLACE_SOURCES}
  thirdparty/imgui/imgui.cpp thirdparty/imgui/imgui_draw.cpp
  thirdparty/imgui/imgui_tables.cpp thirdparty/imgui/imgui_widgets.cpp
  thirdparty/imgui/backends/imgui_impl_dx11.cpp
  thirdparty/imgui/backends/imgui_impl_win32.cpp
  thirdparty/imgui/misc/freetype/imgui_freetype.cpp)
target_include_directories(solace PUBLIC src thirdparty/imgui thirdparty/imgui/backends
  thirdparty/imgui/misc/freetype thirdparty/freetype/include)
target_compile_definitions(solace PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  IMGUI_DEFINE_MATH_OPERATORS IMGUI_ENABLE_FREETYPE)
target_compile_options(solace PRIVATE /utf-8 /bigobj /MP)
target_link_libraries(solace PUBLIC d3d11 d3dcompiler dxgi dcomp dwmapi windowscodecs
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/freetype/win64/freetype.lib")
add_executable(DeadlockOverlay src/overlay/main.cpp src/overlay/game_reader.cpp)
target_link_libraries(DeadlockOverlay PRIVATE solace bcrypt shell32 ole32 advapi32 psapi setupapi)
target_compile_options(DeadlockOverlay PRIVATE /utf-8 /W4 /MP)
target_link_options(DeadlockOverlay PRIVATE /ENTRY:mainCRTStartup
  "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/resources/app.manifest")
enable_testing()
add_test(NAME overlay_checks COMMAND DeadlockOverlay --self-test)
add_test(NAME renderer_checks COMMAND DeadlockOverlay --render-test)
add_test(NAME console_shutdown_checks COMMAND DeadlockOverlay --shutdown-test)
add_test(NAME resource_checks COMMAND DeadlockOverlay --resource-test)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/Release/tools/data-update/DeadlockDataUpdate.exe")
  add_test(NAME data_update_checks COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build/Release/tools/data-update/DeadlockDataUpdate.exe" --self-test)
endif()

