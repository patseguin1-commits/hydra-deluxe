// The app version, single-sourced from CMake's project(Hydra VERSION ...)
// via target_compile_definitions. Everything that needs the version string
// (window titles, the record staleness stamp, the HTTP User-Agent) derives
// from HYDRA_VERSION so a release bump is one edit in CMakeLists.txt.

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
// installer/hydra.iss).
inline constexpr const wchar_t* kWindowTitleW = L"Hydra v" HYDRA_VERSION_W;
inline constexpr const wchar_t* kAppUserModelIDW = L"Hydra.Hydra";
}  // namespace hydra

#endif  // HYDRA_CORE_VERSION_H
