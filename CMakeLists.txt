# Hydra — Windows-only C++ build. Produces:
#   * Hydra.exe / HydraUncapped.exe   — the two GUI editions
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
project(Hydra VERSION 1.4.1 LANGUAGES C CXX RC)

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
)
target_compile_options(imgui PRIVATE /W1 /EHsc)

# ---- third_party: SQLite3 amalgamation -----------------------------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5=0)
target_compile_options(sqlite3 PRIVATE /W1)

# ---- third_party: miniz (raw deflate, for Clone Hero .srb bundles) -------
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_options(miniz PRIVATE /W1 /EHsc)

# ---- hydra_core: everything below the GUI/CLI shells ---------------------
add_library(hydra_core STATIC
    src/core/timing.cpp
    src/core/chord_tables.cpp
    src/core/model.cpp
    src/core/scoring.cpp
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
    src/app/report.cpp
)
target_include_directories(hydra_core PUBLIC src)
target_compile_definitions(hydra_core PUBLIC HYDRA_VERSION="${PROJECT_VERSION}")
# store/ needs sqlite3; app/ hashes chart files via CNG (bcrypt.lib) and walks
# folders via the Win32 Find*File API (already linked into every Windows exe).
target_link_libraries(hydra_core PUBLIC sqlite3 miniz)
target_link_libraries(hydra_core PRIVATE bcrypt hydra_warnings)

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

# ---- hydra_ui: the edition-agnostic half of the GUI ----------------------
# Compiled once and linked into both editions; only the sources that read
# hydra::kUncapped (main, app_state, details_view) build per edition.
add_library(hydra_ui STATIC
    src/ui/icons.cpp
    src/ui/jobs.cpp
    src/ui/library_view.cpp
    src/ui/theme.cpp
    src/ui/win32_dialogs.cpp
)
# stb_image.h is third-party; keep icons.cpp out of our /W4 sweep like the
# other vendored libraries above.
set_source_files_properties(src/ui/icons.cpp PROPERTIES COMPILE_OPTIONS "/W1")
target_include_directories(hydra_ui PRIVATE third_party/stb)
target_link_libraries(hydra_ui PUBLIC hydra_dm imgui)
# shell32: ShellExecuteW (opening reports); ole32: the folder-picker dialog.
target_link_libraries(hydra_ui PRIVATE ole32 shell32 hydra_warnings)

# ---- Hydra.exe / HydraUncapped.exe: the GUI shells -----------------------
# Two editions from one source set (see src/app/edition.h). hydra_core is
# edition-agnostic -- the uncapped SP semantics are a runtime parameter -- so
# both link the same static libs; only HydraUncapped is compiled with
# -DHYDRA_UNCAPPED, which flips the edition constants (title, db/ini names,
# the uncapped flag).
set(HYDRA_GUI_SOURCES
    src/ui/main.cpp
    src/ui/hydra.rc
    src/ui/app_state.cpp
    src/ui/details_view.cpp
)

foreach(edition IN ITEMS Hydra HydraUncapped)
    add_executable(${edition} WIN32 ${HYDRA_GUI_SOURCES})
    target_link_libraries(${edition} PRIVATE hydra_ui d3d11 dxgi d3dcompiler
                          shell32 hydra_warnings)
    # GUI-subsystem app entered from a standard int main(), like the ImGui example.
    target_link_options(${edition} PRIVATE /ENTRY:mainCRTStartup)
    # resource/ (fonts, icons) is read relative to the exe at runtime; keep a
    # copy beside every build output so the exe doesn't need to run from the
    # repo root.
    add_custom_command(TARGET ${edition} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resource" "$<TARGET_FILE_DIR:${edition}>/resource"
    )
endforeach()
target_compile_definitions(HydraUncapped PRIVATE HYDRA_UNCAPPED)

# ---- hydra_batch / hydra_report: the command-line tools ------------------
# One exe each serves both editions: --uncapped selects the uncapped
# settings/db at runtime — no second build.
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
    tests/test_song.cpp
    tests/test_srb.cpp
    tests/test_search.cpp
    tests/test_store.cpp
    tests/test_analysis.cpp
    tests/test_report.cpp
    tests/test_config.cpp
    tests/test_dm_report.cpp
)
# hydra_dm: test_dm_report exercises the leaderboards join and JSON parsers.
target_link_libraries(hydra_tests PRIVATE hydra_dm hydra_warnings)
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
)

enable_testing()
add_test(NAME hydra_tests COMMAND hydra_tests)

# ---- packaging -----------------------------------------------------------
# `cpack -G ZIP` (or build_cpp.ps1 -Package) produces Hydra-<ver>-win64.zip:
# both GUI editions, both CLI tools, resource/, LICENSE and README, all in one
# folder. User data (hydra*.db/.ini) is created next to the exe at runtime and
# never lives in the package, so a rebuilt zip can't clobber anyone's records.
install(TARGETS Hydra HydraUncapped hydra_batch hydra_report RUNTIME DESTINATION .)
install(DIRECTORY resource/ DESTINATION resource)
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
