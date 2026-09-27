# Hydra — Windows-only C++ build. Produces:
#   * Hydra.exe                       — the GUI
#   * hydra_batch / hydra_report / hydra_fillcompare — the command line tools
#   * hydra_tests                     — the doctest harness
#   * hydra_uitest                    — the headless GUI test runner
#   * hydra_bench / hydra_replay      — developer tools (build_cpp.ps1 builds them)
#   * a zip release via CPack (build_cpp.ps1 -Package)
#
# Third-party code (ImGui and its Test Engine, SQLite, miniz, doctest, json,
# stb, miniaudio, libogg, libopus) is vendored under third_party/ and built
# here, so there is nothing to install but a compiler + CMake.
#
# Configure/build (CMake and MSVC come from Visual Studio; see build_cpp.ps1):
#   cmake --preset default
#   cmake --build --preset default

cmake_minimum_required(VERSION 3.21)
# The single source of the version: window titles, the record staleness stamp,
# and the HTTP User-Agent all derive from it via src/core/version.h.
project(Hydra VERSION 1.8.4 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT MSVC)
    message(FATAL_ERROR "Hydra's C++ port is Windows/MSVC only.")
endif()

# Parallel compilation within each target.
add_compile_options(/MP)

# __FILE__ names the source relative to the repo, not by its absolute path.
# Opus's safety checks (OPUS_HARDENING) store __FILE__ for their error message,
# which put the builder's folder path into the shipped Hydra.exe 28 times.
# /d1trimfile strips the prefix (MSVC 16.4+); the checks themselves stay on.
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/" HYDRA_SOURCE_PREFIX)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/d1trimfile:${HYDRA_SOURCE_PREFIX}>)

# Strict flags for our own code, applied via this interface target; third-party
# gets relaxed warnings so a vendor update never turns into a wall of warnings.
# /utf-8: our sources are UTF-8, and several hold non-ASCII text in string
# literals (em dashes, arrows). Without it MSVC reads them in the system code
# page, which works on an English Windows only by luck. C++ only, so the
# resource compiler never sees it.
add_library(hydra_warnings INTERFACE)
target_compile_options(hydra_warnings INTERFACE
    /W4 /EHsc $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

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
# EXCLUDE_FROM_ALL: each of these builds only when a target that links it is
# built. hydra_audio links all four, so the CLI tools never compile them.
#
# miniaudio drives the output device and converts sample rates and channel
# counts; each audio format is decoded to PCM by dedicated code (stb_vorbis
# for OGG, libopus + libogg for OPUS, miniaudio's own dr_libs for MP3/WAV/FLAC).
add_library(miniaudio STATIC EXCLUDE_FROM_ALL third_party/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)
target_compile_options(miniaudio PRIVATE /W1)
# Compile out what Hydra never calls (option names from miniaudio.h's own
# table): encoders, generators, the high-level engine, its node graph and
# resource manager, and every backend but WASAPI. PUBLIC so every file that
# includes miniaudio.h sees the same declarations as the implementation.
target_compile_definitions(miniaudio PUBLIC
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS
    MA_ENABLE_WASAPI
)

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
# here behind image/decode.h and both link this one lib.
add_library(hydra_image STATIC src/image/decode.cpp)
target_include_directories(hydra_image PUBLIC src PRIVATE third_party/stb)
# stb_image.h is third-party; keep it out of the /W4 sweep. /utf-8 as for
# every Hydra target (see hydra_warnings).
target_compile_options(hydra_image PRIVATE /W1 /EHsc /utf-8)

# ---- hydra_core: everything below the GUI/CLI shells ---------------------
add_library(hydra_core STATIC
    src/core/timing.cpp
    src/core/model.cpp
    src/core/scoring.cpp
    src/core/rules.cpp
    src/core/squeeze_rating.cpp
    src/core/stars.cpp
    src/core/replay.cpp
    src/core/strutil.cpp
    src/core/winstr.cpp
    src/parse/midi.cpp
    src/parse/song.cpp
    src/parse/srb.cpp
    src/parse/chart_files.cpp
    src/parse/sng.cpp
    src/search/graph.cpp
    src/search/engine.cpp
    src/search/pather.cpp
    src/store/serialize.cpp
    src/store/path_codec.cpp
    src/store/record_store.cpp
    src/app/analysis.cpp
    src/app/config.cpp
    src/app/rules_file.cpp
    src/app/html_page.cpp
    src/app/dynamics_breakdown.cpp
    src/app/display_format.cpp
    src/app/library_query.cpp
    src/app/path_view.cpp
    src/app/preview_view.cpp
    src/app/preview_source.cpp
    src/app/fill_report.cpp
    src/app/report.cpp
    src/app/report_files.cpp
    src/app/user_messages.cpp
)
target_include_directories(hydra_core PUBLIC src)
target_compile_definitions(hydra_core PUBLIC HYDRA_VERSION="${PROJECT_VERSION}")
# store/ needs sqlite3; app/ hashes chart files via CNG (bcrypt.lib) and walks
# folders via the Win32 Find*File API (already linked into every Windows exe).
# shell32: report_files.cpp hands finished report pages to the browser with
# ShellExecuteW, for both the GUI's report jobs and the hydra_report CLI, and
# winstr.cpp splits every entry point's Unicode command line with
# CommandLineToArgvW. shell32 + ole32 + uuid: reports_dir() finds Documents
# with SHGetKnownFolderPath(FOLDERID_Documents) and frees the answer with
# CoTaskMemFree.
target_link_libraries(hydra_core PUBLIC sqlite3 miniz)
target_link_libraries(hydra_core PRIVATE bcrypt shell32 ole32 uuid hydra_warnings)

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
# A faithful port of Onyx's 3D drum previewer
# (docs/adr/0008-preview-is-a-faithful-port-of-onyx.md) rendering to an
# offscreen D3D11 target (docs/adr/0005). The pure half — the .obj reader, the
# config, the instant timeline (track_state) and the draw-list builder
# (highway_draw, where every Onyx number lives) — is device-free and
# unit-tested; only preview_renderer touches D3D. Kept out of hydra_core like
# hydra_audio; linked by hydra_ui and hydra_tests.
add_library(hydra_render STATIC
    src/render/obj_loader.cpp
    src/render/preview_config.cpp
    src/render/track_state.cpp
    src/render/highway_draw.cpp
    src/render/overlay_layout.cpp
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
add_library(hydra_dm STATIC
    src/app/dm_report.cpp
    src/net/dmbot_client.cpp
)
target_link_libraries(hydra_dm PUBLIC hydra_core)
target_link_libraries(hydra_dm PRIVATE winhttp hydra_warnings)
target_include_directories(hydra_dm PRIVATE third_party/json)

# ---- hydra_ui: the window-independent GUI ---------------------------------
# Everything but the Win32 window (main.cpp), shared by Hydra.exe and the
# headless GUI test runner.
add_library(hydra_ui STATIC
    src/ui/app_shell.cpp
    src/ui/app_state.cpp
    src/ui/details_panel.cpp
    src/ui/dm_jobs.cpp
    src/ui/dynamics_tab.cpp
    src/ui/icons.cpp
    src/ui/paths_tab.cpp
    src/ui/preview_tab.cpp
    src/ui/stars_tab.cpp
    src/ui/library_dialogs.cpp
    src/ui/library_jobs.cpp
    src/ui/library_table.cpp
    src/ui/library_toolbar.cpp
    src/ui/settings_bar.cpp
    src/ui/library_view.cpp
    src/ui/preview_controller.cpp
    src/ui/dynamics_load_job.cpp
    src/ui/preview_load_job.cpp
    src/ui/preview_transport.cpp
    src/ui/theme.cpp
    src/ui/win32_dialogs.cpp
)
# hydra_render + hydra_audio join the GUI here (Phase 5): the Preview tab's
# controller (preview_controller.cpp) drives the offscreen renderer and the
# audio engine, and preview_load_job.cpp's PreviewLoadJob decodes + mixes
# stems. PUBLIC so the exes link them transitively.
target_link_libraries(hydra_ui PUBLIC hydra_dm imgui imgui_test_engine hydra_render hydra_audio)
# ole32: the folder-picker dialog (COM's IFileOpenDialog). shell32 is not
# needed here: hydra_core links it for report_files.cpp's ShellExecuteW.
# hydra_image: icons.cpp decodes its PNGs through the shared stb TU.
target_link_libraries(hydra_ui PRIVATE ole32 hydra_image hydra_warnings)

# Attached GUI testing (`Hydra.exe --uitest ...`, docs/agents/ui-testing.md)
# builds the GUI tests into the exe. On for dev builds; the "ship" preset (the
# installer's build) turns it off, so the shipped exe carries no tests and no
# repo paths.
option(HYDRA_UITEST_ATTACHED
       "Build Hydra.exe with the attached GUI test harness (off for the installer)" ON)

# ---- hydra_uitest_harness: the GUI tests, compiled once --------------------
# The harness, the script runner and the tests. hydra_uitest always links
# them; Hydra.exe links them when HYDRA_UITEST_ATTACHED is on. The repo paths
# the tests need are compiled in here and nowhere else.
add_library(hydra_uitest_harness STATIC
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
    tests/ui/uitest_library.cpp
    tests/ui/uitest_details.cpp
    tests/ui/uitest_preview.cpp
    tests/ui/uitest_batch_reports.cpp
)
target_include_directories(hydra_uitest_harness PUBLIC tests/ui)
target_link_libraries(hydra_uitest_harness PUBLIC hydra_ui PRIVATE hydra_warnings)
target_compile_definitions(hydra_uitest_harness PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
    HYDRA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource"
)

# ---- Hydra.exe: the GUI shell ---------------------------------------------
# The SP cap (once a separate "Uncapped" build) is a runtime setting, so there
# is one exe.
add_executable(Hydra WIN32 src/ui/main.cpp src/ui/hydra.rc)
target_link_libraries(Hydra PRIVATE hydra_ui d3d11 dxgi d3dcompiler hydra_warnings)
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

# Attached GUI testing: a test runs inside the real window at watchable speed.
# main.cpp compiles the --uitest path only under HYDRA_UITEST_ATTACHED.
if(HYDRA_UITEST_ATTACHED)
    target_link_libraries(Hydra PRIVATE hydra_uitest_harness)
    target_compile_definitions(Hydra PRIVATE HYDRA_UITEST_ATTACHED)
endif()

# ---- hydra_batch / hydra_report: the command-line tools ------------------
# Both read the same settings INI and database the GUI uses.
add_executable(hydra_batch src/cli/batch.cpp)
add_executable(hydra_report src/cli/report.cpp)
add_executable(hydra_fillcompare src/cli/fillcompare.cpp)
foreach(tool IN ITEMS hydra_batch hydra_report hydra_fillcompare)
    target_link_libraries(${tool} PRIVATE hydra_core hydra_warnings)
endforeach()

# ---- hydra_tests: the doctest harness ------------------------------------
add_executable(hydra_tests
    tests/test_main.cpp
    tests/test_strutil.cpp
    tests/test_library_query.cpp
    tests/test_corpus_cache.cpp
    tests/test_timing.cpp
    tests/test_chord_code.cpp
    tests/test_winstr.cpp
    tests/test_midi.cpp
    tests/test_model.cpp
    tests/test_squeeze_rating.cpp
    tests/test_stars.cpp
    tests/test_theme.cpp
    tests/test_path_view.cpp
    tests/test_preview_view.cpp
    tests/test_preview_source.cpp
    tests/test_audio_decode.cpp
    tests/test_audio_mixer.cpp
    tests/test_audio_player.cpp
    tests/test_audio_device.cpp
    tests/test_obj_loader.cpp
    tests/test_preview_config.cpp
    tests/test_track_state.cpp
    tests/test_highway_draw.cpp
    tests/test_overlay_layout.cpp
    tests/test_preview_golden.cpp
    tests/test_preview_clock.cpp
    tests/test_preview_transport.cpp
    tests/test_image_decode.cpp
    tests/test_preview_renderer.cpp
    tests/test_dynamics_breakdown.cpp
    tests/test_dynamics_store.cpp
    tests/test_song.cpp
    tests/test_sng.cpp
    tests/test_srb.cpp
    tests/test_search.cpp
    tests/test_replay.cpp
    tests/test_store.cpp
    tests/test_path_codec.cpp
    tests/test_analysis.cpp
    tests/test_report.cpp
    tests/test_config.cpp
    tests/test_rules.cpp
    tests/test_dm_report.cpp
    tests/test_user_messages.cpp
    tests/test_library_jobs.cpp
    tests/test_fill_deadline.cpp
    tests/test_fill_report.cpp
    tests/test_app_state.cpp
    tests/test_song_panel_state.cpp
    tests/test_app_shell.cpp
    tests/test_preview_controller.cpp
    tests/test_utf8_paths.cpp
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
# see docs/agents/ui-testing.md. The GUI tests themselves live in
# hydra_uitest_harness (above), which Hydra.exe shares in dev builds.
add_executable(hydra_uitest tests/ui/uitest_main.cpp)
target_link_libraries(hydra_uitest PRIVATE
    hydra_uitest_harness d3d11 dxgi d3dcompiler hydra_warnings)

enable_testing()
add_test(NAME hydra_tests COMMAND hydra_tests)
add_test(NAME hydra_uitest COMMAND hydra_uitest --all)

# ---- packaging -----------------------------------------------------------
# `cpack -G ZIP` (or build_cpp.ps1 -Package) produces Hydra-<ver>-win64.zip:
# the GUI, both CLI tools, resource/, LICENSE and README, all in one folder.
# User data (hydra*.db/.ini) is created next to the exe at runtime and never
# lives in the package, so a rebuilt zip can't clobber anyone's records.
install(TARGETS Hydra hydra_batch hydra_report hydra_fillcompare RUNTIME DESTINATION .)
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

# ---- hydra_replay: arbitrary-path scorer (not a test; built on demand) ----
# Prices a hand-written Star Power path chord by chord, dumps a record's
# stored paths, and self-checks the replay against the engine over the
# corpus (hence the same corpus_util.h wiring hydra_bench uses).
add_executable(hydra_replay EXCLUDE_FROM_ALL tools/replay.cpp)
target_link_libraries(hydra_replay PRIVATE hydra_core hydra_warnings)
target_include_directories(hydra_replay PRIVATE tests third_party/json)
target_compile_definitions(hydra_replay PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
)

# ---- CLI end-to-end tests ------------------------------------------------
# tests/test_cli.cpp runs the three console tools as real processes, so
# building the tests builds the tools first and tells the tests where they are.
target_sources(hydra_tests PRIVATE tests/test_cli.cpp)
add_dependencies(hydra_tests hydra_batch hydra_report hydra_fillcompare)
target_compile_definitions(hydra_tests PRIVATE
    HYDRA_BATCH_EXE="$<TARGET_FILE:hydra_batch>"
    HYDRA_REPORT_EXE="$<TARGET_FILE:hydra_report>"
    HYDRA_FILLCOMPARE_EXE="$<TARGET_FILE:hydra_fillcompare>"
)

# ---- hydra_replay_json: the dump/target JSON shapes (tools only) ----------
# hydra_replay writes and reads these, and tests/test_replay pins them. The
# app never reads a path as JSON, so this stays out of hydra_core, and core/
# stays free of the JSON library.
add_library(hydra_replay_json STATIC tools/replay_json.cpp)
target_include_directories(hydra_replay_json PUBLIC tools third_party/json)
target_link_libraries(hydra_replay_json PUBLIC hydra_core PRIVATE hydra_warnings)
target_link_libraries(hydra_replay PRIVATE hydra_replay_json)
target_link_libraries(hydra_tests PRIVATE hydra_replay_json)
