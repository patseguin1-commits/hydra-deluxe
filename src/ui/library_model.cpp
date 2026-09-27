#include "ui/library_model.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include "core/model.h"  // group_thousands

namespace hydra::ui {

namespace {

StatusChip chip_of(store::RecordStatus status) {
    switch (status) {
        case store::RecordStatus::Ready: return StatusChip::Analyzed;
        case store::RecordStatus::Stale: return StatusChip::Stale;
        case store::RecordStatus::NotAnalyzed: break;
    }
    return StatusChip::NotAnalyzed;
}

// Where a row with no score sorts under Best path, the same in both
// directions: a Ready result with no paths, then Stale, then Not analyzed.
int unscored_rank(store::RecordStatus status) {
    switch (status) {
        case store::RecordStatus::Ready: return 0;
        case store::RecordStatus::Stale: return 1;
        case store::RecordStatus::NotAnalyzed: break;
    }
    return 2;
}

// What a stars: or squeeze filter may test. Only a Ready result's numbers
// count; anything else has no facts, so those filters never match it.
app::RowFacts facts_of(const LibraryRow& row) {
    if (row.status != store::RecordStatus::Ready) return app::RowFacts{};
    return app::RowFacts{row.summary.stars, row.summary.hardest_ms};
}

bool matches_query(const app::LibraryQuery& q, const LibraryRow& row) {
    return q.empty() || app::query_matches(q, row.searchable, facts_of(row));
}

// Applies one lookup to one row. False when nothing the library shows or
// filters on changed, so a refresh that finds the same answers costs no
// re-sort.
bool apply_summary(LibraryRow& row, const store::SummaryLookup& lookup) {
    if (row.status == lookup.status && row.bestpath == lookup.bestpath &&
        row.summary.score == lookup.summary.score && row.summary.stars == lookup.summary.stars &&
        row.summary.hardest_ms == lookup.summary.hardest_ms)
        return false;
    row.status = lookup.status;
    row.bestpath = lookup.bestpath;
    row.summary = lookup.summary;
    row.best_label = best_path_label(row.status, row.bestpath, row.summary);
    return true;
}

}  // namespace

std::string best_path_label(store::RecordStatus status, const std::string& bestpath,
                            const store::PathSummary& summary) {
    switch (status) {
        case store::RecordStatus::Stale: return "Stale";
        case store::RecordStatus::NotAnalyzed: return "Not analyzed";
        case store::RecordStatus::Ready: break;
    }
    // A Ready result with no paths has no score; its cell shows its path
    // string (empty), as the table always has.
    if (!summary.score) return bestpath;
    return group_thousands(*summary.score) + "  " + bestpath;
}

size_t ChipCounts::of(StatusChip chip) const {
    switch (chip) {
        case StatusChip::NotAnalyzed: return not_analyzed;
        case StatusChip::Stale: return stale;
        case StatusChip::Analyzed: return analyzed;
        case StatusChip::All: break;
    }
    return all;
}

void LibraryModel::set_charts(std::vector<store::ChartLibraryEntry> charts) {
    rows_.clear();
    rows_.reserve(charts.size());
    for (store::ChartLibraryEntry& entry : charts) {
        LibraryRow row;
        row.title = app::strip_rich_tags(entry.title);
        row.artist = app::strip_rich_tags(entry.artist);
        row.charter = app::strip_rich_tags(entry.charter);
        row.searchable =
            app::make_searchable(entry.title, entry.artist, entry.charter, entry.rootfolder);
        row.entry = std::move(entry);
        row.best_label = best_path_label(row.status, row.bestpath, row.summary);
        rows_.push_back(std::move(row));
    }
    resort();
    refilter();
}

std::vector<std::string> LibraryModel::hashes() const {
    std::vector<std::string> out;
    out.reserve(rows_.size());
    for (const LibraryRow& row : rows_) out.push_back(row.entry.md5);
    return out;
}

size_t LibraryModel::set_summaries(const std::vector<store::SummaryLookup>& lookups) {
    size_t changed = 0;
    const size_t n = std::min(rows_.size(), lookups.size());
    for (size_t i = 0; i < n; ++i)
        if (apply_summary(rows_[i], lookups[i])) ++changed;
    if (changed > 0) summaries_changed();
    return changed;
}

size_t LibraryModel::set_summary_for(const std::string& md5, const store::SummaryLookup& lookup) {
    size_t changed = 0;
    for (LibraryRow& row : rows_)
        if (row.entry.md5 == md5 && apply_summary(row, lookup)) ++changed;
    if (changed > 0) summaries_changed();
    return changed;
}

void LibraryModel::summaries_changed() {
    // A new score moves a row only under the Best path sort; a new status
    // can move it between chips and in or out of a stars:/squeeze filter.
    if (sort_column_ == LibrarySort::BestPath) resort();
    refilter();
}

void LibraryModel::set_query(std::string_view text) {
    if (text == query_text_) return;
    query_text_.assign(text.data(), text.size());
    query_ = app::parse_library_query(text);
    refilter();
}

void LibraryModel::set_chip(StatusChip chip) {
    if (chip == chip_) return;
    chip_ = chip;
    refilter();
}

void LibraryModel::set_sort(LibrarySort column, bool ascending) {
    if (column == sort_column_ && ascending == ascending_) return;
    sort_column_ = column;
    ascending_ = ascending;
    resort();
    refilter();
}

std::vector<size_t> LibraryModel::matches() const {
    if (query_.empty()) return sorted_;
    std::vector<size_t> out;
    for (size_t i : sorted_)
        if (matches_query(query_, rows_[i])) out.push_back(i);
    return out;
}

void LibraryModel::resort() {
    sorted_.resize(rows_.size());
    std::iota(sorted_.begin(), sorted_.end(), size_t{0});

    // Equal keys fall back to title, folder, then file path, always
    // ascending: one fixed order, so a row never jumps on a refresh. The
    // folded strings sort without case or accents getting in the way.
    auto tie_break = [this](size_t a, size_t b) {
        const LibraryRow& x = rows_[a];
        const LibraryRow& y = rows_[b];
        if (int c = x.searchable.title.compare(y.searchable.title)) return c < 0;
        if (int c = x.searchable.folder.compare(y.searchable.folder)) return c < 0;
        return x.entry.notespath < y.entry.notespath;
    };

    if (sort_column_ == LibrarySort::BestPath) {
        std::sort(sorted_.begin(), sorted_.end(), [&](size_t a, size_t b) {
            const LibraryRow& x = rows_[a];
            const LibraryRow& y = rows_[b];
            const bool xs = x.summary.score.has_value();
            const bool ys = y.summary.score.has_value();
            // Scored rows first in both directions: "not analyzed" is not a
            // low score.
            if (xs != ys) return xs;
            if (xs) {
                if (*x.summary.score != *y.summary.score)
                    return ascending_ ? *x.summary.score < *y.summary.score
                                      : *x.summary.score > *y.summary.score;
            } else {
                const int rx = unscored_rank(x.status), ry = unscored_rank(y.status);
                if (rx != ry) return rx < ry;
            }
            return tie_break(a, b);
        });
        return;
    }

    auto key = [this](const LibraryRow& r) -> const std::string& {
        switch (sort_column_) {
            case LibrarySort::Artist: return r.searchable.artist;
            case LibrarySort::Charter: return r.searchable.charter;
            case LibrarySort::Folder: return r.searchable.folder;
            case LibrarySort::Title:
            case LibrarySort::BestPath: break;
        }
        return r.searchable.title;
    };
    std::sort(sorted_.begin(), sorted_.end(), [&](size_t a, size_t b) {
        if (int c = key(rows_[a]).compare(key(rows_[b]))) return ascending_ ? c < 0 : c > 0;
        return tie_break(a, b);
    });
}

void LibraryModel::refilter() {
    counts_ = ChipCounts{};
    order_.clear();
    for (size_t i : sorted_) {
        const LibraryRow& row = rows_[i];
        if (!matches_query(query_, row)) continue;
        const StatusChip group = chip_of(row.status);
        ++counts_.all;
        if (group == StatusChip::NotAnalyzed) ++counts_.not_analyzed;
        else if (group == StatusChip::Stale) ++counts_.stale;
        else ++counts_.analyzed;
        if (chip_ == StatusChip::All || chip_ == group) order_.push_back(i);
    }
    // A chip whose last chart left it (the last Stale chart was re-analyzed,
    // or the search matches none of that group) falls back to All, so the
    // table never sits empty behind a filter with nothing in it.
    if (chip_ != StatusChip::All && counts_.of(chip_) == 0) {
        chip_ = StatusChip::All;
        refilter();
    }
}

}  // namespace hydra::ui
