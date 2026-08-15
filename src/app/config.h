// Persisted user settings + per-edition file locations, shared by the GUI
// shells and the command-line tools. Split out of ui/app_state so a console
// exe can read the same INI the app writes (hydra_batch.py's contract:
// "results match what the app would produce for the same songs").
//
// The edition is a *runtime* parameter here, unlike the GUI's compile-time
// app/edition.h: the CLI exes take --uncapped at runtime (matching hymisc.py's
// sys.argv check), so the file names hang off a bool rather than a macro. The
// GUI passes hydra::kUncapped; both spellings produce the same paths.

#ifndef HYDRA_APP_CONFIG_H
#define HYDRA_APP_CONFIG_H

#include <string>
#include <vector>

#include "app/analysis.h"

namespace hydra::app {

// Directory containing the running executable (UTF-8). User files live here,
// mirroring hymisc.ROOTPATH's app-relative layout.
std::string exe_dir();

// Edition-separate file names, mirroring hymisc's hyapp{_uncapped}.db/.ini so
// the capped and uncapped editions never share a store. The suffix is empty
// for the capped edition, keeping "hydra.db" / "hydra_settings.ini" unchanged.
std::string db_path(bool uncapped);
std::string ini_path(bool uncapped);

// Persisted user settings, mirroring HyAppUserSettings' fields. Loaded once at
// startup and written back to disk on every change (matching Python's
// HyAppUserSetting descriptor, which saves on every __set__).
struct Settings {
    // Which edition's INI this was loaded from (and saves back to). Set by
    // load(); not itself a line in the file.
    bool uncapped = false;

    std::vector<std::string> chartfolders;
    bool is_rescan = false;

    std::string view_difficulty = "Expert";
    bool view_prodrums = true;
    bool view_bass2x = true;

    int depth_value = 4;
    int depth_mode = 0;  // 0 = scores, 1 = points (search/engine.h convention)

    bool mslimit_enabled = true;
    int mslimit_value = 10;

    // Uncapped edition only: a manual SP meter ceiling in bars. Disabled runs
    // the auto-settling ladder (the default); enabled forces the given cap
    // (any value). Ignored by the capped edition.
    bool sp_cap_enabled = false;
    int sp_cap_value = 8;

    static Settings load(bool uncapped);
    void save() const;

    // "Expert Pro Drums, 2x Bass" — mirrors HyAppUserSettings.chartmode_key.
    std::string chartmode_key() const;

    AnalysisSettings to_analysis_settings() const;
};

}  // namespace hydra::app

#endif  // HYDRA_APP_CONFIG_H
