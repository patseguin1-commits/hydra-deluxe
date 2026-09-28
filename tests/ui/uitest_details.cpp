#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "uitest_harness.h"

#include "app/config.h"
#include "core/model.h"
#include "core/stars.h"
#include "imgui_internal.h"
#include "ui/app_state.h"
#include "ui/details_view.h"
#include "ui/dynamics_load_job.h"
#include "ui/fonts.h"  // px()
#include "ui/preview_controller.h"

namespace uitest {

namespace {

void test_analyze(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK(visible_text(h).find("After analyzing this song") != std::string::npos);
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(!h.app->viewed.record->paths.empty());
    std::string best = h.app->viewed.record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    // The library row's Best Path cell now shows it too.
    IM_CHECK(h.app->library_row_at(0).status ==
             hydra::store::RecordStatus::Ready);

    // Records are kept per SP cap: switching the cap away from 4 shows the
    // song as not analyzed (no record at that cap), switching back finds the
    // 4-bar record again, and the INI follows every change. The SP cap box
    // is in the settings bar, outside the panel the ref points at.
    ctx->ItemInputValue("//Hydra/**/##spcap",8);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 8; }, 5));
    IM_CHECK(h.app->library_row_at(0).status == hydra::store::RecordStatus::NotAnalyzed);
    IM_CHECK(!h.app->viewed.record.has_value());
    IM_CHECK(wait_until(ctx, [&] {
        return hydra::app::Settings::load_file(h.ini_path).sp_cap == 8;
    }, 5));
    ctx->ItemInputValue("//Hydra/**/##spcap",4);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 4; }, 5));
    IM_CHECK(h.app->library_row_at(0).status == hydra::store::RecordStatus::Ready);
    IM_CHECK(h.app->viewed.record.has_value());
    // The headline shows the 4-bar record's best path again.
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
}

// Switching the SP cap between two caps that both have a record swaps
// the viewed record mid-frame, after the panel already chose which path to show.
// That used to leave the details panel reading the freed record (1.5.1 crash:
// bad_alloc from a garbage vector copy, 0xc0000409 on the UI thread).
void test_cap_switch(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    auto analyze = [&] {
        ctx->ItemClick(analyze_button_ref(h).c_str());
        return wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300) &&
               h.app->viewed.record.has_value() && !h.app->viewed.record->paths.empty();
    };
    IM_CHECK(analyze());
    std::string best4 = h.app->viewed.record->best_path().pathstring();
    ctx->ItemInputValue("//Hydra/**/##spcap",6);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 6; }, 5));
    IM_CHECK(analyze());
    std::string best6 = h.app->viewed.record->best_path().pathstring();

    // Flip back and forth; every switch must land on the current record's
    // best path, never on whatever the previous record's memory now holds.
    for (int cap : {4, 6, 4, 6, 4}) {
        ctx->ItemInputValue("//Hydra/**/##spcap",cap);
        IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == cap; }, 5));
        IM_CHECK(h.app->viewed.record.has_value());
        IM_CHECK(h.app->viewed.record->sp_cap == cap);
        const std::string& best = cap == 4 ? best4 : best6;
        IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    }
}

void test_dynamics(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // Search for the chart that the doctest pins dynamics on.
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("**/##search", "Acid Romance");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "Acid Romance"; }, 5));
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->library_shown_count() > 0 &&
               h.app->library_row_at(0).title == "Acid Romance";
    }, 5));
    open_details(ctx, 0);
    if (ctx->IsError()) return;

    // Click the Dynamics tab and wait for the background parse to finish.
    ctx->ItemClick("##DetailsTabs/Dynamics");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->dynamics_result.has_value();
    }, 60));

    std::string text = visible_text(h);
    IM_CHECK(text.find("Dynamics enabled: yes") != std::string::npos);
    IM_CHECK(text.find("2x kicks:") != std::string::npos);
    // The doctest pins 5 ghosts for this chart (all from the red snare).
    IM_CHECK(text.find("Ghosts: 5") != std::string::npos);

    // Toggle 2x Bass off via app state and verify the Dynamics tab updates
    // without re-parsing.
    h.app->settings.view_bass2x = false;
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("not counted (2x Bass off)") != std::string::npos;
    }, 5));

    // Restore.
    h.app->settings.view_bass2x = true;
    h.app->commit_settings();
}

// Stored dynamics: the first open parses and stores; a second open reads
// the store and skips the parse job entirely. An analysis with 2x Bass on
// also stores the breakdown as a by-product, so the Dynamics tab after an
// analysis shows counts with no parse job.
void test_dynamics_stored(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);

    // ---- Scenario 1: parse, store, then re-open from store ----
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // Open Acid Romance. Set the search via app state to avoid the ImGui
    // input-buffer residue from the previous dynamics test.
    h.app->set_search("Acid Romance");
    ctx->Yield(2);
    IM_CHECK(h.app->library_shown_count() > 0);
    IM_CHECK(h.app->library_row_at(0).title == "Acid Romance");
    open_details(ctx, 0);
    if (ctx->IsError()) return;

    // Click the Dynamics tab and wait for the background parse to finish.
    ctx->ItemClick("##DetailsTabs/Dynamics");
    IM_CHECK(wait_until(ctx, [&] { return h.app->dynamics_result.has_value(); }, 60));
    IM_CHECK(visible_text(h).find("Ghosts: 5") != std::string::npos);

    // Select a different chart so the in-memory dynamics cache for Acid
    // Romance is dropped, then close the panel so the tab stops rendering.
    h.app->set_search("");
    size_t other_idx = 0;
    for (size_t i = 0; i < h.app->library_shown_count(); ++i) {
        if (h.app->library_row_at(i).title != "Acid Romance") {
            other_idx = i;
            break;
        }
    }
    h.app->select(h.app->library_row_at(other_idx).entry);
    h.app->show_details = false;
    ctx->Yield(3);

    // Reopen Acid Romance. The Dynamics tab loads its counts from the
    // store (put there by the first open's job), so no parse job starts.
    h.app->set_search("Acid Romance");
    ctx->Yield(2);
    IM_CHECK(h.app->library_shown_count() > 0);
    IM_CHECK(h.app->library_row_at(0).title == "Acid Romance");
    // Clear any leftover dynamics state from the other chart.
    h.app->dynamics_result.reset();
    h.app->dynamics_key.clear();
    if (h.app->dynamics_job) { h.app->dynamics_job->cancel(); h.app->dynamics_job.reset(); }
    open_details(ctx, 0);
    if (ctx->IsError()) return;

    ctx->ItemClick("##DetailsTabs/Dynamics");
    ctx->Yield(3);
    // The stored breakdown was read from the store: no job was started.
    IM_CHECK(h.app->dynamics_result.has_value());
    IM_CHECK(h.app->dynamics_job == nullptr);
    IM_CHECK(visible_text(h).find("Ghosts: 5") != std::string::npos);

    // ---- Scenario 2: analysis stores dynamics as a by-product ----
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // Open any chart (first row) and analyze it with 2x Bass on (the default).
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK(h.app->settings.effective_bass2x());
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());

    // Now click the Dynamics tab. The analysis stored the breakdown, so the
    // tab should show counts with no parse job.
    ctx->ItemClick("##DetailsTabs/Dynamics");
    ctx->Yield(3);
    IM_CHECK(h.app->dynamics_result.has_value());
    IM_CHECK(h.app->dynamics_job == nullptr);
    // The counts are on screen (the first chart has notes, so "All" > 0).
    std::string text = visible_text(h);
    IM_CHECK(text.find("Ghosts:") != std::string::npos ||
             text.find("Accents:") != std::string::npos);
}

// The Stars tab: a prompt before analysis, then the base score, the solo
// bonus and the seven cutoffs from star_cutoffs(). "87" has a drum solo;
// "I'm A Believer" has a solo only on guitar, so its drums show none.
void test_stars(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // ---- A song with a drum solo ----
    open_titled(ctx, "Polyphia", "87");
    if (ctx->IsError()) return;
    ctx->ItemClick("##DetailsTabs/Stars");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("After analyzing this song, star cutoffs will show up here.") !=
             std::string::npos);

    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    ctx->ItemClick("##DetailsTabs/Stars");
    ctx->Yield(3);

    hydra::StarCutoffs sc = hydra::star_cutoffs(h.app->viewed.record->best_path());
    IM_CHECK(sc.solo_bonus > 0);
    std::string text = visible_text(h);
    IM_CHECK(text.find("Base score: " + hydra::group_thousands(sc.base)) != std::string::npos);
    IM_CHECK(text.find("Solo bonus: " + hydra::group_thousands(sc.solo_bonus) +
                       " (not counted toward stars)") != std::string::npos);
    IM_CHECK(text.find("With full solo bonus") != std::string::npos);
    for (int64_t cutoff : sc.cutoffs) {
        IM_CHECK(text.find(hydra::group_thousands(cutoff)) != std::string::npos);
        IM_CHECK(text.find(hydra::group_thousands(cutoff + sc.solo_bonus)) != std::string::npos);
    }
    IM_CHECK(text.find("4.4") != std::string::npos);

    // ---- A song with no drum solo ----
    h.app->show_details = false;
    ctx->Yield(3);
    open_titled(ctx, "Believer", "I'm A Believer (The Monkees cover)");
    if (ctx->IsError()) return;
    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    ctx->ItemClick("##DetailsTabs/Stars");
    ctx->Yield(3);

    sc = hydra::star_cutoffs(h.app->viewed.record->best_path());
    IM_CHECK(sc.solo_bonus == 0);
    text = visible_text(h);
    IM_CHECK(text.find("Base score: " + hydra::group_thousands(sc.base)) != std::string::npos);
    IM_CHECK(text.find(hydra::group_thousands(sc.cutoffs[hydra::kMaxStars - 1])) !=
             std::string::npos);
    IM_CHECK(text.find("Solo bonus") == std::string::npos);
    IM_CHECK(text.find("With full solo bonus") == std::string::npos);
}

// Hiding the details panel by any route tears it down. The "Rescan library"
// button used to set show_details = false directly, which skipped the
// teardown: the Preview kept its audio device and kept playing.
void test_details_close_teardown(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    ctx->ItemClick("**/Play");
    IM_CHECK(h.app->preview->playing());

    // Exactly what the Rescan library button does. The button itself only
    // shows when the chart file is missing, which never happens here.
    h.app->request_scan = true;
    h.app->show_details = false;
    ctx->Yield(3);
    IM_CHECK(!h.app->preview->active());
    IM_CHECK(!h.app->preview->playing());

    // The rescan the button asked for runs; finish it so the app is idle.
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->scan_job && h.app->scan_job->snapshot().finished;
    }, 60));
    ctx->SetRef("//Scanning charts");
    ctx->ItemClick("Continue");
    ctx->Yield(2);
}

// The panel docks beside the library: opening it narrows the library, and
// the X and Escape both close it and give the library its width back.
void test_panel_open_close(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    const float main_w = ctx->GetWindowByRef("//Hydra")->Size.x;
    auto library_w = [&] { return ctx->WindowInfo("//Hydra/##library").Window->Size.x; };
    IM_CHECK(library_w() > main_w * 0.9f);

    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK(library_w() < main_w * 0.7f);
    IM_CHECK(ctx->WindowInfo("//Hydra/##songpanel").Window != nullptr);
    IM_CHECK(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));  // no modal any more

    ctx->ItemClick("X##closepanel");
    ctx->Yield(3);
    IM_CHECK(!h.app->details_open());
    IM_CHECK(library_w() > main_w * 0.9f);

    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    IM_CHECK(!h.app->details_open());
}

// The split beside the panel. A drag of the library's edge is the only thing
// that changes it: it survives closing and reopening the panel, and a
// hydra_ui.ini from before the fix, whose library entry holds the largest
// split, loses to the [Hydra][Layout] share.
void test_panel_split(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    auto library = [&] { return ctx->WindowInfo("//Hydra/##library").Window; };
    auto panel = [&] { return ctx->WindowInfo("//Hydra/##songpanel").Window; };
    auto close = [&] {
        ctx->SetRef("//Hydra");
        ctx->ItemClick("**/X##closepanel");
        ctx->Yield(3);
    };
    auto open = [&] {
        open_details(ctx, 0);
        ctx->Yield(2);
    };

    // Opening: the default share, clamped so the panel keeps its minimum.
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->Yield(2);
    const float room = library()->Size.x + panel()->Size.x;
    const float min_panel = hydra::ui::px(hydra::ui::kMinSongPanelW);
    // (std::min) in brackets: windows.h, through the harness, defines min.
    const float opened = (std::min)(hydra::ui::kDefaultLibraryShare * room, room - min_panel);
    IM_CHECK_FLOAT_NEAR_EQ(library()->Size.x, opened, 1.0f);
    IM_CHECK_GE(panel()->Size.x, min_panel - 1.0f);

    // Drag the library's right edge 80 px left.
    ImGuiWindow* lib = library();
    const ImVec2 edge(lib->Pos.x + lib->Size.x, lib->Pos.y + lib->Size.y * 0.5f);
    ctx->MouseMoveToPos(edge);
    ctx->MouseDown(ImGuiMouseButton_Left);
    ctx->MouseMoveToPos(ImVec2(edge.x - 80.0f, edge.y));
    ctx->MouseUp(ImGuiMouseButton_Left);
    ctx->Yield(2);
    const float dragged = library()->Size.x;
    IM_CHECK_FLOAT_NEAR_EQ(dragged, opened - 80.0f, 2.0f);
    IM_CHECK_FLOAT_NEAR_EQ(hydra::ui::library_share(), dragged / room, 0.002f);

    // Closing gives the library the full width; reopening puts the drag back.
    close();
    if (ctx->IsError()) return;  // WindowInfo answers null once a test has failed
    IM_CHECK(library()->Size.x > room * 0.9f);
    open();
    if (ctx->IsError()) return;
    IM_CHECK_FLOAT_NEAR_EQ(library()->Size.x, dragged, 1.0f);

    // An old hydra_ui.ini, read while no song is open (as at startup): the
    // library child's own entry at a huge width (what the bug saved) and a
    // share. The share wins.
    close();
    if (ctx->IsError()) return;
    const std::string old_ini = std::string("[Window][") + library()->Name +
                                "]\nSize=4000,600\n[Hydra][Layout]\nLibraryShare=0.3000\n";
    ImGui::LoadIniSettingsFromMemory(old_ini.c_str(), old_ini.size());
    IM_CHECK_FLOAT_NEAR_EQ(hydra::ui::library_share(), 0.3f, 0.0001f);
    open();
    if (ctx->IsError()) return;
    IM_CHECK_FLOAT_NEAR_EQ(library()->Size.x, (std::max)(hydra::ui::px(320.0f), 0.3f * room), 1.0f);
}

// Every window on screen whose content is wider than its room (and that has
// no horizontal scroll bar to reach the rest): text or buttons running past
// an edge. Tooltips and ImGui's own debug window aside.
void overflowing_windows(const std::string& screen, std::vector<std::string>& found) {
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (!w->WasActive || (w->Flags & ImGuiWindowFlags_Tooltip) ||
            (w->Flags & ImGuiWindowFlags_HorizontalScrollbar) ||
            std::strncmp(w->Name, "Debug##", 7) == 0)
            continue;
        const float room = w->ContentRegionRect.GetWidth();
        if (w->ContentSize.x > room + 0.5f) {
            char line[512];
            std::snprintf(line, sizeof(line), "%s: %s content %.0f px, room %.0f px",
                          screen.c_str(), w->Name, w->ContentSize.x, room);
            found.push_back(line);
        }
    }
}

// Walks the song panel's four tabs on charts with long titles and long paths,
// at the narrowest and the widest panel, and fails naming every window whose
// content runs past its edge.
void test_layout_sweep(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    std::vector<std::string> found;
    ctx->Yield(2);
    overflowing_windows("library", found);

    struct Chart {
        const char* search;
        const char* title;
    };
    const Chart charts[] = {
        {"burnout", "Burnout"},
        {"spiraling void", "The Spiraling Void"},
        {"tapestry", "Tapestry of the Starless Abstract (Shortened)"},
        // A MIDI chart without [ENABLE_CHART_DYNAMICS]: the Dynamics tab's
        // right box carries its longest line.
        {"themata", "Themata"},
    };
    for (const Chart& c : charts) {
        open_titled(ctx, c.search, c.title);
        if (ctx->IsError()) return;
        analyze_open_song(ctx);
        if (ctx->IsError()) return;
        for (float share : {0.99f, 0.01f}) {
            hydra::ui::remember_library_share(share);
            h.app->library_ui.panel_was_open = false;
            ctx->Yield(3);
            const std::string at = std::string(c.title) + (share > 0.5f ? " narrow " : " wide ");
            set_panel_ref(ctx);
            ctx->ItemClick("**/##DetailsTabs/Paths");
            ctx->Yield(2);
            if (ctx->ItemExists("**/Expand all")) ctx->ItemClick("**/Expand all");
            ctx->Yield(2);
            overflowing_windows(at + "Paths", found);
            ctx->ItemClick("**/##DetailsTabs/Preview");
            wait_until(ctx, [&] { return h.app->preview && h.app->preview->active() &&
                                         !h.app->preview->loading(); }, 120);
            ctx->Yield(3);
            overflowing_windows(at + "Preview", found);
            ctx->ItemClick("**/##DetailsTabs/Dynamics");
            ctx->Yield(10);
            if (std::strcmp(c.title, "Themata") == 0)
                IM_CHECK(wait_until(ctx, [&] {
                    return visible_text(h).find(
                               "Dynamics enabled: no (markings ignored by Clone Hero)") !=
                           std::string::npos;
                }, 60));
            overflowing_windows(at + "Dynamics", found);
            ctx->ItemClick("**/##DetailsTabs/Stars");
            ctx->Yield(3);
            overflowing_windows(at + "Stars", found);
            if (ctx->IsError()) return;
        }
    }
    for (const std::string& f : found) std::fprintf(stderr, "OVERFLOW %s\n", f.c_str());
    IM_CHECK_EQ(found.size(), (size_t)0);
}

// A long error message wraps inside its window instead of running past the
// panel's edge. The Dynamics tab's "Dynamics failed: ..." line is the one the
// harness can reach: pointing the open song at a long path with an unknown
// extension makes the parse fail with that path in its message.
void test_long_error_wraps(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_titled(ctx, "burnout", "Burnout");
    if (ctx->IsError()) return;
    hydra::ui::remember_library_share(0.99f);  // the narrowest panel
    h.app->library_ui.panel_was_open = false;
    ctx->Yield(3);
    set_panel_ref(ctx);
    ctx->ItemClick("**/##DetailsTabs/Dynamics");
    IM_CHECK(wait_until(ctx, [&] { return h.app->dynamics_result.has_value(); }, 60));

    std::string long_path = "C:\\Songs";
    for (int i = 0; i < 8; ++i)
        long_path += "\\A Rather Long Folder Name Kept For Testing " + std::to_string(i);
    long_path += "\\notes.txt";
    h.app->selected->notespath = long_path;
    h.app->selected->md5 = "ffffffffffffffffffffffffffffffff";  // no stored counts
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->dynamics_job && h.app->dynamics_job->finished() &&
               !h.app->dynamics_job->ok();
    }, 30));
    ctx->Yield(3);
    const std::string line = "Dynamics failed: " + h.app->dynamics_job->error();
    IM_CHECK(visible_text(h).find(line) != std::string::npos);
    // Unwrapped, the line would be wider than the whole screen.
    IM_CHECK_GT(ImGui::CalcTextSize(line.c_str()).x, ImGui::GetIO().DisplaySize.x);

    // Only the song panel and its children: the settings bar is another
    // task's (layout-sweep covers it).
    std::vector<std::string> all, found;
    overflowing_windows("long error", all);
    for (const std::string& f : all)
        if (f.find("##songpanel") != std::string::npos) found.push_back(f);
    for (const std::string& f : found) std::fprintf(stderr, "OVERFLOW %s\n", f.c_str());
    IM_CHECK_EQ(found.size(), (size_t)0);
}

// Previous / next step through the library's rows and stop at the ends.
void test_panel_prev_next(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK((ctx->ItemInfo("<##prevsong").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    ctx->ItemClick(">##nextsong");
    ctx->Yield(2);
    IM_CHECK(h.app->selected->notespath == h.app->view_row(1).notespath);
    IM_CHECK(h.app->details_open());
    ctx->ItemClick("<##prevsong");
    ctx->Yield(2);
    IM_CHECK(h.app->selected->notespath == h.app->view_row(0).notespath);
}

// The panel's top on Burnout (Green Day, charted by Hoph2o), before and after
// analysis, at the scratch settings (Expert, Pro Drums, 2x Bass, cap 4,
// 2 scores, 10 ms).
void test_panel_headline(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_titled(ctx, "Burnout", "Burnout");
    if (ctx->IsError()) return;
    std::string text = visible_text(h);
    IM_CHECK(text.find("Green Day \xC2\xB7 charted by Hoph2o") != std::string::npos);
    IM_CHECK(text.find("Not analyzed yet.") != std::string::npos);
    IM_CHECK(ctx->ItemExists("**/Analyze this song"));

    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("378,315") != std::string::npos;
    }, 5));
    text = visible_text(h);
    IM_CHECK(text.find("3- 1 2") != std::string::npos);
    IM_CHECK(text.find("Optimal path \xC2\xB7 7 stars \xC2\xB7 hardest squeeze") !=
             std::string::npos);
    IM_CHECK(text.find("163.0 ms") != std::string::npos);
    IM_CHECK(ctx->ItemExists("**/Re-analyze"));
}

// Closing the panel mid-analysis no longer cancels it: the result is stored
// and the row reads Ready.
void test_panel_keeps_analysis(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Analyze this song");
    ctx->ItemClick("X##closepanel");
    ctx->Yield(2);
    IM_CHECK(!h.app->details_open());
    IM_CHECK(!h.app->analyze_job || !h.app->analyze_job->is_cancelled());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->view_row_status(0) == hydra::store::RecordStatus::Ready;
    }, 5));
}

// The settings bar locks while a batch runs and unlocks when it stops.
void test_settings_lock(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    IM_CHECK((ctx->ItemInfo("**/##spcap").ItemFlags & ImGuiItemFlags_Disabled) == 0);

    h.app->start_batch(false);  // the whole library: long enough to look at
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_running(); }, 10));
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("Stop the batch to change these.") != std::string::npos;
    }, 5));
    IM_CHECK((ctx->ItemInfo("**/##spcap").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    IM_CHECK((ctx->ItemInfo("**/Pro Drums").ItemFlags & ImGuiItemFlags_Disabled) != 0);

    h.app->batch_job->stop();
    IM_CHECK(wait_until(ctx, [&] { return !h.app->batch_running(); }, 300));
    IM_CHECK(wait_until(ctx, [&] { return !jobs_busy(h); }, 120));
    IM_CHECK((ctx->ItemInfo("**/##spcap").ItemFlags & ImGuiItemFlags_Disabled) == 0);
}

}  // namespace

const std::vector<TestEntry>& details_tests() {
    static const std::vector<TestEntry> entries = {
        {"analyze", test_analyze},
        {"cap-switch", test_cap_switch},
        {"dynamics", test_dynamics},
        {"dynamics-stored", test_dynamics_stored},
        {"stars", test_stars},
        {"details-close-teardown", test_details_close_teardown},
        {"panel-open-close", test_panel_open_close},
        {"panel-split", test_panel_split},
        {"layout-sweep", test_layout_sweep},
        {"long-error-wraps", test_long_error_wraps},
        {"panel-prev-next", test_panel_prev_next},
        {"panel-headline", test_panel_headline},
        {"panel-keeps-analysis", test_panel_keeps_analysis},
        {"settings-lock", test_settings_lock},
    };
    return entries;
}

}  // namespace uitest
