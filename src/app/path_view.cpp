#include "app/path_view.h"
#include "app/display_format.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>

#include "core/backend_value.h"

namespace hydra::app {

namespace {

std::string bars_text(int bars) {
    return std::to_string(bars) + (bars == 1 ? " bar" : " bars");
}

// The separator the new labels use: " · " (U+00B7 in UTF-8).
const char* const kDot = " \xC2\xB7 ";

}  // namespace

std::string format_measure(const Timecode& tc) {
    const int64_t* mbt = tc.measure_beats_ticks();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "m%lld.%lld.%lld", (long long)mbt[0] + 1,
                  (long long)mbt[1] + 1, (long long)mbt[2]);
    return buf;
}

std::string format_measure(const SongTiming& timing, int64_t tick) {
    return format_measure(timing.timecode(tick));
}

std::string activation_badge(const Activation& act) {
    std::optional<double> hardest = act.difficulty();
    // An E activation that skips fills has an optional early fill, which
    // difficulty() leaves out. It still gets the badge: its timing decides
    // whether the first fill shows up, and so how the skips are counted.
    if (!hardest && act.is_e_critical()) hardest = act.e_difficulty(/*verbose=*/true);
    if (!hardest) return {};
    // difficulty() is the max over the SqIns/SqOuts and a required fill, so
    // the squeeze that produced it compares equal; a tie names the squeeze.
    const char* what = "early fill";
    for (const SPSqueeze& sq : act.sqinouts)
        if (sq.difficulty() == *hardest) {
            what = sq.kind == SqueezeKind::SqIn ? "squeeze in" : "squeeze out";
            break;
        }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.0f ms", what, *hardest);
    return buf;
}

std::vector<TextLine> squeeze_sentences(const Activation& act,
                                        const BackendRating* squeezed_out,
                                        double leeway_ms,
                                        const std::vector<std::optional<double>>& note_effective_ms) {
    std::vector<TextLine> out;
    for (size_t i = 0; i < act.sqinouts.size(); ++i) {
        const SPSqueeze& sq = act.sqinouts[i];
        // timing() is the edge SPSqueeze::description() prints: a SqOut must
        // be hit later than it, a SqIn earlier than it.
        const double t = sq.timing();
        std::string text;
        if (sq.kind == SqueezeKind::SqOut) {
            const std::string note =
                squeezed_out ? "the " + squeezed_out->row.chord.notationstr() + " note"
                             : std::string("the SP phrase's last note");
            const std::string when = t >= 0.0
                                         ? "more than " + format_ms_spaced(std::fabs(t)) + " late"
                                         : "no more than " + format_ms_spaced(std::fabs(t)) + " early";
            text = "Hit " + note + " " + when + " so it lands after Star Power ends.";
            if (squeezed_out) {
                // What the squeeze-out costs, from the same function the
                // search prices the row with (the table's "(-N)").
                const BackendSqueeze& row = squeezed_out->row;
                const double off = row.offset_ms.value_or(0.0);
                if (core::counted_without_squeeze(off, leeway_ms)) {
                    const int value = core::backend_row_value(
                        off, row.points, row.sqout_points, core::SqOutPosition::Exact, leeway_ms);
                    const int lost = row.points - value;
                    text += lost > 0 ? " It scores " + std::to_string(lost) +
                                           " fewer points, and its SP phrase banks for later."
                                     : std::string(" It costs no points, and its SP phrase "
                                                   "banks for later.");
                } else {
                    text += " It costs no points, because Hydra's score never counted that "
                            "note under Star Power, and its SP phrase banks for later.";
                }
            } else {
                text += " Its SP phrase banks for later.";
            }
        } else {
            std::string when = t <= 0.0
                                   ? "more than " + format_ms_spaced(std::fabs(t)) + " early"
                                   : "no more than " + format_ms_spaced(std::fabs(t)) + " late";
            // A SqIn has no backend row to carry its eff. figure (a SqOut's
            // sits on its squeezed-out row), so the sentence carries it.
            if (i < note_effective_ms.size() && note_effective_ms[i])
                when += " (eff. " + format_ms_spaced(*note_effective_ms[i]) + ")";
            text = "Hit the SP phrase's last note " + when +
                   " so it lands before Star Power ends. The phrase then counts while Star "
                   "Power runs, which makes Star Power last longer.";
        }
        out.push_back({std::move(text), sq.is_difficult()});
    }
    return out;
}

const char* const kBackendTimingsLead =
    "Timing is how far each note sits from the Star Power end, in ms; negative is "
    "before it. Points is what this path scores for the note under Star Power. A note "
    "marked (uncounted) lands outside Star Power unless it is squeezed in, so this "
    "path's score leaves it out.";

RecordStatusView build_record_status(const store::RecordLookup& lookup) {
    RecordStatusView view;
    view.state = lookup.status;
    if (view.state != store::RecordStatus::Ready || !lookup.record) return view;

    const HydraRecord& record = *lookup.record;
    // A Ready record can legitimately hold nothing -- the chart was analyzed
    // and no path survived. Say so instead of asking for a best path.
    if (record.paths.empty()) {
        view.lines.push_back("No paths found.");
        return view;
    }
    view.lines.push_back("Best score:  " +
                         group_thousands(record.best_path().totalscore()));
    view.lines.push_back("Paths kept:  " +
                         std::to_string((int)record.all_paths().size()));
    if (record.ms_limit)
        view.lines.push_back("Path limit:  " +
                             std::to_string((int)*record.ms_limit) + " ms");
    else
        view.lines.push_back("Path limit:  off");
    if (record.sp_cap)
        view.lines.push_back("SP cap:  " + std::to_string(*record.sp_cap) + " bars");
    return view;
}

std::vector<MultSqueezeView> build_multsqueezes(const HydraRecord& record) {
    std::vector<MultSqueezeView> out;
    out.reserve(record.multsqueezes.size());
    for (const MultSqueeze& msq : record.multsqueezes) {
        MultSqueezeView v;
        v.label = msq.notationstr() + "   (+" + std::to_string(msq.points()) +
                  " pts):   " + msq.chord().rowstr();
        v.howto = msq.howto();
        v.points = msq.points();
        out.push_back(std::move(v));
    }
    return out;
}

std::string multsqueeze_summary(const std::vector<MultSqueezeView>& squeezes) {
    if (squeezes.empty()) return "none";
    int total = 0;
    for (const MultSqueezeView& s : squeezes) total += s.points;
    const std::string sum = "+" + group_thousands(total);
    return squeezes.size() == 1 ? sum : std::to_string(squeezes.size()) + kDot + sum;
}

const char* const kTransferScaleHint =
    "SP length is measured in measures, so frontend timing\n"
    "reaches the SP end scaled by the measure-length ratio.\n"
    "Early and late hits scale differently when the activation\n"
    "or SP end sits exactly on a signature or tempo change.";

const char* const kOverfillHint =
    "SP lasts a set number of measures from the note it is\n"
    "tied to -- normally the activation. When a mid-SP phrase\n"
    "fills the meter to the cap, the end is measured from that\n"
    "phrase's last note instead, so that note's timing is what\n"
    "moves the end. The ms figures here are still measured at\n"
    "the SP end; only the note you move them with changes.";

// `record` fed the old footer's "SP meter" line, gone with the old Paths tab;
// the parameter stays so the callers keep their shape.
ActivationsView build_activations(const Path& path, const HydraRecord& /*record*/,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms,
                                  const core::Rules& rules,
                                  std::optional<double> song_length_ms) {
    ActivationsView view;
    const double W = hit_window_ms;

    const ActivationWalk acts = path.walk_activations();
    for (const Activation& act : acts) {
        ActivationRowView av;

        std::string ntn = act.notationstr();
        std::string meas = format_measure(act.timecode);
        av.difficult = act.is_difficult();
        av.number = static_cast<int>(view.acts.size()) + 1;
        av.notation = ntn;
        av.measure = meas;
        av.sp_bars = act.sp_meter;
        av.bars = bars_text(act.sp_meter);
        av.badge = activation_badge(act);
        av.chord = act.chord.rowstr();
        if (timing && song_length_ms && *song_length_ms > 0.0) {
            const double at = timing->timecode(act.timecode.ticks()).ms();
            av.song_fraction = std::clamp(at / *song_length_ms, 0.0, 1.0);
        }

        if (act.is_e_critical()) {
            // Positive = hit early, the same sign as the report:
            // e_difficulty(true) is -e_offset for every E-critical activation.
            av.early_fill = "Early fill: " +
                             format_ms(*act.e_difficulty(/*verbose=*/true)) +
                             (act.is_E0() ? " (required)" : " (optional)");
        }

        ActivationRating rate = rate_activation(act, W, rules.backend_leeway_ms);

        // The line shows every scale that isn't x1.00, early first. Backend
        // rows are judged at the post (deact-node) end, SqIn/SqOut phrase
        // notes at the pre (pre-extension) end; the two differ only when a
        // SqIn extended SP, and then the SqIn's end gets its own clause.
        // 0.005 is half the last digit of %.2f: a scale within it prints as
        // x1.00, and two scales within it print the same.
        auto shows = [](double r) { return std::abs(r - 1.0) >= 0.005; };
        auto same = [](double a, double b) { return std::abs(a - b) < 0.005; };
        auto sides = [&shows](const TransferScale& s) {
            char buf[64];
            std::string out;
            if (shows(s.early)) {
                std::snprintf(buf, sizeof(buf), "x%.2f (early)", s.early);
                out = buf;
            }
            if (shows(s.late)) {
                std::snprintf(buf, sizeof(buf), "x%.2f (late)", s.late);
                if (!out.empty()) out += " / ";
                out += buf;
            }
            return out;
        };
        const TransferScale& post = rate.scales.post;
        const TransferScale& pre = rate.scales.pre;
        const std::string post_part = sides(post);
        const bool pre_differs = !same(pre.early, post.early) || !same(pre.late, post.late);
        const std::string pre_part = pre_differs ? sides(pre) : std::string();
        std::string clauses;
        if (!post_part.empty()) clauses = post_part + " at the SP end";
        if (!pre_part.empty()) {
            if (!clauses.empty()) clauses += "; ";
            clauses += pre_part + " at the SqIn's SP end";
        }
        if (!clauses.empty()) av.scale_warning = "Frontend timing scales " + clauses + ".";
        // Orange only when a shown scale moves a figure on screen.
        av.scale_warn = (rate.late_backend_warns && shows(post.late)) ||
                        (rate.early_backend_warns && shows(post.early)) ||
                        (rate.late_note_warns && shows(pre.late)) ||
                        (rate.early_note_warns && shows(pre.early));

        // When the SP window was cap-clamped and this activation lists a
        // squeeze the frontend decides, warn that the anchor is the
        // collecting note, not the activation.
        if (rate.cap_clamped) {
            if (timing && act.clamp_tick) {
                av.overfill_warning =
                    "SP overfilled at " + format_measure(*timing, *act.clamp_tick);
            } else {
                av.overfill_warning = "SP overfilled";
            }
        }

        const BackendRating* squeezed_out = nullptr;
        for (const BackendRating& br : rate.backends)
            if (br.squeezed_out) squeezed_out = &br;
        av.squeeze_sentences = squeeze_sentences(act, squeezed_out, rules.backend_leeway_ms,
                                                 rate.note_effective_ms);

        av.backends.reserve(rate.backends.size());
        for (const BackendRating& br : rate.backends) {
            const BackendSqueeze& bsq = br.row;

            // The display window the user asked for. It only drops rows from
            // the table: the ratings above (and the warns feeding the scale
            // line) still read every stored row. A squeezed-out note is the
            // reason the row matters, so it always shows.
            if (backend_limit_ms && !br.squeezed_out &&
                std::fabs(bsq.offset_ms.value_or(0.0)) > *backend_limit_ms)
                continue;

            BackendRowView row;
            char tbuf[32];
            std::snprintf(tbuf, sizeof(tbuf), "%.1f", bsq.offset_ms.value_or(0.0));
            row.timing = tbuf;
            if (br.effective_ms) {
                char tip[256];
                // The budget at identity scale (x1.00): what the combined
                // budget would be with no frontend-timing scale.
                const double normal_budget = squeeze_budget_ms(1.0, W);
                std::snprintf(tip, sizeof(tip),
                              "Effectively %.1fms on the normal %.0fms scale:\n"
                              "frontend timing scales x%.2f here, so the combined\n"
                              "squeeze budget is %.0fms, not %.0fms.",
                              *br.effective_ms, normal_budget, br.scale,
                              br.budget_ms, normal_budget);
                row.tooltip = tip;
            }
            row.chord = bsq.chord.notationstr();
            // What the engine actually paid for this row on this path, from
            // the same function the search calls. display_backends already
            // dropped every row past the squeezed-out chord, so a row here is
            // either that chord or priced as if nothing was squeezed out.
            const double off = bsq.offset_ms.value_or(0.0);
            const bool counted =
                core::counted_without_squeeze(off, rules.backend_leeway_ms);
            const int value = core::backend_row_value(
                off, bsq.points, bsq.sqout_points,
                br.squeezed_out ? core::SqOutPosition::Exact
                                : core::SqOutPosition::NoSqOut,
                rules.backend_leeway_ms);
            row.points = std::to_string(value);
            row.rating = bsq.summarystr(W, rules.backend_leeway_ms);
            if (br.effective_ms) {
                char effbuf[32];
                std::snprintf(effbuf, sizeof(effbuf), " (eff. %.1fms)",
                              *br.effective_ms);
                row.rating += effbuf;
            }
            if (br.squeezed_out) {
                // "(-N)" and the warning colour only when the squeeze-out
                // really costs points. A row the engine never counted costs
                // nothing either way (user decisions 1 and 19).
                char extra[48];
                if (counted) {
                    std::snprintf(extra, sizeof(extra),
                                  " <-- squeezed out (-%d)", bsq.points - value);
                    row.warn = true;
                } else {
                    std::snprintf(extra, sizeof(extra),
                                  " <-- squeezed out (uncounted)");
                }
                row.rating += extra;
            }
            av.backends.push_back(std::move(row));
        }
        const size_t shown = av.backends.size();
        av.backends_label = std::to_string(shown) + (shown == 1 ? " note" : " notes") +
                            " near the SP end";

        view.acts.push_back(std::move(av));
    }

    // The line beside the heading: how many, and what is left.
    if (!view.acts.empty()) {
        const std::string left = path.leftover_sp == 0
                                     ? std::string("no SP left over")
                                     : bars_text(path.leftover_sp) + " of SP left over";
        view.summary = std::to_string(view.acts.size()) + kDot + left;
    }
    if (timing && song_length_ms && *song_length_ms > 0.0) {
        const int64_t end_tick = std::llround(timing->ms_index().tick_at_ms(*song_length_ms));
        view.timeline_end =
            "m" + std::to_string((long long)timing->timecode(end_tick).measure_beats_ticks()[0] + 1);
    }

    return view;
}

std::vector<std::string> build_score_breakdown(const Path& path) {
    std::vector<std::string> lines;
    // Rounded, not truncated, so it matches the report's mult column.
    lines.push_back("Avg. Multiplier:      " + format_avg_mult(path.avg_mult()) + "x");

    // Leading '\n' on Notes and Total Score puts a blank line above
    // each; there is no other separator.
    auto right10 = [](int64_t v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%10s", group_thousands(v).c_str());
        return std::string(buf);
    };
    lines.push_back("\nNotes:            " + right10(path.score_base));
    lines.push_back("Combo Bonus:      " + right10(path.score_combo));
    lines.push_back("Star Power:       " + right10(path.score_sp));
    lines.push_back("Solo Bonus:       " + right10(path.score_solo));
    lines.push_back("Accent Notes:     " + right10(path.score_accents));
    lines.push_back("Ghost Notes:      " + right10(path.score_ghosts));
    lines.push_back("\nTotal Score:      " + right10(path.totalscore()));
    return lines;
}

PathListView build_path_list(const HydraRecord& record) {
    PathListView view;
    std::vector<const Path*> flat = record.all_paths();

    // Every unique score along the traversal gets its own group, labelled
    // with the score in thousands-separated digits.
    int64_t current_score = INT64_MIN;
    for (const Path* p : flat) {
        if (p->totalscore() != current_score) {
            current_score = p->totalscore();
            view.groups.push_back({group_thousands(current_score), {}});
        }
        view.groups.back().paths.push_back(p);
    }

    // The all-0 section is only worth showing when the generated list does
    // not already contain that path: same score and same notation is the same
    // path. Score alone would hide a genuinely different all-0 path that ties
    // some listed path, and notation alone would hide one that reads the same
    // but banks differently.
    view.allzero = record.all_allzero_paths();
    if (view.allzero.empty()) return view;
    for (const Path* z : view.allzero) {
        const int64_t score = z->totalscore();
        const std::string notation = z->pathstring();
        for (const Path* p : flat)
            if (p->totalscore() == score && p->pathstring() == notation)
                return view;
    }
    view.show_allzero = true;

    const int64_t score = view.allzero.front()->totalscore();
    view.allzero_label = group_thousands(score);
    // The first thing a user asks of this row is what it costs against the
    // optimal path, so answer it in the header.
    if (!flat.empty()) {
        const int64_t delta = score - flat.front()->totalscore();
        if (delta != 0)
            view.allzero_label += "   (" + std::string(delta > 0 ? "+" : "-") +
                                  group_thousands(delta < 0 ? -delta : delta) +
                                  ")";
    }
    return view;
}

std::string within_label(int depth_mode, int depth_value) {
    const bool points = depth_mode == 1;
    const std::string n = points ? group_thousands(depth_value) : std::to_string(depth_value);
    const char* unit = points ? (depth_value == 1 ? " point" : " points")
                              : (depth_value == 1 ? " score" : " scores");
    return "Within " + n + unit;
}

PathButtonsView build_path_buttons(const HydraRecord& record, int depth_mode, int depth_value) {
    PathButtonsView view;
    view.within_label = within_label(depth_mode, depth_value);
    const PathListView list = build_path_list(record);
    const int64_t best = record.paths.empty() ? 0 : record.best_path().totalscore();

    auto add = [&](const Path* p, PathButtonView::Group group) {
        PathButtonView b;
        b.path = p;
        b.group = group;
        b.notation = p->pathstring();
        b.title = group_thousands(p->totalscore()) + kDot + b.notation;
        if (std::optional<double> hardest = p->difficulty()) {
            b.timing = format_ms_spaced(*hardest);
            b.timing_warn = p->is_difficult();
        }
        if (group == PathButtonView::Group::AllZero) {
            // What the all-0 path costs against the optimal one.
            const int64_t delta = p->totalscore() - best;
            if (delta < 0) b.detail = group_thousands(-delta) + " below optimal";
            if (delta > 0) b.detail = group_thousands(delta) + " above optimal";
        }
        view.buttons.push_back(std::move(b));
    };
    for (size_t g = 0; g < list.groups.size(); ++g)
        for (const Path* p : list.groups[g].paths)
            add(p, g == 0 ? PathButtonView::Group::Optimal : PathButtonView::Group::Within);
    if (list.show_allzero)
        for (const Path* p : list.allzero) add(p, PathButtonView::Group::AllZero);
    return view;
}

void PathsTabUi::reset(size_t rows) {
    act_open.assign(rows, 0);
    backends_open.assign(rows, 0);
    if (rows > 0) act_open[0] = 1;
}

bool PathsTabUi::all_open() const {
    if (act_open.empty()) return false;
    for (char open : act_open)
        if (!open) return false;
    return true;
}

void PathsTabUi::set_all(bool open) {
    std::fill(act_open.begin(), act_open.end(), static_cast<char>(open ? 1 : 0));
}

void PathsTabUi::click_row(size_t i) {
    if (i >= act_open.size()) return;
    if (act_open[i]) {
        act_open[i] = 0;
        return;
    }
    std::fill(act_open.begin(), act_open.end(), static_cast<char>(0));
    act_open[i] = 1;
}

const RecordStatusView& PathsTabCache::status(const store::RecordLookup& lookup,
                                               int record_generation) {
    if (record_generation != status_generation_) {
        status_ = build_record_status(lookup);
        status_generation_ = record_generation;
        ++status_builds_;
    }
    return status_;
}

const PathsTabCache::Details& PathsTabCache::details(
    const Path& path, const HydraRecord& record, int record_generation,
    const SongTiming* timing, double hit_window_ms, std::optional<double> backend_limit_ms,
    const core::Rules& rules, std::optional<double> song_length_ms) {
    const bool new_path = record_generation != details_generation_ || &path != details_path_;
    if (new_path || hit_window_ms != details_hit_window_ms_ ||
        backend_limit_ms != details_backend_limit_ms_ ||
        song_length_ms != details_song_length_ms_) {
        details_.squeezes = build_multsqueezes(record);
        details_.activations = build_activations(path, record, timing, hit_window_ms,
                                                 backend_limit_ms, rules, song_length_ms);
        details_.breakdown = build_score_breakdown(path);
        details_generation_ = record_generation;
        details_path_ = &path;
        details_hit_window_ms_ = hit_window_ms;
        details_backend_limit_ms_ = backend_limit_ms;
        details_song_length_ms_ = song_length_ms;
        // What is unfolded belongs to the path; a display setting keeps it.
        if (new_path) ui_.reset(details_.activations.acts.size());
        ++details_builds_;
    }
    return details_;
}

const PathButtonsView& PathsTabCache::buttons(const HydraRecord& record, int record_generation,
                                              int depth_mode, int depth_value) {
    if (record_generation != buttons_generation_ || depth_mode != buttons_depth_mode_ ||
        depth_value != buttons_depth_value_) {
        buttons_ = build_path_buttons(record, depth_mode, depth_value);
        buttons_generation_ = record_generation;
        buttons_depth_mode_ = depth_mode;
        buttons_depth_value_ = depth_value;
        ++buttons_builds_;
    }
    return buttons_;
}

}  // namespace hydra::app
