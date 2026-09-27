// Path view-model — the derived strings and flags the Song Details screen
// renders, built here so the derivation is testable without an ImGui frame.
//
// Every string below is exactly what the Paths tab shows; the view layer
// (ui/paths_tab.cpp) only lays these out. The same forms were previously
// composed inline in the draw functions, where no test could reach them.
// Resolve the display facts once, render dumb.
//

#ifndef HYDRA_APP_PATH_VIEW_H
#define HYDRA_APP_PATH_VIEW_H

#include <cstdint>
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

// ---- one measure format ------------------------------------------------------

// A chart position as both tabs print it: "m<measure>.<beat>.<tick-in-beat>",
// measure and beat 1-based, the tick counted from the beat line ("m32.1.0").
// The only measure formatter the Paths and Preview tabs use.
std::string format_measure(const SongTiming& timing, int64_t tick);
// The same for a Timecode that is already resolved, such as an activation's.
std::string format_measure(const Timecode& tc);

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
    int points = 0;  // MultSqueeze::points()
};
std::vector<MultSqueezeView> build_multsqueezes(const HydraRecord& record);

// Beside the "Multiplier squeeze" fold: "none", "+15" for one squeeze, or
// "3 · +45" (how many, then their total) for several.
std::string multsqueeze_summary(const std::vector<MultSqueezeView>& squeezes);

// ---- activations -----------------------------------------------------------

struct BackendRowView {
    std::string timing;   // "%.1f" raw offset
    std::string tooltip;  // effective-ms explanation; empty when none
    std::string chord;
    std::string points;   // what the engine paid for the row; 0 when uncounted
    std::string rating;   // summarystr + " (eff. ...)" + " <-- squeezed out (-N)" or " (uncounted)"
    bool warn = false;    // a squeezed-out row the engine counts (it costs points)
};

// One activation as the Paths tab lists it: a one-line row, and what the row
// shows when it is opened.
struct ActivationRowView {
    // The row.
    int number = 0;              // 1-based; the row's ##act<number> id
    std::string notation;        // Activation::notationstr(), e.g. "3-"
    std::string measure;         // format_measure of the activation, e.g. "m32.1.0"
    int sp_bars = 0;             // bars of SP the activation spends
    std::string bars;            // "3 bars" / "1 bar"
    std::string badge;           // activation_badge(); empty = no badge
    bool difficult = false;      // Activation::is_difficult(): badge and sentences warn-coloured
    // Where the activation falls in the song, 0..1: its onset over the song's
    // length. Unset when the caller passed no timing or no length.
    std::optional<double> song_fraction;

    // The opened row.
    std::string chord;           // Chord::rowstr(), e.g. "[Kick - GreenCym]"
    std::string calibration;    // "Calibration fill: " + format_ms(positive = early); empty when not E-critical
    std::vector<TextLine> squeeze_sentences;  // one per SqIn/SqOut, see squeeze_sentences()
    std::string scale_warning;  // the transfer-scale prose; empty when immaterial
    std::string overfill_warning;  // cap-clamped anchor prose; empty when not clamped
    std::string backends_label;  // "3 notes near the SP end": the rows below, after the backend limit
    std::vector<BackendRowView> backends;
};

struct ActivationsView {
    std::vector<ActivationRowView> acts;
    // Beside the "Activations" heading: "3 · 3 bars each · no SP left over".
    // Empty when the path has no activations.
    std::string summary;
    // The timeline's right-hand label, "m96": the measure the song's length
    // falls in. Empty when there is no timeline (no timing or no length).
    std::string timeline_end;
};

// `timing` may be null (no songmeta row): the stored transfer scales are used
// (see rate_activation).
// `backend_limit_ms` hides backend rows beyond +/- that many ms, squeezed-out
// rows excepted; nullopt (the default) shows every stored row.
// `song_length_ms` is the chart's length for the timeline; with it and a
// `timing`, every row gets its song_fraction and the view its timeline_end.
ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms = std::nullopt,
                                  const core::Rules& rules = core::default_rules(),
                                  std::optional<double> song_length_ms = std::nullopt);

// The badge on an activation row. It shows exactly when the activation needs
// a squeeze, which is when Activation::difficulty() has a value: a SqIn, a
// SqOut, or a required (E0) calibration fill. It names the hardest of them,
// the one difficulty() reports, in whole ms: "squeeze out 163 ms",
// "squeeze in 12 ms", "calibration fill 30 ms". Empty otherwise.
std::string activation_badge(const Activation& act);

// One plain sentence per SqIn/SqOut of `act`, in its order, warn-coloured when
// that squeeze is difficult. `squeezed_out` is the backend row the rating
// flagged as the squeezed-out note (nullptr when the record names none); it
// supplies the SqOut's chord and what the squeeze-out costs.
// `leeway_ms` is Rules::backend_leeway_ms.
std::vector<TextLine> squeeze_sentences(const Activation& act,
                                        const BackendRating* squeezed_out,
                                        double leeway_ms);

// The line above a backend table, explaining its columns in plain words.
extern const char* const kBackendTimingsLead;

// The hover hint shown next to a scale warning.
extern const char* const kTransferScaleHint;

// The hover hint shown next to an overfill (cap-clamped) warning.
extern const char* const kOverfillHint;

// ---- score breakdown -------------------------------------------------------

// The eight lines, leading '\n's included (they reproduce the blank lines the
// Python app printed).
std::vector<std::string> build_score_breakdown(const Path& path);

// ---- path list -------------------------------------------------------------

struct PathGroupView {
    std::string score_label;  // comma-grouped score heading
    std::vector<const Path*> paths;
};

struct PathListView {
    // Groups in traversal order.
    std::vector<PathGroupView> groups;

    // The all-0 section: shown only when the generated list does not already
    // contain that path (same score AND same notation).
    bool show_allzero = false;
    std::string allzero_label;  // score, plus the delta against optimal
    std::vector<const Path*> allzero;
};
// Pointers into `record`; valid until the record is modified or moved.
PathListView build_path_list(const HydraRecord& record);

// ---- the path list as buttons (the Paths tab and the Preview's list) --------

// One path as a button in the list.
struct PathButtonView {
    enum class Group { Optimal, Within, AllZero };
    const Path* path = nullptr;
    Group group = Group::Optimal;
    std::string notation;   // Path::pathstring(), "3- 1 2"
    std::string title;      // "378,315 · 3- 1 2"
    // The line under the title: "hardest squeeze 163.0 ms" when the path has
    // a difficulty, "2,360 below optimal" on the all-0 path, else empty.
    std::string detail;
    bool detail_warn = false;  // Path::is_difficult()
};

struct PathButtonsView {
    // In drawn order; a button's index is its ##path<i> id. Optimal first
    // (every path tied at the best score), then the rest of the generated
    // list, then the all-0 path when build_path_list shows it.
    std::vector<PathButtonView> buttons;
    std::string within_label;  // the heading over the Within group
};

// The heading over the non-optimal paths: "Within 2 scores", "Within 1 score",
// "Within 5,000 points". depth_mode 0 is scores, 1 is points (Settings).
std::string within_label(int depth_mode, int depth_value);

// Pointers into `record`, like build_path_list's. The viewed record is always
// the one stored under the current Score range (records are keyed by it), so
// the caller passes the current Settings::depth_mode and depth_value.
PathButtonsView build_path_buttons(const HydraRecord& record, int depth_mode, int depth_value);

// What the Paths tab has unfolded, and a pending "Show in Preview". Kept on
// AppState through PathsTabCache::ui(), so it dies with the app state.
struct PathsTabUi {
    std::vector<char> act_open;       // one per activation row; char, not vector<bool>
    std::vector<char> backends_open;  // one per activation row
    bool mult_open = false;
    bool breakdown_open = false;
    // The 0-based activation "Show in Preview" asked for, until the Preview
    // tab has moved its playhead there.
    std::optional<size_t> preview_jump;

    // A fresh path: `rows` rows, the first one open, every backend table folded.
    void reset(size_t rows);
    // Every row open; false when there are no rows.
    bool all_open() const;
    // Expand all / Collapse all.
    void set_all(bool open);
    // A click on row i: an open row closes; a closed row opens and every
    // other row closes. Past the end: nothing.
    void click_row(size_t i);
};

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
    struct Details {
        std::vector<MultSqueezeView> squeezes;
        ActivationsView activations;
        std::vector<std::string> breakdown;
    };

    // The stored-result panel's lines for `lookup`.
    const RecordStatusView& status(const store::RecordLookup& lookup, int record_generation);
    // The selected path's squeezes, activations and score breakdown. A new
    // path or record also resets ui() for it; a display setting does not.
    const Details& details(const Path& path, const HydraRecord& record, int record_generation,
                           const SongTiming* timing, double hit_window_ms,
                           std::optional<double> backend_limit_ms, const core::Rules& rules,
                           std::optional<double> song_length_ms = std::nullopt);
    // The path list as buttons, rebuilt when the record or the score range moves.
    const PathButtonsView& buttons(const HydraRecord& record, int record_generation,
                                   int depth_mode, int depth_value);
    // What the Paths tab has unfolded (see PathsTabUi).
    PathsTabUi& ui() { return ui_; }
    const PathsTabUi& ui() const { return ui_; }

    // How many times each view was built; for tests.
    int status_builds() const { return status_builds_; }
    int details_builds() const { return details_builds_; }
    int buttons_builds() const { return buttons_builds_; }

private:
    int status_generation_ = -1;
    RecordStatusView status_;
    int status_builds_ = 0;

    int details_generation_ = -1;
    const Path* details_path_ = nullptr;
    double details_hit_window_ms_ = 0.0;
    std::optional<double> details_backend_limit_ms_;
    std::optional<double> details_song_length_ms_;
    Details details_;
    int details_builds_ = 0;

    int buttons_generation_ = -1;
    int buttons_depth_mode_ = -1;
    int buttons_depth_value_ = -1;
    PathButtonsView buttons_;
    int buttons_builds_ = 0;

    PathsTabUi ui_;
};

}  // namespace hydra::app

#endif  // HYDRA_APP_PATH_VIEW_H
