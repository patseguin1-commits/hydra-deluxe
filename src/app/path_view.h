// Path view-model — the derived strings and flags the Song Details screen
// renders, built here so the derivation is testable without an ImGui frame.
//
// Every string below is exactly what the details modal shows; the view layer
// (ui/details_view.cpp) only lays these out. The same forms were previously
// composed inline in the draw functions, where no test could reach them.
// Follows the LibraryPage::RowSummary pattern (ui/app_state.h): resolve the
// display facts once, render dumb.
//

#ifndef HYDRA_APP_PATH_VIEW_H
#define HYDRA_APP_PATH_VIEW_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/model.h"
#include "core/rules.h"
#include "core/squeeze_rating.h"
#include "core/timing.h"
#include "store/record_store.h"

namespace hydra::app {

// One display line plus whether it renders warning-colored.
struct TextLine {
    std::string text;
    bool warn = false;
};

// ---- stored-result panel --------------------------------------------------

struct RecordStatusView {
    store::RecordStatus state = store::RecordStatus::NotAnalyzed;
    std::vector<std::string> lines;  // Ready only
};
// The store decides the status; this only turns it into display lines. A
// Ready record with no paths is still a real result, shown as one line.
RecordStatusView build_record_status(const store::RecordLookup& lookup);

// ---- multiplier squeezes ---------------------------------------------------

struct MultSqueezeView {
    std::string label;  // "3x   (+50 pts):   [Red - ...]"
    std::string howto;
};
std::vector<MultSqueezeView> build_multsqueezes(const HydraRecord& record);

// ---- activations -----------------------------------------------------------

struct BackendRowView {
    std::string timing;   // "%.1f" raw offset
    std::string tooltip;  // effective-ms explanation; empty when none
    std::string chord;
    std::string points;   // what the engine paid for the row; 0 when uncounted
    std::string rating;   // summarystr + " (eff. ...)" + " <-- squeezed out (-N)" or " (uncounted)"
    bool warn = false;    // a squeezed-out row the engine counts (it costs points)
};

struct ActivationDetailsView {
    std::string header;  // "%-6s(%d SP)\t%9s" (+ "\t" and format_ms right-aligned in 9 when difficulty-rated)
    bool difficult = false;
    std::string calibration;    // "Calibration fill: " + format_ms(positive = early); empty when not E-critical
    std::string frontend;       // "Frontend: ..."
    std::string scale_warning;  // the transfer-scale prose; empty when immaterial
    std::string overfill_warning;  // cap-clamped anchor prose; empty when not clamped
    std::vector<TextLine> sqinouts;
    std::vector<BackendRowView> backends;
};

struct ActivationsView {
    std::vector<ActivationDetailsView> acts;
    std::vector<TextLine> footer;  // leftover SP, SP meter, skipped notes
};

// `timing` may be null (no songmeta row): the stored transfer scales are used
// (see rate_activation).
// `backend_limit_ms` hides backend rows beyond +/- that many ms, squeezed-out
// rows excepted; nullopt (the default) shows every stored row.
ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms = std::nullopt,
                                  const core::Rules& rules = core::default_rules());

// The hover hint shown next to a scale warning.
extern const char* const kTransferScaleHint;

// The hover hint shown next to an overfill (cap-clamped) warning.
extern const char* const kOverfillHint;

// ---- score breakdown -------------------------------------------------------

// The eight lines, leading '\n's included (they reproduce the blank lines the
// Python app printed).
std::vector<std::string> build_score_breakdown(const Path& path);

// ---- path list -------------------------------------------------------------

// The right-aligned ms cell of one path row; `ms` empty when the path has no
// difficulty.
struct PathRowView {
    std::string ms;  // "%9.1f ms"
    bool warn = false;
};
PathRowView build_path_row(const Path& path);

struct PathGroupView {
    std::string score_label;  // comma-grouped score heading
    std::vector<const Path*> paths;
};

struct PathListView {
    // Groups in traversal order; the view renders "Optimal Path" before the
    // first and `more_label` before the second.
    std::vector<PathGroupView> groups;
    std::string more_label;  // "More Paths" (+ " (Path limit: N ms)")

    // The all-0 section: shown only when the generated list does not already
    // contain that path (same score AND same notation).
    bool show_allzero = false;
    std::string allzero_label;  // score, plus the delta against optimal
    std::vector<const Path*> allzero;
};
// Pointers into `record`; valid until the record is modified or moved.
PathListView build_path_list(const HydraRecord& record);

// ---- the Paths tab's views, kept between frames -----------------------------

// The Paths tab's views, built once and kept until what they show changes.
// The tab used to rebuild all of them every frame (60 times a second): the
// list, every row's pathstring, every activation's rating. Each view here is
// rebuilt only when its inputs move: the record (by its generation number),
// the selected path, or the two display settings the ratings read. The rules
// are left out of the key because they only change when Hydra restarts.
// Pointers inside point into the record, like build_path_list's.
class PathsTabCache {
public:
    struct Row {
        std::string label;  // the path's pathstring
        PathRowView cell;   // the right-aligned ms cell
    };
    struct Details {
        std::vector<MultSqueezeView> squeezes;
        ActivationsView activations;
        std::vector<std::string> breakdown;
    };

    // The stored-result panel's lines for `lookup`.
    const RecordStatusView& status(const store::RecordLookup& lookup, int record_generation);
    // The path list for `record`, with every listed path's row.
    const PathListView& list(const HydraRecord& record, int record_generation);
    // The row of a path in the last list() (the all-0 section included).
    const Row& row(const Path* path) const;
    // The selected path's squeezes, activations and score breakdown.
    const Details& details(const Path& path, const HydraRecord& record, int record_generation,
                           const SongTiming* timing, double hit_window_ms,
                           std::optional<double> backend_limit_ms, const core::Rules& rules);

    // How many times each view was built; for tests.
    int status_builds() const { return status_builds_; }
    int list_builds() const { return list_builds_; }
    int details_builds() const { return details_builds_; }

private:
    int status_generation_ = -1;
    RecordStatusView status_;
    int status_builds_ = 0;

    int list_generation_ = -1;
    PathListView list_;
    std::unordered_map<const Path*, Row> rows_;
    int list_builds_ = 0;

    int details_generation_ = -1;
    const Path* details_path_ = nullptr;
    double details_hit_window_ms_ = 0.0;
    std::optional<double> details_backend_limit_ms_;
    Details details_;
    int details_builds_ = 0;
};

}  // namespace hydra::app

#endif  // HYDRA_APP_PATH_VIEW_H
