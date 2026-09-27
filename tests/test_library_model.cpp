// Unit tests for ui/library_model: the in-memory library behind the main
// window's table. It filters with app/library_query, counts the status chips,
// sorts, and turns each stored summary into the Best path cell's text.

#include "doctest.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "store/record_store.h"
#include "ui/library_model.h"

using hydra::store::ChartLibraryEntry;
using hydra::store::RecordStatus;
using hydra::store::SummaryLookup;
using hydra::ui::LibraryModel;
using hydra::ui::LibrarySort;
using hydra::ui::StatusChip;

namespace {

ChartLibraryEntry chart(const char* md5, const char* title, const char* artist,
                        const char* charter, const char* folder) {
    ChartLibraryEntry e;
    e.md5 = md5;
    e.title = title;
    e.artist = artist;
    e.charter = charter;
    e.rootfolder = folder;
    e.notespath = std::string("C:\\songs\\") + folder + "\\" + title + "\\notes.mid";
    return e;
}

SummaryLookup ready(int64_t score, const char* bestpath, int stars, double hardest_ms) {
    SummaryLookup s;
    s.status = RecordStatus::Ready;
    s.bestpath = bestpath;
    s.summary.score = score;
    s.summary.stars = stars;
    s.summary.hardest_ms = hardest_ms;
    return s;
}

SummaryLookup stale() {
    SummaryLookup s;
    s.status = RecordStatus::Stale;
    return s;
}

// Six charts from the scratch library's shapes: Burnout analyzed, Chair
// stale, the rest not analyzed. Row order here is scan order, not title order.
LibraryModel sample() {
    LibraryModel m;
    m.set_charts({
        chart("burnout", "Burnout", "Green Day", "Hoph2o", "common\\Summer Blast _25 Setlist\\Tier 4"),
        chart("yyz", "YYZ", "Rush", "Harmonix, Onyxite", "common\\Summer Blast _25 Setlist\\Tier 4"),
        chart("chair", "Chair", "Sufferer", "Satan", "common\\Summer Blast _25 Setlist\\Tier 4"),
        chart("acid", "Acid Romance", "Some Band", "<color=#e02222>Blood</color>line", "common\\Other"),
        chart("halo", "Halo", "Beyoncé", "Someone", "common\\Other"),
        chart("other", "Other", "Thrice", "Someone", "IB24\\T4"),
    });
    SummaryLookup none;
    m.set_summaries({ready(378315, "3- 1 2", 7, 163.0), none, stale(), none, none, none});
    return m;
}

std::vector<std::string> titles(const LibraryModel& m) {
    std::vector<std::string> out;
    for (size_t i : m.order()) out.push_back(m.rows()[i].title);
    return out;
}

}  // namespace

TEST_CASE("library model: every chart shows, sorted by title, with its Best path text") {
    const LibraryModel m = sample();
    CHECK(titles(m) == std::vector<std::string>{"Acid Romance", "Burnout", "Chair", "Halo",
                                                "Other", "YYZ"});
    CHECK(m.rows()[0].best_label == "378,315  3- 1 2");
    CHECK(m.rows()[2].best_label == "Stale");
    CHECK(m.rows()[1].best_label == "Not analyzed");
    // Colour tags never reach the screen.
    CHECK(m.rows()[3].charter == "Bloodline");
    CHECK(m.counts().all == 6);
    CHECK(m.counts().not_analyzed == 4);
    CHECK(m.counts().stale == 1);
    CHECK(m.counts().analyzed == 1);
}

TEST_CASE("library model: the search narrows the rows and the chip counts follow it") {
    LibraryModel m = sample();
    m.set_query("\"tier 4\"");
    CHECK(titles(m) == std::vector<std::string>{"Burnout", "Chair", "YYZ"});
    CHECK(m.counts().all == 3);
    CHECK(m.counts().not_analyzed == 1);
    CHECK(m.counts().stale == 1);
    CHECK(m.counts().analyzed == 1);

    m.set_query("green burnout");
    CHECK(titles(m) == std::vector<std::string>{"Burnout"});
    m.set_query("bloodline");
    CHECK(titles(m) == std::vector<std::string>{"Acid Romance"});
    m.set_query("beyonce");
    CHECK(titles(m) == std::vector<std::string>{"Halo"});
    m.set_query("stars:7");
    CHECK(titles(m) == std::vector<std::string>{"Burnout"});
    m.set_query("squeeze<=200");
    CHECK(titles(m) == std::vector<std::string>{"Burnout"});
    m.set_query("squeeze<=20");
    CHECK(titles(m).empty());
    CHECK(m.counts().all == 0);
    m.set_query("stars:9");
    CHECK_FALSE(m.query().errors.empty());
    m.set_query("");
    CHECK(m.order().size() == 6);
}

TEST_CASE("library model: a status chip narrows the rows, and an emptied chip falls back to All") {
    LibraryModel m = sample();
    m.set_chip(StatusChip::NotAnalyzed);
    CHECK(titles(m) == std::vector<std::string>{"Acid Romance", "Halo", "Other", "YYZ"});
    CHECK(m.counts().all == 6);  // counts ignore the chip
    m.set_chip(StatusChip::Stale);
    CHECK(titles(m) == std::vector<std::string>{"Chair"});
    // "What would Analyze search analyze": the query's matches, whatever the chip.
    CHECK(m.matches().size() == 6);

    // Chair is re-analyzed: the Stale group is empty, so the table goes back
    // to All instead of sitting empty.
    CHECK(m.set_summary_for("chair", ready(300000, "1 1", 6, 40.0)) == 1);
    CHECK(m.chip() == StatusChip::All);
    CHECK(m.order().size() == 6);
    CHECK(m.counts().analyzed == 2);
}

TEST_CASE("library model: Best path sorts by score, with unscored rows last both ways") {
    LibraryModel m = sample();
    m.set_summary_for("yyz", ready(500000, "2 2", 7, 12.0));
    m.set_sort(LibrarySort::BestPath, false);
    CHECK(titles(m) == std::vector<std::string>{"YYZ", "Burnout", "Chair", "Acid Romance",
                                                "Halo", "Other"});
    m.set_sort(LibrarySort::BestPath, true);
    CHECK(titles(m) == std::vector<std::string>{"Burnout", "YYZ", "Chair", "Acid Romance",
                                                "Halo", "Other"});
    m.set_sort(LibrarySort::Title, false);
    CHECK(titles(m).front() == "YYZ");
    m.set_sort(LibrarySort::Artist, true);
    CHECK(titles(m).front() == "Halo");  // "beyonce" folds first
}

TEST_CASE("library model: one chart in two folders gets both rows updated") {
    LibraryModel m;
    m.set_charts({chart("same", "Song", "A", "C", "Pack 1"), chart("same", "Song", "A", "C", "Pack 2")});
    CHECK(m.counts().not_analyzed == 2);
    CHECK(m.set_summary_for("same", ready(1000, "1", 3, 0.0)) == 2);
    CHECK(m.counts().analyzed == 2);
    // Asking again with the same answer changes nothing.
    CHECK(m.set_summary_for("same", ready(1000, "1", 3, 0.0)) == 0);
}

TEST_CASE("library model: filtering 20,000 charts takes under 20 ms") {
    // query_matches runs for every row on every applied keystroke, so the
    // whole pass has to fit well inside a frame.
    std::vector<ChartLibraryEntry> charts;
    std::vector<SummaryLookup> summaries;
    charts.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        char md5[16], title[32], artist[32], charter[64], folder[48];
        std::snprintf(md5, sizeof(md5), "h%05d", i);
        std::snprintf(title, sizeof(title), "Song %05d", i);
        std::snprintf(artist, sizeof(artist), "Artist %02d", i % 50);
        std::snprintf(charter, sizeof(charter), "<color=#e02222>Char</color>ter %d", i % 30);
        std::snprintf(folder, sizeof(folder), "Pack %03d\\Tier %d", i % 200, i % 7);
        charts.push_back(chart(md5, title, artist, charter, folder));
        summaries.push_back(i % 3 == 0 ? ready(300000 + i, "1 2 3", 5 + i % 3, i % 90)
                                       : SummaryLookup{});
    }
    LibraryModel m;
    auto t0 = std::chrono::steady_clock::now();
    m.set_charts(std::move(charts));
    m.set_summaries(summaries);
    MESSAGE("load + sort 20,000: "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
            << " ms");

    for (const char* q : {"s", "song 1", "\"tier 4\"", "artist 07 song", "charter", "stars:7",
                          "squeeze<=20 pack", "zzqx", ""}) {
        t0 = std::chrono::steady_clock::now();
        m.set_query(q);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        MESSAGE("query \"" << std::string(q) << "\": " << ms << " ms, " << m.order().size() << " rows");
        CHECK(ms < 20.0);
    }
    t0 = std::chrono::steady_clock::now();
    m.set_sort(LibrarySort::BestPath, false);
    MESSAGE("sort by Best path: "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
            << " ms");
}
