#include "app/path_view.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace hydra::app {

namespace {

std::string measurestr(const Timecode& tc) {
    const int64_t* mbt = tc.measure_beats_ticks();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "m%lld.%lld.%lld", (long long)mbt[0] + 1,
                  (long long)mbt[1] + 1, (long long)mbt[2]);
    return buf;
}

}  // namespace

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

std::vector<MultSqueezeView> build_multsqueezes(const Path& path) {
    std::vector<MultSqueezeView> out;
    out.reserve(path.multsqueezes.size());
    for (const MultSqueeze& msq : path.multsqueezes) {
        MultSqueezeView v;
        v.label = msq.notationstr() + "   (+" + std::to_string(msq.points()) +
                  " pts):   " + msq.chord().rowstr();
        v.howto = msq.howto();
        out.push_back(std::move(v));
    }
    return out;
}

const char* const kTransferScaleHint =
    "SP length is measured in measures, so frontend timing\n"
    "reaches the SP end scaled by the measure-length ratio.\n"
    "Early and late hits scale differently when the activation\n"
    "or SP end sits exactly on a signature or tempo change.";

ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms) {
    ActivationsView view;
    const double W = hit_window_ms;

    for (const Activation& act : path.all_activations()) {
        ActivationDetailsView av;

        // Mirrors hydra_app.py:951-953 exactly, including the literal tabs:
        // f"{notationstr:6}({sp_meter} SP)\t{measurestr:>9}" and (when
        // difficult) f"\t{ms:7.1f}ms". ImGui's '\t' is a fixed 4-space
        // advance (IM_TABSIZE) shared with DearPyGui, so the columns line up
        // identically to the Python app.
        std::string ntn = act.notationstr();
        std::string meas = act.timecode ? measurestr(*act.timecode) : "";
        char hbuf[128];
        std::snprintf(hbuf, sizeof(hbuf), "%-6s(%d SP)\t%9s", ntn.c_str(),
                      act.sp_meter.value_or(0), meas.c_str());
        av.header = hbuf;
        if (auto ms = act.difficulty()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "\t%7.1fms", *ms);
            av.header += buf;
        }
        av.difficult = act.is_difficult();

        if (act.is_e_critical()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Calibration fill: %.1fms (%s)",
                          *act.e_offset, act.is_E0() ? "required" : "optional");
            av.calibration = buf;
        }

        av.frontend =
            "Frontend: " + (act.chord ? act.chord->rowstr() : std::string("None"));

        ActivationRating rate = rate_activation(act, timing, W);

        // The line prints the scale(s) that actually tripped the warn:
        // backend rows are judged at the post (deact-node) end, SqIn/SqOut
        // phrase notes at the pre (pre-extension) end -- different numbers
        // when a SqIn extended SP. A scale that still displays as x1.00 has
        // nothing to say (a bare over-budget gap trips materiality too), so
        // it is dropped; with no claims left there is no line at all.
        struct ScaleClaim { double r; bool late; bool note; };
        std::vector<ScaleClaim> claims;
        auto claim = [&claims](double r, bool late, bool note) {
            if (std::abs(r - 1.0) < 0.005) return;  // renders as x1.00
            for (const ScaleClaim& c : claims)
                if (c.late == late && std::abs(c.r - r) <= 0.005) return;
            claims.push_back({r, late, note});
        };
        if (rate.late_backend_warns) claim(rate.scales.post.late, true, false);
        if (rate.late_note_warns) claim(rate.scales.pre.late, true, true);
        if (rate.early_backend_warns) claim(rate.scales.post.early, false, false);
        if (rate.early_note_warns) claim(rate.scales.pre.early, false, true);

        // A note claim needs its end named only when the post end disagrees.
        auto ends_agree = [&rate](const ScaleClaim& c) {
            double post = c.late ? rate.scales.post.late : rate.scales.post.early;
            return std::abs(c.r - post) <= 0.005;
        };

        char buf[192];
        if (claims.size() == 1) {
            const ScaleClaim& c = claims[0];
            if (!c.note || ends_agree(c)) {
                std::snprintf(buf, sizeof(buf),
                              "Frontend timing scales x%.2f to the SP end: "
                              "%s at the frontend moves the SP end %s%s%.1fms.",
                              c.r, c.late ? "+10ms (late)" : "-10ms (early)",
                              c.r < 1.0 ? "only " : "", c.late ? "+" : "-",
                              10.0 * c.r);
            } else {
                std::snprintf(buf, sizeof(buf),
                              "Frontend timing scales x%.2f at the %s's SP end: "
                              "%s at the frontend moves that end %s%s%.1fms.",
                              c.r, c.late ? "SqIn" : "SqOut",
                              c.late ? "+10ms (late)" : "-10ms (early)",
                              c.r < 1.0 ? "only " : "", c.late ? "+" : "-",
                              10.0 * c.r);
            }
            av.scale_warning = buf;
        } else if (claims.size() == 2 && claims[0].late != claims[1].late &&
                   ends_agree(claims[0]) && ends_agree(claims[1])) {
            const ScaleClaim& lc = claims[0].late ? claims[0] : claims[1];
            const ScaleClaim& ec = claims[0].late ? claims[1] : claims[0];
            if (std::abs(lc.r - ec.r) > 0.005) {
                std::snprintf(buf, sizeof(buf),
                              "Frontend timing scales x%.2f (late) / x%.2f "
                              "(early) at the SP end.",
                              lc.r, ec.r);
            } else {
                std::snprintf(buf, sizeof(buf),
                              "Frontend timing scales x%.2f to the SP end: "
                              "10ms at the frontend moves the SP end %s%.1fms.",
                              lc.r, lc.r < 1.0 ? "only " : "", 10.0 * lc.r);
            }
            av.scale_warning = buf;
        } else if (!claims.empty()) {
            std::string parts;
            for (const ScaleClaim& c : claims) {
                std::snprintf(buf, sizeof(buf), "x%.2f (%s, %s)", c.r,
                              c.late ? "late" : "early",
                              c.note ? (c.late ? "SqIn" : "SqOut") : "backends");
                if (!parts.empty()) parts += " / ";
                parts += buf;
            }
            av.scale_warning = "Frontend timing scales " + parts + ".";
        }

        for (const SPSqueeze& sq : act.sqinouts)
            av.sqinouts.push_back({sq.description(), sq.is_difficult()});

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
                std::snprintf(tip, sizeof(tip),
                              "Effectively %.1fms on the normal %.0fms scale:\n"
                              "frontend timing scales x%.2f here, so the combined\n"
                              "squeeze budget is %.0fms, not %.0fms.",
                              *br.effective_ms, 2.0 * W, br.scale,
                              br.budget_ms, 2.0 * W);
                row.tooltip = tip;
            }
            row.chord = bsq.chord.notationstr();
            row.points =
                std::to_string(br.squeezed_out ? bsq.sqout_points : bsq.points);
            row.rating = bsq.summarystr(W);
            if (br.effective_ms) {
                char effbuf[32];
                std::snprintf(effbuf, sizeof(effbuf), " (eff. %.1fms)",
                              *br.effective_ms);
                row.rating += effbuf;
            }
            if (br.squeezed_out) {
                char extra[48];
                std::snprintf(extra, sizeof(extra), " <-- squeezed out (-%d)",
                              bsq.points - bsq.sqout_points);
                row.rating += extra;
                row.warn = true;
            }
            av.backends.push_back(std::move(row));
        }

        view.acts.push_back(std::move(av));
    }

    view.footer.push_back(
        {"Leftover SP: " + std::to_string(path.leftover_sp) + ".", false});

    // Which SP ceiling this result was found under. Mirrors
    // hydra_app.py:1017-1031 (warning-colored when an Auto run ran out of
    // time before the score settled).
    if (record.sp_cap) {
        std::string bars = std::to_string(*record.sp_cap);
        if (record.sp_cap_converged) {
            view.footer.push_back({"SP meter: " + bars + " bars.", false});
        } else {
            view.footer.push_back(
                {"SP meter: " + bars +
                     " bars. The search ran out of time before the score "
                     "settled, so a higher meter may still score more.",
                 true});
        }
    }

    if (path.skipped_accents > 0)
        view.footer.push_back(
            {"This path has " + std::to_string(path.skipped_accents) +
                 " skipped (unhittable) accent" +
                 (path.skipped_accents == 1 ? "" : "s") + "!",
             true});
    if (path.skipped_ghosts > 0)
        view.footer.push_back(
            {"This path has " + std::to_string(path.skipped_ghosts) +
                 " skipped (unhittable) ghost" +
                 (path.skipped_ghosts == 1 ? "" : "s") + "!",
             true});
    return view;
}

std::vector<std::string> build_score_breakdown(const Path& path) {
    std::vector<std::string> lines;
    // hydra_app.py:1044 formats the average as (str(avg_mult()) + "000")[:5]
    // -- a string slice, which TRUNCATES to three decimals rather than
    // rounding (%.3f would round). %.10f gives a long-enough decimal
    // expansion; slicing its first five chars reproduces Python exactly.
    char avgbuf[32];
    std::snprintf(avgbuf, sizeof(avgbuf), "%.10f", path.avg_mult());
    std::string avgs = (std::string(avgbuf) + "000").substr(0, 5);
    lines.push_back("Avg. Multiplier:      " + avgs + "x");

    // Leading '\n' on Notes and Total Score reproduces the blank lines
    // hydra_app.py:1046,1058 add; Python uses no separator between them.
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

PathRowView build_path_row(const Path& path) {
    PathRowView row;
    if (auto diff = path.difficulty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%9.1f ms", *diff);
        row.ms = buf;
        row.warn = *diff > kDifficultMs;
    }
    return row;
}

PathListView build_path_list(const HydraRecord& record) {
    PathListView view;
    std::vector<const Path*> flat = record.all_paths();

    // Every unique score along the traversal gets its own group, mirroring
    // hydra_app.py's dpg.add_tree_node(label=f"{current_score:,}") grouping.
    int64_t current_score = INT64_MIN;
    for (const Path* p : flat) {
        if (p->totalscore() != current_score) {
            current_score = p->totalscore();
            view.groups.push_back({group_thousands(current_score), {}});
        }
        view.groups.back().paths.push_back(p);
    }

    view.more_label = "More Paths";
    if (record.ms_limit)
        view.more_label +=
            " (Path limit: " + std::to_string((int)*record.ms_limit) + " ms)";

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

}  // namespace hydra::app
