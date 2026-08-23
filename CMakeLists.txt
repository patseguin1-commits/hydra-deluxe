# Hydra — Windows-only C++ build. Produces:
#   * Hydra.exe                       — the GUI
#   * hydra_batch / hydra_report      — the command line tools
#   * hydra_tests                     — the doctest harness
#   * a zip release via CPack (build_cpp.ps1 -Package)
#
# Third-party code (ImGui, SQLite, miniz, doctest, json, stb) is vendored under
# third_party/ and built here so there is nothing to install but a compiler +
# CMake.
#
# Configure/build (CMake and MSVC come from Visual Studio; see build_cpp.ps1):
#   cmake --preset default
#   cmake --build --preset default

cmake_minimum_required(VERSION 3.21)
# The single source of the version: window titles, the record staleness stamp,
# and the HTTP User-Agent all derive from it via src/core/version.h.
project(Hydra VERSION 1.5.1 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT MSVC)
    message(FATAL_ERROR "Hydra's C++ port is Windows/MSVC only.")
endif()

# Parallel compilation within each target.
add_compile_options(/MP)

# Strict flags for our own code, applied via this interface target; third-party
# gets relaxed warnings so a vendor update never turns into a wall of warnings.
add_library(hydra_warnings INTERFACE)
target_compile_options(hydra_warnings INTERFACE /W4 /EHsc)

# ---- third_party: Dear ImGui (core + Win32/DX11 backends) ----------------
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
    third_party  # imconfig.h includes "imgui_test_engine/imgui_te_imconfig.h"
)
target_compile_options(imgui PRIVATE /W1 /EHsc)

# ---- third_party: Dear ImGui Test Engine ---------------------------------
# Drives the real GUI by widget label for the hydra_uitest runner and the
# `Hydra.exe --uitest` attached mode (docs/agents/ui-testing.md). imconfig.h
# turns its hooks on for every ImGui build, so this links wherever imgui does.
add_library(imgui_test_engine STATIC
    third_party/imgui_test_engine/imgui_capture_tool.cpp
    third_party/imgui_test_engine/imgui_te_context.cpp
    third_party/imgui_test_engine/imgui_te_coroutine.cpp
    third_party/imgui_test_engine/imgui_te_engine.cpp
    third_party/imgui_test_engine/imgui_te_exporters.cpp
    third_party/imgui_test_engine/imgui_te_perftool.cpp
    third_party/imgui_test_engine/imgui_te_ui.cpp
    third_party/imgui_test_engine/imgui_te_utils.cpp
)
target_include_directories(imgui_test_engine PUBLIC third_party/imgui_test_engine)
target_link_libraries(imgui_test_engine PUBLIC imgui)
target_compile_options(imgui_test_engine PRIVATE /W1 /EHsc)

# ---- third_party: SQLite3 amalgamation -----------------------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5=0)
target_compile_options(sqlite3 PRIVATE /W1)

# ---- third_party: miniz (raw deflate, for Clone Hero .srb bundles) -------
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_options(miniz PRIVATE /W1 /EHsc)

# ---- third_party: chart Preview media libraries --------------------------
# Vendored for the 3D note-highway Preview (see docs/adr/0004, 0005):
# miniaudio (public domain/MIT-0), stb_vorbis (public domain), libogg +
# libopus (BSD).
#
# EXCLUDE_FROM_ALL: each of these builds only when a Preview module links it,
# so a build that doesn't reach the Preview is byte-for-byte unaffected and
# the heavy libopus compile stays off the default path until it's wired in.
#
# miniaudio drives the output device, resampling, and mixing; each audio
# format is decoded to PCM by dedicated code (stb_vorbis for OGG, libopus +
# libogg for OPUS, miniaudio's own dr_libs for MP3/WAV/FLAC).
add_library(miniaudio STATIC EXCLUDE_FROM_ALL third_party/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)
target_compile_options(miniaudio PRIVATE /W1)

add_library(stb_vorbis STATIC EXCLUDE_FROM_ALL third_party/stb/stb_vorbis.c)
target_include_directories(stb_vorbis PUBLIC third_party/stb)
target_compile_options(stb_vorbis PRIVATE /W1)

# libogg + libopus build through their own CMake. Force static libraries and
# drop their tests, programs, and docs so only the codec libs are produced.
# libogg's cmake_minimum_required is 2.8.12; CMake 4.x refuses compatibility
# below 3.5, so raise the effective floor for these vendored subprojects. This
# does not touch the vendored files, and our own top-level minimum is 3.21.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/ogg EXCLUDE_FROM_ALL)
add_subdirectory(third_party/opus EXCLUDE_FROM_ALL)

# ---- hydra_image: the single stb_image TU (shared PNG/JPEG decode) --------
# stb_image's implementation may be compiled in exactly one TU across the binary
# (it defines non-static symbols), yet both the GUI icons and the Preview
# renderer's textures decode images. So the STB_IMAGE_IMPLEMENTATION lives
# here behind image/decode.h and both link this one lib (docs/adr/0008).
add_library(hydra_image STATIC src/image/decode.cpp)
target_include_directories(hydra_image PUBLIC src PRIVATE third_party/stb)
# stb_image.h is third-party; keep it out of the /W4 sweep like icons.cpp.
target_compile_options(hydra_image PRIVATE /W1 /EHsc)

# ---- hydra_core: everything below the GUI/CLI shells ---------------------
add_library(hydra_core STATIC
    src/core/timing.cpp
    src/core/chord_tables.cpp
    src/core/model.cpp
    src/core/scoring.cpp
    src/core/squeeze_rating.cpp
    src/core/winstr.cpp
    src/parse/midi.cpp
    src/parse/song.cpp
    src/parse/srb.cpp
    src/search/graph.cpp
    src/search/engine.cpp
    src/search/pather.cpp
    src/store/serialize.cpp
    src/store/record_store.cpp
    src/app/analysis.cpp
    src/app/config.cpp
    src/app/html_page.cpp
    src/app/path_view.cpp
    src/app/preview_view.cpp
    src/app/preview_source.cpp
    src/app/report.cpp
    src/app/report_files.cpp
)
target_include_directories(hydra_core PUBLIC src)
target_compile_definitions(hydra_core PUBLIC HYDRA_VERSION="${PROJECT_VERSION}")
# store/ needs sqlite3; app/ hashes chart files via CNG (bcrypt.lib) and walks
# folders via the Win32 Find*File API (already linked into every Windows exe).
# shell32: report_files.cpp hands finished report pages to the browser with
# ShellExecuteW, for both the GUI's report jobs and the hydra_report CLI.
target_link_libraries(hydra_core PUBLIC sqlite3 miniz)
target_link_libraries(hydra_core PRIVATE bcrypt shell32 hydra_warnings)

# ---- hydra_audio: the Preview audio engine (decode + mix + player) -------
# Decodes chart stems to float PCM, mixes them, and (GUI-side) drives the
# output device. Kept separate from hydra_core so the CLI tools never pull in
# the codec libraries, and linked by hydra_ui and hydra_tests. The decode and
# mix code is pure and unit-tested; only the player owns a miniaudio device.
# Depends on hydra_core for the PreviewAudioStem type and file I/O helpers.
add_library(hydra_audio STATIC
    src/audio/decode.cpp
    src/audio/mixer.cpp
    src/audio/player.cpp
    src/audio/device.cpp
)
target_link_libraries(hydra_audio PUBLIC hydra_core)
# miniaudio decodes WAV/MP3/FLAC (its dr_libs) and, GUI-side, drives the device;
# stb_vorbis decodes OGG; libopus over a libogg page demux decodes OPUS.
target_link_libraries(hydra_audio PRIVATE
    miniaudio stb_vorbis opus ogg hydra_warnings)

# ---- hydra_render: the Preview 3D note-highway renderer (GUI-side) --------
# A faithful port of Onyx's 3D drum previewer (docs/adr/0008) rendering to an
# offscreen D3D11 target (docs/adr/0005). The pure half — the .obj reader, the
# config, the instant timeline (track_state) and the draw-list builder
# (highway_draw, where every Onyx number lives) — is device-free and
# unit-tested; only preview_renderer touches D3D. Kept out of hydra_core like
# hydra_audio; linked by hydra_ui and hydra_tests.
add_library(hydra_render STATIC
    src/render/file_util.cpp
    src/render/obj_loader.cpp
    src/render/preview_config.cpp
    src/render/track_state.cpp
    src/render/highway_draw.cpp
    src/render/preview_renderer.cpp
)
target_link_libraries(hydra_render PUBLIC hydra_core)
# third_party/json parses assets/preview/3d-config.json (preview_config).
target_include_directories(hydra_render PRIVATE third_party/json)
# d3d11/dxgi drive the offscreen targets and d3dcompiler compiles the HLSL
# files at runtime; hydra_image decodes the Onyx textures.
target_link_libraries(hydra_render PRIVATE
    d3d11 dxgi d3dcompiler hydra_image hydra_warnings)

# ---- hydra_dm: the DMBot leaderboards feature (GUI-only) -----------------
# Kept out of hydra_core so the CLI tools don't link WinHTTP or carry the
# comparison page. third_party/json parses the leaderboard API responses.
# /utf-8: dm_report.cpp's HTML template embeds non-ASCII glyphs (arrows, em
# dash) in string literals; without this MSVC reads the source in the system
# code page and can mangle them.
add_library(hydra_dm STATIC
    src/app/dm_report.cpp
    src/net/dmbot_client.cpp
)
target_link_libraries(hydra_dm PUBLIC hydra_core)
target_link_libraries(hydra_dm PRIVATE winhttp hydra_warnings)
target_include_directories(hydra_dm PRIVATE third_party/json)
target_compile_options(hydra_dm PRIVATE /utf-8)

# ---- hydra_ui: the window-independent GUI ---------------------------------
# Everything but the Win32 window (main.cpp), shared by Hydra.exe and the
# headless GUI test runner.
add_library(hydra_ui STATIC
    src/ui/app_shell.cpp
    src/ui/app_state.cpp
    src/ui/details_view.cpp
    src/ui/dm_jobs.cpp
    src/ui/icons.cpp
    src/ui/library_jobs.cpp
    src/ui/library_view.cpp
    src/ui/preview_controller.cpp
    src/ui/preview_load_job.cpp
    src/ui/preview_transport.cpp
    src/ui/theme.cpp
    src/ui/win32_dialogs.cpp
)
# stb_image.h is third-party; keep icons.cpp out of our /W4 sweep like the
# other vendored libraries above.
set_source_files_properties(src/ui/icons.cpp PROPERTIES COMPILE_OPTIONS "/W1")
target_include_directories(hydra_ui PRIVATE third_party/stb)
# hydra_render + hydra_audio join the GUI here (Phase 5): the Preview tab's
# controller (preview_controller.cpp) drives the offscreen renderer and the
# audio engine, and preview_load_job.cpp's PreviewLoadJob decodes + mixes
# stems. PUBLIC so the exes link them transitively.
target_link_libraries(hydra_ui PUBLIC hydra_dm imgui imgui_test_engine hydra_render hydra_audio)
# shell32/ole32: the folder-picker dialog (opening reports moved down to
# hydra_core's report_files.cpp with the rest of the report-file handling);
# hydra_image: icons.cpp decodes its PNGs through the shared stb TU.
target_link_libraries(hydra_ui PRIVATE ole32 shell32 hydra_image hydra_warnings)

# ---- Hydra.exe: the GUI shell ---------------------------------------------
# The SP cap (once a separate "Uncapped" build) is a runtime setting, so there
# is one exe.
add_executable(Hydra WIN32 src/ui/main.cpp src/ui/hydra.rc)
target_link_libraries(Hydra PRIVATE hydra_ui d3d11 dxgi d3dcompiler shell32 hydra_warnings)
# GUI-subsystem app entered from a standard int main(), like the ImGui example.
target_link_options(Hydra PRIVATE /ENTRY:mainCRTStartup)
# resource/ (fonts, icons) is read relative to the exe at runtime; keep a
# copy beside the build output so the exe doesn't need to run from the repo
# root.
add_custom_command(TARGET Hydra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resource" "$<TARGET_FILE_DIR:Hydra>/resource"
)
# assets/ (the Preview highway art, loaded at runtime via exe_dir()) is
# staged beside the exe the same way resource/ is (see assets/preview/README).
add_custom_command(TARGET Hydra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:Hydra>/assets"
)

# Attached GUI testing (`Hydra.exe --uitest ...`, docs/agents/ui-testing.md):
# the exe carries the test harness + tests so a test can run inside the real
# window at watchable speed.
target_sources(Hydra PRIVATE
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
)
target_include_directories(Hydra PRIVATE tests/ui)
target_compile_definitions(Hydra PRIVATE
    HYDRA_UITEST_ATTACHED
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
    HYDRA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource"
)

# ---- hydra_batch / hydra_report: the command-line tools ------------------
# Both read the same settings INI and database the GUI uses.
add_executable(hydra_batch src/cli/batch.cpp)
add_executable(hydra_report src/cli/report.cpp)
foreach(tool IN ITEMS hydra_batch hydra_report)
    target_link_libraries(${tool} PRIVATE hydra_core hydra_warnings)
endforeach()

# ---- hydra_tests: the doctest harness ------------------------------------
add_executable(hydra_tests
    tests/test_main.cpp
    tests/test_timing.cpp
    tests/test_chord_tables.cpp
    tests/test_midi.cpp
    tests/test_model.cpp
    tests/test_squeeze_rating.cpp
    tests/test_path_view.cpp
    tests/test_preview_view.cpp
    tests/test_preview_source.cpp
    tests/test_audio_decode.cpp
    tests/test_audio_mixer.cpp
    tests/test_audio_player.cpp
    tests/test_obj_loader.cpp
    tests/test_preview_config.cpp
    tests/test_track_state.cpp
    tests/test_highway_draw.cpp
    tests/test_preview_golden.cpp
    tests/test_preview_clock.cpp
    tests/test_preview_transport.cpp
    tests/test_image_decode.cpp
    tests/test_preview_renderer.cpp
    tests/test_song.cpp
    tests/test_srb.cpp
    tests/test_search.cpp
    tests/test_store.cpp
    tests/test_analysis.cpp
    tests/test_report.cpp
    tests/test_config.cpp
    tests/test_dm_report.cpp
    tests/test_app_state.cpp
)
# hydra_dm: test_dm_report exercises the leaderboards join and JSON parsers.
# hydra_audio: test_audio_decode exercises the stem decoders and mixer.
# hydra_render: the highway geometry, meshes, and a WARP offscreen-render test —
# hence d3d11/dxgi (a headless WARP device) and d3dcompiler (runtime HLSL).
# hydra_ui: test_app_state builds a real AppState on a scratch store. Only the
# window-independent half is exercised; nothing here opens a window or a device.
target_link_libraries(hydra_tests PRIVATE
    hydra_ui hydra_dm hydra_audio hydra_render hydra_image d3d11 dxgi d3dcompiler
    hydra_warnings)
target_include_directories(hydra_tests PRIVATE
    third_party/doctest
    third_party/json
)
# doctest.h specializes std::tuple, which newer MSVC flags (C5285); harmless.
target_compile_options(hydra_tests PRIVATE /wd5285)

# Where the chart corpus and fixtures live, so tests find them regardless of
# the working directory.
target_compile_definitions(hydra_tests PRIVATE
    HYDRA_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testdata"
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
)

# ---- hydra_uitest: the headless GUI test runner ----------------------------
# Runs the real ImGui UI with no window, driven by the Dear ImGui Test Engine;
# see docs/agents/ui-testing.md.
add_executable(hydra_uitest
    tests/ui/uitest_main.cpp
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
)
target_link_libraries(hydra_uitest PRIVATE hydra_ui d3d11 dxgi d3dcompiler hydra_warnings)
target_compile_definitions(hydra_uitest PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
    HYDRA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource"
)

enable_testing()
add_test(NAME hydra_tests COMMAND hydra_tests)
add_test(NAME hydra_uitest COMMAND hydra_uitest --all)

# ---- packaging -----------------------------------------------------------
# `cpack -G ZIP` (or build_cpp.ps1 -Package) produces Hydra-<ver>-win64.zip:
# the GUI, both CLI tools, resource/, LICENSE and README, all in one folder.
# User data (hydra*.db/.ini) is created next to the exe at runtime and never
# lives in the package, so a rebuilt zip can't clobber anyone's records.
install(TARGETS Hydra hydra_batch hydra_report RUNTIME DESTINATION .)
install(DIRECTORY resource/ DESTINATION resource)
install(DIRECTORY assets/ DESTINATION assets)
install(FILES LICENSE README.md DESTINATION .)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "Hydra-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
include(CPack)

# ---- hydra_bench: engine timing harness (not a test; built on demand) ----
add_executable(hydra_bench EXCLUDE_FROM_ALL tools/bench.cpp)
target_link_libraries(hydra_bench PRIVATE hydra_core hydra_warnings)
target_include_directories(hydra_bench PRIVATE tests third_party/json)
target_compile_definitions(hydra_bench PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
)
