cmake_minimum_required(VERSION 3.20)
project(Fimgui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_executable(Fimgui WIN32
    src/main.cpp
    resources/fimgui.rc
    src/app.cpp
    src/ui_canvas.cpp
    src/ui_panels.cpp
    src/ui_ai.cpp
    src/ui_motion.cpp
    src/mcp_server.cpp
    src/mcp_tools.cpp
    src/widgets.cpp
    src/icons.cpp
    src/document.cpp
    src/renderer.cpp
    src/fonts.cpp
    src/gfx.cpp
    src/platform.cpp
    src/exporters.cpp
    src/codegen.cpp
    src/http.cpp
    src/ai.cpp
    runtime/fimgui_runtime.cpp
    runtime/fimgui_fx_dx11.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(Fimgui PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends runtime src)
target_compile_definitions(Fimgui PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Fimgui PRIVATE d3d11 dxgi d3dcompiler winhttp crypt32 shell32 ole32 shlwapi mfplat mfreadwrite mfuuid ws2_32)

if(MSVC)
    target_compile_options(Fimgui PRIVATE /utf-8 /bigobj /W3 /MP)
    set_property(TARGET Fimgui PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Fimgui>)
endif()

# stdio <-> HTTP bridge for MCP clients that launch servers as processes (e.g. Claude Desktop).
add_executable(fimgui-mcp tools/fimgui_mcp_bridge.cpp resources/fimgui.rc)
target_compile_definitions(fimgui-mcp PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(fimgui-mcp PRIVATE winhttp shell32)
set_target_properties(fimgui-mcp PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug)
add_dependencies(Fimgui fimgui-mcp)

# Runtime sources are copied next to the exe: the exporter writes them into every generated project.
add_custom_command(TARGET Fimgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Fimgui>/runtime
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/fimgui_runtime.h
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/fimgui_runtime.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/fimgui_fx.h
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/fimgui_fx_dx11.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/fimgui_fx_null.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image.h
        $<TARGET_FILE_DIR:Fimgui>/runtime
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE $<TARGET_FILE_DIR:Fimgui>/runtime/FIMGUI_LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources $<TARGET_FILE_DIR:Fimgui>/resources
)
