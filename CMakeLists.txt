# Hydra — Windows-only C++ build. Produces:
#   * Hydra.exe / HydraUncapped.exe   — the two GUI editions
#   * hydra_batch / hydra_report      — the command line tools
#   * hydra_tests                     — the doctest parity/unit harness
#   * a zip release via CPack (build_cpp.ps1 -Package)
#
# Third-party code (ImGui, SQLite, miniz, doctest, json, stb) is vendored under
# third_party/ and built here so there is nothing to install but a compiler +
# CMake. native/ holds the scoring/search core the Python-era accelerator
# already proved out, compiled in place.
#
# Configure/build (CMake and MSVC come from Visual Studio; see build_cpp.ps1):
#   cmake --preset default
#   cmake --build --preset default

cmake_minimum_required(VERSION 3.21)
project(Hydra VERSION 1.4.1 LANGUAGES C CXX RC)  # keep in step with app/edition.h

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT MSVC)
    message(FATAL_ERROR "Hydra's C++ port is Windows/MSVC only.")
endif()

# Strict flags for our own code; third-party gets relaxed warnings so a vendor
# update never turns into a wall of warnings in our build.
set(HYDRA_WARNINGS /W4 /EHsc)
add_compile_options(/O2)

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

# ---- third_party: SQLite3 amalgamation (used from Phase 4) ---------------
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5=0)

# ---- third_party: miniz (raw deflate, for Clone Hero .srb bundles) -------
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_options(miniz PRIVATE /W1 /EHsc)

# ---- the existing algorithm core, compiled in place ----------------------
# Reused verbatim (Phase 3 wires the native graph into these); compiling them
# here proves they build under the new toolchain and keeps the known-good
# scoring/search logic as the single source of truth.
add_library(hydra_native STATIC
    native/hydra_score.cpp
    native/hydra_search.cpp
)
target_include_directories(hydra_native PUBLIC native)
target_compile_options(hydra_native PRIVATE ${HYDRA_WARNINGS})

# ---- ported foundation modules (Phase 1+) --------------------------------
add_library(hydra_core STATIC
    src/core/timing.cpp
    src/core/chord_tables.cpp
    src/core/model.cpp
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
    src/app/report.cpp
    src/app/dm_report.cpp
    src/net/dmbot_client.cpp
)
target_include_directories(hydra_core PUBLIC src)
# third_party/sqlite: the store. third_party/json: nlohmann, used by the DMBot
# client to parse the leaderboard API. (The report's own JSON is hand-rolled.)
target_include_directories(hydra_core PRIVATE third_party/sqlite third_party/json)
# search/ wires the native ScoreGraph into the existing hydra_native engine, so
# hydra_core needs its headers (native/) and symbols (hy_search, hy_category_*).
# store/ needs sqlite3; app/ hashes chart files via CNG (bcrypt.lib) and walks
# folders via the Win32 Find*File API (already linked into every Windows exe).
target_link_libraries(hydra_core PUBLIC hydra_native sqlite3 miniz)
# bcrypt: CNG (chart-file MD5). winhttp: the DMBot leaderboard client (net/).
target_link_libraries(hydra_core PRIVATE bcrypt winhttp)
target_compile_options(hydra_core PRIVATE ${HYDRA_WARNINGS})
# /utf-8: dm_report.cpp's HTML template embeds non-ASCII glyphs (arrows, em
# dash) in string literals; without this MSVC reads the source in the system
# code page and can mangle them. Harmless for the ASCII-only sources here.
target_compile_options(hydra_core PRIVATE /utf-8)

# ---- Hydra.exe / HydraUncapped.exe: the GUI shells -----------------------
# Two editions from one source set (see src/app/edition.h). hydra_core is
# edition-agnostic -- the uncapped SP semantics are a runtime parameter -- so
# both link the same static lib; only HydraUncapped is compiled with
# -DHYDRA_UNCAPPED, which flips the edition constants (title, db/ini names,
# the uncapped flag). Mirrors hydra_app.py + hydra_uncapped.py.
set(HYDRA_GUI_SOURCES
    src/app/main.cpp
    src/app/hydra.rc
    src/ui/app_state.cpp
    src/ui/icons.cpp
    src/ui/jobs.cpp
    src/ui/library_view.cpp
    src/ui/details_view.cpp
    src/ui/theme.cpp
    src/ui/win32_dialogs.cpp
)
# stb_image.h is third-party; keep it out of our /W4 sweep like the other
# vendored libraries above. (Source-scoped: applies in every target below.)
set_source_files_properties(src/ui/icons.cpp PROPERTIES COMPILE_OPTIONS "/W1")

foreach(edition IN ITEMS Hydra HydraUncapped)
    add_executable(${edition} WIN32 ${HYDRA_GUI_SOURCES})
    target_include_directories(${edition} PRIVATE third_party/stb)
    # shell32: ShellExecuteW, which ReportJob uses to open the finished report.
    target_link_libraries(${edition} PRIVATE hydra_core imgui d3d11 dxgi d3dcompiler ole32 shell32)
    target_compile_options(${edition} PRIVATE ${HYDRA_WARNINGS})
    # GUI-subsystem app entered from a standard int main(), like the ImGui example.
    target_link_options(${edition} PRIVATE /ENTRY:mainCRTStartup)
    # resource/ (fonts, icons) is read relative to the exe at runtime; keep a
    # copy beside every build output so the exe doesn't need to run from the
    # repo root. Mirrors what Phase 6's packaging step zips up.
    add_custom_command(TARGET ${edition} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/resource" "$<TARGET_FILE_DIR:${edition}>/resource"
    )
endforeach()
target_compile_definitions(HydraUncapped PRIVATE HYDRA_UNCAPPED)

# ---- hydra_batch / hydra_report: the command-line tools ------------------
# Console ports of hydra_batch.py / hydra_report.py. One exe each serves both
# editions: --uncapped selects the uncapped settings/db at runtime, the way
# the Python CLIs read the flag from sys.argv (hymisc.py) — no second build.
add_executable(hydra_batch src/cli/batch.cpp)
add_executable(hydra_report src/cli/report.cpp)
foreach(tool IN ITEMS hydra_batch hydra_report)
    target_link_libraries(${tool} PRIVATE hydra_core)
    target_compile_options(${tool} PRIVATE ${HYDRA_WARNINGS})
endforeach()

# ---- hydra_tests: the parity/unit harness (doctest) ----------------------
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
)
target_link_libraries(hydra_tests PRIVATE hydra_core hydra_native)
target_include_directories(hydra_tests PRIVATE
    third_party/doctest
    third_party/json
)
target_compile_options(hydra_tests PRIVATE ${HYDRA_WARNINGS})
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

# ---- packaging (replaces the PyInstaller build.py/specs) -----------------
# `cpack -G ZIP` (or build_cpp.ps1 -Package) produces Hydra-<ver>-win64.zip:
# both GUI editions, both CLI tools, resource/, LICENSE and README, all in one
# folder the way the PyInstaller dist shipped. User data (hydra*.db/.ini) is
# created next to the exe at runtime and never lives in the package, so a
# rebuilt zip can't clobber anyone's records — the problem build.py existed
# to work around.
install(TARGETS Hydra HydraUncapped hydra_batch hydra_report RUNTIME DESTINATION .)
install(DIRECTORY resource/ DESTINATION resource)
install(FILES LICENSE README.md DESTINATION .)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "Hydra-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
include(CPack)

# ---- hydra_bench: engine timing harness (not a test) ---------------------
add_executable(hydra_bench tools/bench.cpp)
target_link_libraries(hydra_bench PRIVATE hydra_core hydra_native)
target_include_directories(hydra_bench PRIVATE tests third_party/json)
target_compile_options(hydra_bench PRIVATE ${HYDRA_WARNINGS})
target_compile_definitions(hydra_bench PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
)
