// Compile-time edition selection, mirroring hymisc.apply_edition / hydra_uncapped.py.
//
// The uncapped edition (built with -DHYDRA_UNCAPPED) removes Clone Hero's 4-bar
// Star Power meter cap and keeps its own database, settings and records so the
// two editions never mix (hymisc's _EDITIONFILE "_uncapped" suffix). The
// uncapped flag is a *runtime* parameter through hydra_core -- analyze_chart's
// `capped`, RecordStore's `uncapped`, current_record_version -- so hydra_core is
// edition-agnostic and both executables link the same static lib. Only the GUI
// shell binds an edition, here.
#pragma once

namespace hydra {

inline constexpr const char* kHydraVersion = "1.3.1";

// Per-edition user file names (hydra{_uncapped}.db / .ini) are built at
// runtime by app/config.h from the uncapped flag, since the CLI tools select
// their edition with --uncapped rather than a build flag.
#ifdef HYDRA_UNCAPPED
inline constexpr bool kUncapped = true;
inline constexpr const char* kEditionName = "Hydra Uncapped";
inline constexpr const wchar_t* kWindowTitleW = L"Hydra Uncapped v1.3.1";
#else
inline constexpr bool kUncapped = false;
inline constexpr const char* kEditionName = "Hydra";
inline constexpr const wchar_t* kWindowTitleW = L"Hydra v1.3.1";
#endif

}  // namespace hydra
