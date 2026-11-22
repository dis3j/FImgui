cmake_minimum_required(VERSION 3.20)
project(fimgui_example CXX)
set(CMAKE_CXX_STANDARD 17)

# Point this to your Dear ImGui (1.92+) checkout.
set(IMGUI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/imgui" CACHE PATH "Dear ImGui source directory")

add_executable(fimgui_example
    main.cpp
    ../menu/menu.cpp
    ../menu/menu_assets.cpp
    ../menu/fimgui_runtime.cpp
    ../menu/fimgui_fx_dx11.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(fimgui_example PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ../menu)
target_link_libraries(fimgui_example PRIVATE d3d11 dxgi d3dcompiler mfplat mfreadwrite mfuuid shlwapi)
if(MSVC)
    target_compile_options(fimgui_example PRIVATE /utf-8 /bigobj)
endif()
