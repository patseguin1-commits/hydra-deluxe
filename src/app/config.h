// Persisted user settings + user file locations, shared by the GUI
// shells and the command-line tools. Split out of ui/app_state so a console
// exe can read the same INI the app writes (hydra_batch.py's contract:
// "results match what the app would produce for the same songs").
//

#ifndef HYDRA_APP_CONFIG_H
#define HYDRA_APP_CONFIG_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "store/record_store.h"

namespace hydra::app {

// Directory containing the running executable (UTF-8). User files live here,
// mirroring hymisc.ROOTPATH's app-relative layout.
std::string exe_dir();

// The user's database and settings file, next to the exe ("hydra.db" /
// "hydra_settings.ini").
std::string db_path();
std::string ini_path();

// Opens the store at `db` and, the first time, pulls in any records from a
// pre-1.6 "hydra_uncapped.db" sitting in the same folder (the old Uncapped
// edition's separate library). The old file is never modified.
std::unique_ptr<store::RecordStore> open_store(const std::string& db);

// The Preview's authored highway art (exe_dir()\assets\preview by default).
std::string asset_dir();

// Process-wide overrides for the three locations above. Empty = default.
// Set once at startup by harnesses that must run the real app code on scratch
// files (the GUI test runner, docs/agents/ui-testing.md); the app never sets
// them.
struct PathOverrides {
    std::string db_path;
    std::string ini_path;
    std::string asset_dir;
};
void set_path_overrides(PathOverrides overrides);
const PathOverrides& path_overrides();

// Persisted user settings, mirroring HyAppUserSettings' fields. Loaded once at
// startup and written back to disk on every change (matching Python's
// HyAppUserSetting descriptor, which saves on every __set__).
struct Settings {
    std::vector<std::string> chartfolders;
    bool is_rescan = false;

    std::string view_difficulty = "Expert";
    bool view_prodrums = true;
    bool view_bass2x = true;

    int depth_value = 4;
    int depth_mode = 0;  // 0 = scores, 1 = points (search/engine.h convention)

    bool mslimit_enabled = true;
    int mslimit_value = 10;

    // The per-side hit window in real ms; feeds the squeeze budgets, the
    // backend ratings, and the report tiers. Display-layer only: it never
    // reaches the search, so changing it never invalidates stored records.
    int hit_window_ms = static_cast<int>(kDefaultHitWindowMs);

    // Preview playback volume, 0..100 %. A summed multi-stem mix at 100 % is
    // loud and clips, so the default sits well below it.
    int preview_volume = 40;

    // The Star Power meter ceiling in bars. 4 is Clone Hero's rule (the
    // default). nullopt is "Auto": raise the ceiling until the score settles
    // (search/pather.h analyze_auto_cap). INI line: sp_cap=4 / sp_cap=auto.
    // The pre-1.6 keys sp_cap_enabled/sp_cap_value are ignored on load.
    std::optional<int> sp_cap = 4;

    // Open the HTML path report in the browser as soon as a batch run builds
    // it; off by default (the finished modal offers an "Open report" button).
    bool auto_open_report = false;

    // The dmleaderboards user (Discord ID) last compared against, so the
    // "Compare dmleaderboards user" picker can pre-select it. Empty = none yet.
    std::string dm_last_user;

    static Settings load();
    // False when the INI can't be written (the GUI surfaces this; the CLIs
    // never call save()).
    bool save() const;

    // Explicit-path forms, so tests can round-trip through a temp file
    // without touching the real INI. load/save delegate here.
    static Settings load_file(const std::string& path);
    bool save_file(const std::string& path) const;

    // "Expert Pro Drums, 2x Bass" — mirrors HyAppUserSettings.chartmode_key.
    std::string chartmode_key() const;

    AnalysisSettings to_analysis_settings() const;

    // Which stored record the current SP cap asks for (store::CapQuery).
    store::CapQuery cap_query() const;

    // The identity of one chart's record under the current settings: this
    // hash, the current chartmode, and the current SP cap.
    store::RecordKey record_key(const std::string& hyhash) const;
};

}  // namespace hydra::app

#endif  // HYDRA_APP_CONFIG_H
