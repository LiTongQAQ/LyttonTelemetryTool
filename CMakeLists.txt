cmake_minimum_required(VERSION 3.21)
project(F1_25_Mods4Lytton VERSION 0.2.6 LANGUAGES CXX)

# 软件版本（apps/README.md 里记录每个版本的变化）
# 预发布后缀（如 -beta）：CMake 的版本号只能是数字，后缀单独写；正式版留空
set(F1RACE_VERSION_SUFFIX "")
set(F1RACE_VERSION ${PROJECT_VERSION}${F1RACE_VERSION_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- telemetry-core: header-only F1 25 UDP packet layouts ----
add_library(f1tel INTERFACE)
target_include_directories(f1tel INTERFACE apps/telemetry-core/include)

# ---- hub: receive, forward, record, replay ----
add_library(f1hub_lib STATIC apps/dash-app/hub/hub.cpp apps/dash-app/hub/replay.cpp)
target_include_directories(f1hub_lib PUBLIC apps/dash-app/hub)
target_link_libraries(f1hub_lib PUBLIC f1tel ws2_32)

add_executable(f1hub apps/dash-app/hub/main.cpp)
target_link_libraries(f1hub PRIVATE f1hub_lib)

# ---- tests ----
enable_testing()
add_executable(test_f1tel apps/telemetry-core/tests/test_parse.cpp)
target_link_libraries(test_f1tel PRIVATE f1tel)
target_compile_definitions(test_f1tel PRIVATE F1TEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/apps/telemetry-core/tests/data")
add_test(NAME f1tel COMMAND test_f1tel)

add_executable(test_hub apps/dash-app/hub/test_hub.cpp)
target_link_libraries(test_hub PRIVATE f1hub_lib)
target_compile_definitions(test_hub PRIVATE F1TEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/apps/telemetry-core/tests/data")
add_test(NAME hub COMMAND test_hub)

# ---- race state: standings, sectors, inputs ----
add_library(timing STATIC apps/dash-app/timing/race_state.cpp apps/dash-app/timing/trace.cpp)
target_include_directories(timing PUBLIC apps/dash-app/timing)
target_link_libraries(timing PUBLIC f1tel)

add_executable(test_timing apps/dash-app/timing/test_race_state.cpp)
target_link_libraries(test_timing PRIVATE timing)
target_compile_definitions(test_timing PRIVATE F1TEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/apps/telemetry-core/tests/data")
add_test(NAME timing COMMAND test_timing)

# ---- circuit outlines (bundled data, scripts/gen_tracks.py) ----
add_library(tracks STATIC apps/dash-app/tracks/tracks.cpp)
target_include_directories(tracks PUBLIC apps/dash-app/tracks)
# track calibration from recordings (needs the replay reader)
add_library(trackcal STATIC apps/dash-app/tracks/calib.cpp)
target_link_libraries(trackcal PUBLIC tracks f1tel f1hub_lib)
add_executable(test_trackcal apps/dash-app/tracks/test_calib.cpp)
target_link_libraries(test_trackcal PRIVATE trackcal)
add_test(NAME trackcal COMMAND test_trackcal)

# ---- virtual telemetry ----
add_library(f1sim STATIC apps/dash-app/sim/sim.cpp)
target_include_directories(f1sim PUBLIC apps/dash-app/sim)
target_link_libraries(f1sim PUBLIC f1tel tracks)

add_executable(test_sim apps/dash-app/sim/test_sim.cpp)
target_link_libraries(test_sim PRIVATE f1sim f1hub_lib timing tracks)
add_test(NAME sim COMMAND test_sim)

# ---- third party ----
set(IMGUI third_party/imgui)
add_library(imgui STATIC ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp
  ${IMGUI}/imgui_widgets.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp ${IMGUI}/backends/imgui_impl_win32.cpp
  apps/dash-app/race/imgui_tls.cpp)
target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends apps/dash-app/race)
# the current ImGui context per thread (HMD panels render on their own thread in the Qt program)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imgui_tls.hpp")
target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi dwmapi imm32 gdi32)

# ---- race core: HMD panels, SteamVR, layout, sources / settings (shared by both desktop programs) ----
add_library(race_core STATIC apps/dash-app/race/panels.cpp apps/dash-app/race/carwire.cpp apps/dash-app/race/vr_overlay.cpp
  apps/dash-app/race/layout.cpp apps/dash-app/race/app.cpp apps/dash-app/race/compare.cpp apps/dash-app/race/log.cpp
  apps/dash-app/race/launcher.cpp apps/dash-app/race/wheelinput.cpp)
target_include_directories(race_core PUBLIC apps/dash-app/race third_party/openvr/headers)
target_link_libraries(race_core PUBLIC f1hub_lib timing f1sim tracks imgui shell32 comdlg32 dinput8 dxguid)
target_compile_definitions(race_core PUBLIC F1RACE_VERSION="${F1RACE_VERSION}")

# ---- f1-race: the ImGui desktop window (being replaced by the Qt one below) ----
add_executable(f1-race WIN32 apps/dash-app/race/main.cpp apps/dash-app/race/ui.cpp apps/dash-app/race/f1-race.rc)
target_link_libraries(f1-race PRIVATE race_core)
target_link_options(f1-race PRIVATE -municode)
add_custom_command(TARGET f1-race POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll $<TARGET_FILE_DIR:f1-race>)

# ---- lytton: the Qt desktop window (QML in apps/dash-qt) ----
# Needs Qt 6 built with the same compiler (Qt's MinGW kit uses GCC 13.1: configure a separate build directory with
# -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 and Qt's g++, see CLAUDE.md).
find_package(Qt6 6.5 QUIET COMPONENTS Quick QuickControls2 ShaderTools Svg)
if(Qt6_FOUND)
  set(QTUI apps/dash-qt)
  qt_add_executable(lytton WIN32 ${QTUI}/src/main.cpp ${QTUI}/src/engine.cpp ${QTUI}/src/backend.cpp
    ${QTUI}/src/trackmap.cpp ${QTUI}/src/imcanvas.cpp ${QTUI}/src/carsvg.cpp apps/dash-app/race/ui.cpp apps/dash-app/race/f1-race.rc)
  # everything the program needs ends up in build/app (exe, Qt DLLs and QML modules, openvr_api.dll): the installer
  # packs that folder as it is
  set_target_properties(lytton PROPERTIES AUTOMOC ON OUTPUT_NAME LyttonTelemetryTool
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/app)
  target_link_libraries(lytton PRIVATE race_core Qt6::Quick Qt6::QuickControls2 Qt6::Svg winmm)
  qt_add_shaders(lytton "imshaders" BATCHABLE PREFIX "/shaders" BASE ${CMAKE_CURRENT_SOURCE_DIR}/${QTUI}/src
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/${QTUI}/src/imcanvas.vert ${CMAKE_CURRENT_SOURCE_DIR}/${QTUI}/src/imcanvas.frag)
  set(QTUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${QTUI})
  file(GLOB_RECURSE QTUI_QML CONFIGURE_DEPENDS ${QTUI_DIR}/content/*.qml ${QTUI_DIR}/imports/*.qml ${QTUI_DIR}/imports/*qmldir)
  # imports/F1Data and imports/F1Native are Design Studio stand-ins for the C++ types: not in the program
  list(FILTER QTUI_QML EXCLUDE REGEX "/imports/(F1Data|F1Native)/")
  qt_add_resources(lytton "qml" PREFIX "/" BASE ${QTUI_DIR} FILES ${QTUI_QML})
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/logo-512.png PROPERTIES QT_RESOURCE_ALIAS assets/logo.png)
  qt_add_resources(lytton "brand" PREFIX "/" FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/logo-512.png)
  # Track N Race's car drawing (GPL-3.0), rendered by QtSvg for the car card and the HMD car panel
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/third_party/track-n-race/car/f1-car-wireframe.svg
    PROPERTIES QT_RESOURCE_ALIAS car/f1-car-wireframe.svg)
  qt_add_resources(lytton "car" PREFIX "/" FILES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/track-n-race/car/f1-car-wireframe.svg)
  add_custom_command(TARGET lytton POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll $<TARGET_FILE_DIR:lytton>)
  # Qt's DLLs and QML modules next to the exe, so it runs without Qt on the PATH
  add_custom_command(TARGET lytton POST_BUILD COMMAND ${WINDEPLOYQT_EXECUTABLE} --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/${QTUI}
    --no-translations --no-system-d3d-compiler --no-opengl-sw $<TARGET_FILE:lytton> > ${CMAKE_BINARY_DIR}/windeployqt.log)
endif()

add_executable(test_layout apps/dash-app/race/test_layout.cpp apps/dash-app/race/layout.cpp)
target_include_directories(test_layout PRIVATE apps/dash-app/race)
target_link_libraries(test_layout PRIVATE timing)
add_test(NAME layout COMMAND test_layout)

foreach(t f1hub_lib timing f1sim tracks trackcal race_core f1-race)
  target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()
