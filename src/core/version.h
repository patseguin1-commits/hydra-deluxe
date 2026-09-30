// The app version, single-sourced from CMake's project(Hydra VERSION ...)
// via target_compile_definitions. The HTTP User-Agent is the one thing in the
// app that uses it; the installer reads the same number from CMakeLists.txt,
// so a release bump is one edit there. Stored results are stamped separately
// (src/store/stored_versions.h).

#ifndef HYDRA_CORE_VERSION_H
#define HYDRA_CORE_VERSION_H

#ifndef HYDRA_VERSION
#error "HYDRA_VERSION must be defined by the build (see CMakeLists.txt)"
#endif

// The same version as a wide literal (L"1.4.1"-style).
#define HYDRA_WIDEN_(s) L##s
#define HYDRA_WIDEN(s) HYDRA_WIDEN_(s)
#define HYDRA_VERSION_W HYDRA_WIDEN(HYDRA_VERSION)

namespace hydra {
// Window title and the taskbar identity (AppUserModelID; must match
// installer/hydra.iss). The title is the bare name, with no version. The
// AppUserModelID keeps its old value so existing taskbar pins still group
// with the running window.
inline constexpr const wchar_t* kWindowTitleW = L"Hydra Deluxe";
inline constexpr const wchar_t* kAppUserModelIDW = L"Hydra.Hydra";
}  // namespace hydra

#endif  // HYDRA_CORE_VERSION_H
