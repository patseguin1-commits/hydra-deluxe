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
std::vector<MultSqueezeView> build_multsqueezes(const Path& path);

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

}  // namespace hydra::app

#endif  // HYDRA_APP_PATH_VIEW_H
