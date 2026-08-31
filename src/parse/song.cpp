#include "parse/song.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <regex>
#include <stdexcept>
#include <unordered_map>

#include "core/winstr.h"  // read_file_bytes
#include "parse/midi.h"
#include "parse/srb.h"

namespace hydra {

// ---- shared helpers -----------------------------------------------------

namespace {

std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

std::vector<std::string> split_char(const std::string& s, char c) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(c, start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

// The name inside a practice-section marker body, which both formats spell one
// of two ways: "section Verse 2B" (Clone Hero) or "prc_verse_2b" (Rock Band).
// Nothing else is a section.
bool section_name_of(const std::string& body, std::string* name) {
    if (body.rfind("section ", 0) == 0) {
        *name = body.substr(8);
        return true;
    }
    if (body.rfind("prc_", 0) == 0) {
        *name = body.substr(4);
        return true;
    }
    return false;
}

bool try_parse_int(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    try {
        size_t idx = 0;
        long long v = std::stoll(s, &idx);
        if (idx == s.size()) {
            out = static_cast<int64_t>(v);
            return true;
        }
    } catch (...) {
    }
    return false;
}

std::string ascii_casefold(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    return out;
}

const std::regex& re_dynamics() {
    static const std::regex r(R"(\[?ENABLE_CHART_DYNAMICS\]?)");
    return r;
}
const std::regex& re_disco_on() {
    static const std::regex r(R"(\[?mix.3.drums\d?d\]?)");
    return r;
}
const std::regex& re_disco_off() {
    static const std::regex r(R"(\[?mix.3.drums\d?(dnoflip)?\]?)");
    return r;
}

bool full_match(const std::string& s, const std::regex& re) {
    return std::regex_match(s, re);
}

// Activation-fill placement heuristic, shared by both parsers: true when the
// fill that ended at `fill_end_tick` lands on the chord being emitted at
// `tick` (so its op must run after the chord emit), false when it belongs to
// an earlier chord (run it before). "Lands on" means the next chord is within
// a 1/32-of-a-beat slop of the fill end and no closer to the previous chord.
bool fill_lands_on_chord(const Song& song, int64_t fill_end_tick, int64_t tick) {
    std::optional<int64_t> prevchord_dist;
    if (!song.sequence.empty())
        prevchord_dist = fill_end_tick - song.sequence.back().timecode.ticks();
    int64_t nextchord_dist = tick - fill_end_tick;
    return nextchord_dist <= song.tick_resolution() / 32 &&
           (!prevchord_dist.has_value() || nextchord_dist <= *prevchord_dist);
}

// Emit the buffered chord as a sequence timestamp, shared by both parsers.
// apply_flam is true only on the .mid path: MIDI charts carry a flam marker
// that converts the chord, while the .chart format has no such marker, so
// ChartParser always passes false (existing behavior, now explicit).
void emit_chord_timestamp(Song& song, Chord& chord, int64_t tick,
                          bool apply_flam, bool apply_disco, bool solo) {
    if (apply_flam) chord.apply_flam_conversion();
    if (apply_disco) chord.apply_disco_flip();
    SongTimestamp ts;
    ts.chord = chord;
    ts.timecode = song.timecode(tick);
    ts.flag_solo = solo;
    song.sequence.push_back(std::move(ts));
}

}  // namespace

const char* difficulty_name(Difficulty difficulty) {
    switch (difficulty) {
        case Difficulty::Hard: return "Hard";
        case Difficulty::Medium: return "Medium";
        case Difficulty::Easy: return "Easy";
        default: return "Expert";
    }
}

// ---- Song::check_activations -------------------------------------------

void Song::check_activations() {
    for (const SongTimestamp& ts : sequence)
        if (ts.has_activation()) return;  // chart already has fills.

    features.push_back("Auto-Generated Fills");

    struct Cell {
        int64_t tick;                 // the downbeat tick
        std::optional<size_t> best;   // index into sequence of the closest chord
        int64_t bestdist = 0;
        int64_t pre_tpm = 0;
    };
    // std::map keeps measures sorted, which equals Python's dict insertion
    // order here: measures are only ever added at or above the current max.
    std::map<int64_t, Cell> measuremap;

    // SongIter: walk the sequence, advancing at most one tpm/bpm mark each step.
    std::vector<int64_t> tpm_keys, bpm_keys;
    for (const auto& kv : tpm_changes) tpm_keys.push_back(kv.first);
    for (const auto& kv : bpm_changes) bpm_keys.push_back(kv.first);
    size_t tpm_pos = 0, bpm_pos = 0;
    int64_t tpm = tpm_keys.empty() ? 0 : tpm_changes[tpm_keys[tpm_pos++]];
    if (!bpm_keys.empty()) bpm_pos++;  // bpm value itself is unused here

    Timecode downbeat_ref = start_time();

    for (size_t i = 0; i < sequence.size(); ++i) {
        SongTimestamp& ts = sequence[i];
        int64_t tick = ts.timecode.ticks();

        int64_t pre_tpm = tpm;
        if (tpm_pos < tpm_keys.size()) {
            if (tick == tpm_keys[tpm_pos]) {
                tpm = tpm_changes[tpm_keys[tpm_pos]];
                ++tpm_pos;
            } else if (tick > tpm_keys[tpm_pos]) {
                tpm = tpm_changes[tpm_keys[tpm_pos]];
                pre_tpm = tpm;
                ++tpm_pos;
            }
        }
        if (bpm_pos < bpm_keys.size() && tick >= bpm_keys[bpm_pos]) ++bpm_pos;

        int64_t m0 = ts.timecode.measure_beats_ticks()[0];
        for (int64_t measure : {m0 + 1, m0 + 2}) {
            auto it = measuremap.find(measure);
            if (it == measuremap.end()) {
                downbeat_ref = timing().plusmeasure(
                    downbeat_ref,
                    measure - (downbeat_ref.measure_beats_ticks()[0] + 1));
                Cell c;
                c.tick = downbeat_ref.ticks();
                it = measuremap.emplace(measure, c).first;
            }
            Cell& cell = it->second;
            int64_t dist = std::llabs(cell.tick - tick);
            if (!cell.best.has_value() || dist <= cell.bestdist) {
                cell.best = i;
                cell.bestdist = dist;
                cell.pre_tpm = pre_tpm;
            }
        }
    }

    const int64_t ACT_COOLDOWN_MEASURES = 4;
    const int64_t MAX_DISTANCE = tick_resolution_ / 2;
    std::optional<int64_t> last_act_measure;
    for (auto& kv : measuremap) {
        int64_t measure = kv.first;
        Cell& cell = kv.second;
        if (last_act_measure.has_value() &&
            measure < *last_act_measure + ACT_COOLDOWN_MEASURES)
            continue;
        if (cell.best.has_value() && cell.bestdist <= MAX_DISTANCE) {
            sequence[*cell.best].activation_length = cell.pre_tpm / 2;
            last_act_measure = measure;
        }
    }
}

// ---- MidiParser ---------------------------------------------------------

namespace {

enum class MPhase { None, Time, Pre, PreDelayed, Notes, Post, PostDelayed,
                    PreTimestamp };

struct MOp {
    MPhase phase = MPhase::None;
    std::function<void()> run;
};

// All four difficulties share the one "PART DRUMS" track; each owns a block of
// five pitches starting here (kick, then the four pads).
int difficulty_base_pitch(Difficulty difficulty) {
    switch (difficulty) {
        case Difficulty::Hard: return 84;
        case Difficulty::Medium: return 72;
        case Difficulty::Easy: return 60;
        default: return 96;
    }
}

// `base` is the difficulty's kick pitch. The five note pitches follow it; every
// other pitch here is a marker shared by all four difficulties (95 is the 2x
// kick, which only Expert charts carry). A pitch outside this set belongs to
// another difficulty (or to another instrument) and is dropped.
bool is_handled_note(int note, int base) {
    if (note >= base && note <= base + 4) return true;
    switch (note) {
        case 95:
        case 103:
        case 109: case 110: case 111: case 112:
        case 116:
        case 120:
            return true;
        default:
            return false;
    }
}

class MidiParser {
public:
    Song parse(const MidiFile& mid, bool pro, bool bass2x, Difficulty difficulty);

private:
    MOp optype(const Message& msg, int64_t tick);
    void push_timestamp(int64_t tick);
    static void run_ops(std::vector<std::function<void()>>& ops);

    // op_* handlers
    void op_enable_dynamics() { dynamics_enabled_ = true; }
    void op_disco(bool on) { flag_disco_ = on; }
    void op_tempo(int64_t tick, uint32_t miditempo) {
        song_->bpm_changes[tick] = 60000000.0 / static_cast<double>(miditempo);
    }
    void op_timesig(int64_t tick, int numerator, int denominator) {
        song_->tpm_changes[tick] = song_->tick_resolution() *
                                   static_cast<int64_t>(numerator) * 4 /
                                   static_cast<int64_t>(denominator);
    }
    void op_fillstart(int64_t tick) {
        fill_start_tick_ = tick;
        fill_end_tick_.reset();
    }
    void op_store_fillend(int64_t tick) { fill_end_tick_ = tick; }
    void op_apply_fill(int64_t starttick) {
        if (song_->sequence.empty()) return;
        SongTimestamp& last = song_->sequence.back();
        if (last.timecode.ticks() >= starttick)
            last.activation_length = last.timecode.ticks() - starttick;
    }
    void op_sp_start(int64_t tick) { sp_start_tick_ = tick; }
    void op_sp_end() {
        if (song_->sequence.empty()) {
            sp_start_tick_.reset();
            return;
        }
        if (song_->sequence.back().timecode.ticks() >= *sp_start_tick_) {
            song_->sequence.back().flag_sp = true;
            song_->sequence.back().sp_phrase_start = *sp_start_tick_;
        }
        sp_start_tick_.reset();
    }
    void op_tom(NoteColor color, NoteCymbalType cymbal) {
        flag_cymbals_[static_cast<int>(color) - 1] = cymbal;
    }
    void op_flam(bool enabled) { flag_flam_ = enabled; }
    void op_solo(bool on) { flag_solo_ = on; }
    void op_note(NoteColor color, NoteDynamicType dyn, bool is2x) {
        ChordNote& note = chord_.add_note(color);  // may throw ChartFileError
        note.dynamictype = dynamics_enabled_ ? dyn : NoteDynamicType::Normal;
        if (allows_cymbals(color) && mode_pro_)
            note.cymbaltype = flag_cymbals_[static_cast<int>(color) - 1];
        note.is2x = is2x;
    }

    Song* song_ = nullptr;
    bool mode_pro_ = false;
    bool mode_bass2x_ = false;
    int base_ = 96;

    Chord chord_;
    std::vector<const Message*> msg_buffer_;
    bool flag_solo_ = false;
    std::array<NoteCymbalType, 5> flag_cymbals_{};
    bool flag_flam_ = false;
    bool flag_disco_ = false;
    std::optional<int64_t> fill_start_tick_;
    std::optional<int64_t> fill_end_tick_;
    bool dynamics_enabled_ = false;
    std::optional<int64_t> sp_start_tick_;
};

MOp MidiParser::optype(const Message& msg, int64_t tick) {
    const bool is_channel = (msg.type == "note_on" || msg.type == "note_off");

    if (is_channel) {
        int note = msg.note;
        if (!is_handled_note(note, base_)) return {};

        int velocity = msg.velocity;
        bool is_noteon = (msg.type == "note_on" && velocity > 0);
        bool is_noteoff =
            (msg.type == "note_off" || (msg.type == "note_on" && velocity == 0));

        if (is_noteoff && note < 103) return {};

        if (is_noteon) {
            // The difficulty's own five pitches come first: base is the kick,
            // the next four are Red/Yellow/Blue/Green.
            if (note == base_) {
                return {MPhase::Notes, [this] {
                            op_note(NoteColor::Kick,
                                    NoteDynamicType::Normal, false);
                        }};
            }
            if (note > base_ && note <= base_ + 4) {
                // base+1 -> Red(2), as 97 -> Red(2) on Expert.
                NoteColor color = static_cast<NoteColor>(note - base_ + 1);
                NoteDynamicType dyn = NoteDynamicType::Normal;
                if (velocity == 127) dyn = NoteDynamicType::Accent;
                else if (velocity == 1) dyn = NoteDynamicType::Ghost;
                return {MPhase::Notes,
                        [this, color, dyn] { op_note(color, dyn, false); }};
            }
            switch (note) {
                case 95:
                    if (mode_bass2x_)
                        return {MPhase::Notes, [this] {
                                    op_note(NoteColor::Kick,
                                            NoteDynamicType::Normal, true);
                                }};
                    return {};
                case 120:
                    return {MPhase::PostDelayed,
                            [this, tick] { op_fillstart(tick); }};
                case 116:
                    return {sp_start_tick_.has_value() ? MPhase::PreDelayed
                                                       : MPhase::Pre,
                            [this, tick] { op_sp_start(tick); }};
                case 112:
                    return {MPhase::Pre, [this] {
                                op_tom(NoteColor::Green, NoteCymbalType::Normal);
                            }};
                case 111:
                    return {MPhase::Pre, [this] {
                                op_tom(NoteColor::Blue, NoteCymbalType::Normal);
                            }};
                case 110:
                    return {MPhase::Pre, [this] {
                                op_tom(NoteColor::Yellow, NoteCymbalType::Normal);
                            }};
                case 109:
                    return {MPhase::Pre, [this] { op_flam(true); }};
                case 103:
                    return {MPhase::Pre, [this] { op_solo(true); }};
                default:
                    return {};
            }
        }

        if (is_noteoff) {
            switch (note) {
                case 120:
                    return {MPhase::Pre,
                            [this, tick] { op_store_fillend(tick); }};
                case 116:
                    return {sp_start_tick_.has_value() ? MPhase::Pre
                                                       : MPhase::PreDelayed,
                            [this] { op_sp_end(); }};
                case 112:
                    return {MPhase::Pre, [this] {
                                op_tom(NoteColor::Green, NoteCymbalType::Cymbal);
                            }};
                case 111:
                    return {MPhase::Pre, [this] {
                                op_tom(NoteColor::Blue, NoteCymbalType::Cymbal);
                            }};
                case 110:
                    return {MPhase::Pre, [this] {
                                op_tom(NoteColor::Yellow, NoteCymbalType::Cymbal);
                            }};
                case 109:
                    return {MPhase::Pre, [this] { op_flam(false); }};
                case 103:
                    return {MPhase::Pre, [this] { op_solo(false); }};
                default:
                    return {};
            }
        }
        return {};
    }

    // Meta family. Text-attribute metas carry `str`; name-attribute metas do
    // not match Python's `MetaMessage(text=...)` patterns.
    if (msg.str_attr == Message::StrAttr::Text) {
        const std::string& t = msg.str;
        if (full_match(t, re_dynamics()))
            return {MPhase::Pre, [this] { op_enable_dynamics(); }};
        if (full_match(t, re_disco_on()))
            return {MPhase::Pre, [this] { op_disco(true); }};
        if (full_match(t, re_disco_off()))
            return {MPhase::Pre, [this] { op_disco(false); }};
    }
    if (msg.type == "set_tempo") {
        uint32_t tempo = msg.tempo;
        return {MPhase::Time, [this, tick, tempo] { op_tempo(tick, tempo); }};
    }
    if (msg.type == "time_signature") {
        int num = msg.numerator, den = msg.denominator;
        return {MPhase::Time,
                [this, tick, num, den] { op_timesig(tick, num, den); }};
    }
    return {};
}

void MidiParser::run_ops(std::vector<std::function<void()>>& ops) {
    for (auto& op : ops) {
        try {
            op();
        } catch (const ChartFileError&) {
        }
    }
}

void MidiParser::push_timestamp(int64_t tick) {
    chord_ = Chord();

    std::vector<std::function<void()>> pre, pre_delayed, notes, pre_timestamp,
        post, post_delayed;
    for (const Message* msg : msg_buffer_) {
        MOp op = optype(*msg, tick);
        if (!op.run) continue;
        switch (op.phase) {
            case MPhase::Pre: pre.push_back(std::move(op.run)); break;
            case MPhase::PreDelayed: pre_delayed.push_back(std::move(op.run)); break;
            case MPhase::Notes: notes.push_back(std::move(op.run)); break;
            case MPhase::Post: post.push_back(std::move(op.run)); break;
            case MPhase::PostDelayed: post_delayed.push_back(std::move(op.run)); break;
            case MPhase::PreTimestamp: pre_timestamp.push_back(std::move(op.run)); break;
            default: break;  // Time / None: not run from push_timestamp.
        }
    }

    run_ops(pre);
    run_ops(pre_delayed);
    run_ops(notes);

    // Activation fill placement.
    if (chord_.count() && fill_end_tick_.has_value() &&
        tick >= *fill_end_tick_) {
        int64_t start = *fill_start_tick_;
        auto fill_op = [this, start] { op_apply_fill(start); };

        if (fill_lands_on_chord(*song_, *fill_end_tick_, tick))
            post.push_back(std::move(fill_op));
        else
            pre_timestamp.push_back(std::move(fill_op));
        fill_start_tick_.reset();
        fill_end_tick_.reset();
    }

    run_ops(pre_timestamp);

    if (chord_.count())
        emit_chord_timestamp(*song_, chord_, tick, flag_flam_,
                             mode_pro_ && flag_disco_, flag_solo_);

    run_ops(post);
    run_ops(post_delayed);

    msg_buffer_.clear();
}

Song MidiParser::parse(const MidiFile& mid, bool pro, bool bass2x,
                       Difficulty difficulty) {
    mode_pro_ = pro;
    mode_bass2x_ = bass2x;
    base_ = difficulty_base_pitch(difficulty);

    Song song(mid.ticks_per_beat);
    song_ = &song;

    // Pass 1: tempo/time-signature marks from the first track.
    int64_t elapsed = 0;
    for (const Message& msg : mid.tracks[0].messages) {
        elapsed += msg.time;
        MOp op = optype(msg, elapsed);
        if (op.phase == MPhase::Time && op.run) op.run();
    }
    song.build_timing();

    // Pass 2: the drum track.
    for (const MidiTrack& track : mid.tracks) {
        if (track.name != "PART DRUMS") continue;
        elapsed = 0;
        msg_buffer_.clear();
        flag_solo_ = false;
        flag_disco_ = false;
        flag_cymbals_[static_cast<int>(NoteColor::Green) - 1] =
            NoteCymbalType::Cymbal;
        flag_cymbals_[static_cast<int>(NoteColor::Blue) - 1] =
            NoteCymbalType::Cymbal;
        flag_cymbals_[static_cast<int>(NoteColor::Yellow) - 1] =
            NoteCymbalType::Cymbal;
        dynamics_enabled_ = false;
        for (const Message& msg : track.messages) {
            if (msg.time != 0) {
                push_timestamp(elapsed);
                elapsed += msg.time;
            }
            msg_buffer_.push_back(&msg);
        }
        push_timestamp(elapsed);
        break;
    }

    // Pass 3: practice sections, which live on their own track as bracketed
    // text metas.
    for (const MidiTrack& track : mid.tracks) {
        if (track.name != "EVENTS") continue;
        elapsed = 0;
        for (const Message& msg : track.messages) {
            elapsed += msg.time;
            if (msg.str_attr != Message::StrAttr::Text) continue;
            const std::string& s = msg.str;
            if (s.size() < 2 || s.front() != '[' || s.back() != ']') continue;
            std::string name;
            if (section_name_of(s.substr(1, s.size() - 2), &name))
                song.practice_sections.push_back({elapsed, name});
        }
    }

    song.check_activations();
    return song;
}

}  // namespace

// ---- ChartParser --------------------------------------------------------

namespace {

struct ChartDataEntry {
    std::optional<int64_t> key_tick;
    std::optional<std::string> key_name;

    std::optional<int64_t> property_int;
    std::optional<std::string> property_str;

    std::optional<int> ts_numerator;
    std::optional<int> ts_denominator;
    std::optional<double> tempo_bpm;

    bool solo_start = false;
    bool solo_end = false;
    bool discoflip_enable = false;
    bool discoflip_disable = false;

    // A generic text event's payload: the value with its leading "E" and any
    // surrounding quotes taken off. Only the [Events] walk reads it.
    std::optional<std::string> event_text;

    std::optional<int> notevalue;
    std::optional<int64_t> notelength;
    std::optional<int> phrasevalue;
    std::optional<int64_t> phraselength;

    ChartDataEntry(const std::string& keystr_in, const std::string& valuestr_in);

    bool is_tick_data() const { return key_tick.has_value(); }
};

ChartDataEntry::ChartDataEntry(const std::string& keystr_in,
                               const std::string& valuestr_in) {
    std::string keystr = strip(keystr_in);
    std::string valuestr = strip(valuestr_in);

    int64_t k;
    if (try_parse_int(keystr, k))
        key_tick = k;
    else
        key_name = keystr;

    if (!key_tick.has_value()) {
        int64_t iv;
        if (try_parse_int(valuestr, iv))
            property_int = iv;
        else
            property_str = valuestr;
        return;
    }

    std::vector<std::string> t = split_ws(valuestr);
    if (t.empty()) return;
    const std::string& t0 = t[0];

    if (t0 == "TS" && t.size() == 2) {
        ts_numerator = std::stoi(t[1]);
        ts_denominator = 4;
    } else if (t0 == "TS" && t.size() == 3) {
        ts_numerator = std::stoi(t[1]);
        ts_denominator = 1 << std::stoi(t[2]);
    } else if (t0 == "B" && t.size() == 2) {
        tempo_bpm = static_cast<double>(std::stoll(t[1])) / 1000.0;
    } else if (t0 == "E" && t.size() == 2 && t[1] == "solo") {
        solo_start = true;
    } else if (t0 == "E" && t.size() == 2 && t[1] == "soloend") {
        solo_end = true;
    } else if (t0 == "E" && t.size() == 2 && full_match(t[1], re_disco_off())) {
        discoflip_disable = true;
    } else if (t0 == "E" && t.size() == 2 && full_match(t[1], re_disco_on())) {
        discoflip_enable = true;
    } else if (t0 == "E") {
        // Generic text event: no gameplay effect, but [Events] carries the
        // practice-section markers here.
        std::string rest = strip(valuestr.substr(1));
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
            rest = rest.substr(1, rest.size() - 2);
        event_text = rest;
    } else if (t0 == "N" && t.size() == 3) {
        notevalue = std::stoi(t[1]);
        notelength = std::stoll(t[2]);
    } else if (t0 == "S" && t.size() == 3) {
        phrasevalue = std::stoi(t[1]);
        phraselength = std::stoll(t[2]);
    }
}

struct ChartSection {
    std::string name;
    std::vector<int64_t> tick_order;
    std::unordered_map<int64_t, std::vector<ChartDataEntry>> tick_data;
    std::unordered_map<std::string, std::vector<ChartDataEntry>> prop_data;

    void add(const ChartDataEntry& e) {
        if (e.is_tick_data()) {
            int64_t key = *e.key_tick;
            if (tick_data.find(key) == tick_data.end())
                tick_order.push_back(key);
            tick_data[key].push_back(e);
        } else {
            prop_data[*e.key_name].push_back(e);
        }
    }
};

enum class CPhase { None, Time, Notes, NoteMods, Pre, Post, PostDelayed };

struct COp {
    CPhase phase = CPhase::None;
    std::function<void()> run;
};

class ChartParser {
public:
    Song parse(const std::vector<uint8_t>& data, bool pro, bool bass2x,
               Difficulty difficulty);

private:
    void load_sections(const std::vector<uint8_t>& data);
    COp optype(const ChartDataEntry& e, int64_t tick);
    void push_timestamp(int64_t tick, const std::vector<ChartDataEntry>& entries);

    void op_disco(bool on) { flag_disco_ = on; }
    void op_tempo(int64_t tick, double bpm) { song_->bpm_changes[tick] = bpm; }
    void op_timesig(int64_t tick, int numerator, int denominator) {
        song_->tpm_changes[tick] = song_->tick_resolution() *
                                   static_cast<int64_t>(numerator) * 4 /
                                   static_cast<int64_t>(denominator);
    }
    void op_fillstart(int64_t start, int64_t end) {
        fill_start_tick_ = start;
        fill_end_tick_ = end;
    }
    void op_fillend(int64_t starttick) {
        if (song_->sequence.empty()) return;
        SongTimestamp& last = song_->sequence.back();
        if (last.timecode.ticks() >= starttick)
            last.activation_length = last.timecode.ticks() - starttick;
    }
    void op_sp_start(int64_t start, int64_t end) {
        sp_start_tick_ = start;
        sp_end_tick_ = end;
    }
    void op_sp_end(int64_t starttick) {
        if (song_->sequence.empty()) {
            sp_end_tick_.reset();
            return;
        }
        if (song_->sequence.back().timecode.ticks() >= starttick) {
            song_->sequence.back().flag_sp = true;
            song_->sequence.back().sp_phrase_start = starttick;
        }
        sp_end_tick_.reset();
    }
    void op_solo(bool on) { flag_solo_ = on; }
    void op_note(NoteColor color) { chord_.add_note(color); }
    void op_2x() { chord_.add_2x(); }
    void op_accent(NoteColor color) { chord_.apply_accent(color); }
    void op_ghost(NoteColor color) { chord_.apply_ghost(color); }
    void op_cymbal(NoteColor color) { chord_.apply_cymbal(color); }

    Song* song_ = nullptr;
    bool mode_pro_ = false;
    bool mode_bass2x_ = false;

    std::unordered_map<std::string, ChartSection> sections_;

    Chord chord_;
    bool flag_solo_ = false;
    bool flag_disco_ = false;
    std::optional<int64_t> sp_start_tick_;
    std::optional<int64_t> sp_end_tick_;
    std::optional<int64_t> fill_start_tick_;
    std::optional<int64_t> fill_end_tick_;
};

void ChartParser::load_sections(const std::vector<uint8_t>& data) {
    // Split into lines on '\n' (a trailing '\r' is removed by strip).
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t b : data) {
        if (b == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(static_cast<char>(b));
        }
    }
    if (!cur.empty()) lines.push_back(cur);

    static const std::regex header_re(R"(\[.*\])");

    std::optional<ChartSection> wip;
    for (const std::string& raw : lines) {
        std::string line = strip(raw);
        if (wip.has_value()) {
            if (line == "{") {
                // block open
            } else if (line == "}") {
                sections_[wip->name] = std::move(*wip);
                wip.reset();
            } else {
                std::vector<std::string> parts = split_char(line, '=');
                std::string lhs = strip(parts[0]);
                std::string rhs = parts.size() > 1 ? strip(parts[1]) : "";
                wip->add(ChartDataEntry(lhs, rhs));
            }
        } else {
            std::smatch m;
            if (!std::regex_search(line, m, header_re))
                throw ChartFileError("expected a [section] header");
            std::string bracket = m.str(0);
            ChartSection s;
            s.name = bracket.substr(1, bracket.size() - 2);
            wip = std::move(s);
        }
    }
}

COp ChartParser::optype(const ChartDataEntry& e, int64_t tick) {
    if (e.discoflip_enable) return {CPhase::Pre, [this] { op_disco(true); }};
    if (e.discoflip_disable) return {CPhase::Pre, [this] { op_disco(false); }};
    if (e.tempo_bpm.has_value()) {
        double bpm = *e.tempo_bpm;
        return {CPhase::Time, [this, tick, bpm] { op_tempo(tick, bpm); }};
    }
    if (e.ts_numerator.has_value() && *e.ts_numerator != 0) {
        int n = *e.ts_numerator, d = *e.ts_denominator;
        return {CPhase::Time, [this, tick, n, d] { op_timesig(tick, n, d); }};
    }
    if (e.solo_start) return {CPhase::Pre, [this] { op_solo(true); }};
    if (e.solo_end) return {CPhase::Post, [this] { op_solo(false); }};

    if (e.notevalue.has_value()) {
        switch (*e.notevalue) {
            case 0: return {CPhase::Notes, [this] { op_note(NoteColor::Kick); }};
            case 1: return {CPhase::Notes, [this] { op_note(NoteColor::Red); }};
            case 2: return {CPhase::Notes, [this] { op_note(NoteColor::Yellow); }};
            case 3: return {CPhase::Notes, [this] { op_note(NoteColor::Blue); }};
            case 4: return {CPhase::Notes, [this] { op_note(NoteColor::Green); }};
            case 32:
                if (mode_bass2x_)
                    return {CPhase::Notes, [this] { op_2x(); }};
                return {};
            case 34: return {CPhase::NoteMods, [this] { op_accent(NoteColor::Red); }};
            case 35: return {CPhase::NoteMods, [this] { op_accent(NoteColor::Yellow); }};
            case 36: return {CPhase::NoteMods, [this] { op_accent(NoteColor::Blue); }};
            case 37: return {CPhase::NoteMods, [this] { op_accent(NoteColor::Green); }};
            case 40: return {CPhase::NoteMods, [this] { op_ghost(NoteColor::Red); }};
            case 41: return {CPhase::NoteMods, [this] { op_ghost(NoteColor::Yellow); }};
            case 42: return {CPhase::NoteMods, [this] { op_ghost(NoteColor::Blue); }};
            case 43: return {CPhase::NoteMods, [this] { op_ghost(NoteColor::Green); }};
            case 66:
                if (mode_pro_)
                    return {CPhase::NoteMods, [this] { op_cymbal(NoteColor::Yellow); }};
                return {};
            case 67:
                if (mode_pro_)
                    return {CPhase::NoteMods, [this] { op_cymbal(NoteColor::Blue); }};
                return {};
            case 68:
                if (mode_pro_)
                    return {CPhase::NoteMods, [this] { op_cymbal(NoteColor::Green); }};
                return {};
            default: return {};
        }
    }

    if (e.phrasevalue.has_value()) {
        if (*e.phrasevalue == 2) {
            int64_t len = *e.phraselength;
            return {CPhase::Pre,
                    [this, tick, len] { op_sp_start(tick, tick + len); }};
        }
        if (*e.phrasevalue == 64) {
            int64_t len = *e.phraselength;
            return {CPhase::PostDelayed,
                    [this, tick, len] { op_fillstart(tick, tick + len); }};
        }
    }
    return {};
}

void ChartParser::push_timestamp(int64_t tick,
                                 const std::vector<ChartDataEntry>& entries) {
    chord_ = Chord();

    std::vector<COp> ops;
    ops.reserve(entries.size());
    for (const ChartDataEntry& e : entries) {
        COp op = optype(e, tick);
        if (op.run) ops.push_back(std::move(op));
    }

    auto run_phase = [&ops](CPhase phase) {
        for (COp& op : ops) {
            if (op.phase != phase) continue;
            try {
                op.run();
            } catch (const ChartFileError&) {
            }
        }
    };

    run_phase(CPhase::Notes);
    run_phase(CPhase::NoteMods);

    // Phrase end: SP.
    if (sp_end_tick_.has_value() && tick >= *sp_end_tick_) {
        int64_t start = sp_start_tick_.value_or(0);
        ops.insert(ops.begin(),
                   COp{CPhase::Pre, [this, start] { op_sp_end(start); }});
    }

    // Phrase end: activation fill.
    if (chord_.count() && fill_end_tick_.has_value() &&
        tick >= *fill_end_tick_) {
        CPhase order = fill_lands_on_chord(*song_, *fill_end_tick_, tick)
                           ? CPhase::Post
                           : CPhase::Pre;
        int64_t start = *fill_start_tick_;
        ops.push_back(COp{order, [this, start] { op_fillend(start); }});
        fill_start_tick_.reset();
        fill_end_tick_.reset();
    }

    run_phase(CPhase::Pre);

    if (chord_.count())
        emit_chord_timestamp(*song_, chord_, tick, /*apply_flam=*/false,
                             mode_pro_ && flag_disco_, flag_solo_);

    run_phase(CPhase::Post);
    run_phase(CPhase::PostDelayed);
}

Song ChartParser::parse(const std::vector<uint8_t>& data, bool pro,
                        bool bass2x, Difficulty difficulty) {
    load_sections(data);
    mode_pro_ = pro;
    mode_bass2x_ = bass2x;

    const ChartSection& song_sec = sections_.at("Song");
    const ChartDataEntry& res_entry = song_sec.prop_data.at("Resolution").at(0);
    int64_t tick_resolution;
    if (res_entry.property_int.has_value())
        tick_resolution = *res_entry.property_int;
    else
        tick_resolution = std::stoll(*res_entry.property_str);

    Song song(tick_resolution);
    song_ = &song;

    // Map tempo and time signatures from the sync track.
    auto sync_it = sections_.find("SyncTrack");
    if (sync_it != sections_.end()) {
        const ChartSection& sync = sync_it->second;
        for (int64_t tk : sync.tick_order) {
            for (const ChartDataEntry& e : sync.tick_data.at(tk)) {
                COp op = optype(e, tk);
                if (op.phase == CPhase::Time && op.run) op.run();
            }
        }
    }
    song.build_timing();

    flag_solo_ = false;
    flag_disco_ = false;

    // Each difficulty is its own section ("ExpertDrums", "HardDrums", ...);
    // everything inside one — notes, dynamics, cymbals, SP, fills, solos —
    // follows for free. A chart missing the section parses as an empty song.
    auto ed_it = sections_.find(std::string(difficulty_name(difficulty)) + "Drums");
    if (ed_it != sections_.end()) {
        const ChartSection& ed = ed_it->second;
        for (int64_t tk : ed.tick_order)
            push_timestamp(tk, ed.tick_data.at(tk));
    }

    // Practice sections. tick_order follows the file, which is not required to
    // be sorted, so sort once at the end.
    auto ev_it = sections_.find("Events");
    if (ev_it != sections_.end()) {
        const ChartSection& ev = ev_it->second;
        for (int64_t tk : ev.tick_order) {
            for (const ChartDataEntry& e : ev.tick_data.at(tk)) {
                if (!e.event_text.has_value()) continue;
                std::string name;
                if (section_name_of(*e.event_text, &name))
                    song.practice_sections.push_back({tk, name});
            }
        }
        std::stable_sort(song.practice_sections.begin(),
                         song.practice_sections.end(),
                         [](const SongSection& a, const SongSection& b) {
                             return a.tick < b.tick;
                         });
    }

    song.check_activations();
    return song;
}

}  // namespace

// ---- public loaders -----------------------------------------------------

Song load_songbytes_mid(const std::vector<uint8_t>& data, bool pro,
                        bool bass2x, Difficulty difficulty) {
    MidiFile mid(data);
    return MidiParser().parse(mid, pro, bass2x, difficulty);
}

Song load_songbytes_chart(const std::vector<uint8_t>& data, bool pro,
                          bool bass2x, Difficulty difficulty) {
    return ChartParser().parse(data, pro, bass2x, difficulty);
}

Song load_songpath_mid(const std::string& path, bool pro, bool bass2x,
                       Difficulty difficulty) {
    MidiFile mid = MidiFile::from_file(path);
    return MidiParser().parse(mid, pro, bass2x, difficulty);
}

Song load_songpath_chart(const std::string& path, bool pro, bool bass2x,
                         Difficulty difficulty) {
    std::vector<uint8_t> data = read_file_bytes(path);
    return ChartParser().parse(data, pro, bass2x, difficulty);
}

Song load_songpath_sng(const std::string& path, bool pro, bool bass2x,
                       Difficulty difficulty) {
    std::vector<uint8_t> buf = read_file_bytes(path);

    auto read_u64 = [&buf](size_t pos) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(buf[pos + i]) << (8 * i);
        return v;
    };

    const size_t XORMASK_OFFSET = 10;
    uint8_t xormask[16];
    std::memcpy(xormask, buf.data() + XORMASK_OFFSET, 16);

    size_t pos = XORMASK_OFFSET + 16;
    uint64_t metadata_len = read_u64(pos);
    pos += 8;
    pos += static_cast<size_t>(metadata_len);

    pos += 8;  // skip section length
    uint64_t file_count = read_u64(pos);
    pos += 8;

    enum class Loader { None, Mid, Chart } loader = Loader::None;
    uint64_t chart_len = 0, chart_off = 0;

    for (uint64_t i = 0; i < file_count; ++i) {
        uint8_t filename_len = buf[pos];
        pos += 1;
        std::string filename(reinterpret_cast<const char*>(buf.data() + pos),
                             filename_len);
        pos += filename_len;
        std::string fn = ascii_casefold(filename);
        uint64_t contents_len = read_u64(pos);
        pos += 8;
        uint64_t contents_index = read_u64(pos);
        pos += 8;

        if (fn == "notes.mid") {
            loader = Loader::Mid;
            chart_len = contents_len;
            chart_off = contents_index;
            break;
        } else if (fn == "notes.chart") {
            loader = Loader::Chart;
            chart_len = contents_len;
            chart_off = contents_index;
        }
    }

    if (loader == Loader::None)
        throw std::runtime_error("No chart files found in SNG file.");

    std::vector<uint8_t> notebytes(static_cast<size_t>(chart_len));
    for (uint64_t i = 0; i < chart_len; ++i) {
        uint8_t xorkey =
            xormask[i % 16] ^ static_cast<uint8_t>(i & 0xff);
        notebytes[static_cast<size_t>(i)] =
            buf[static_cast<size_t>(chart_off + i)] ^ xorkey;
    }

    if (loader == Loader::Mid)
        return load_songbytes_mid(notebytes, pro, bass2x, difficulty);
    return load_songbytes_chart(notebytes, pro, bass2x, difficulty);
}

Song load_songpath_srb(const std::string& path, bool pro, bool bass2x,
                       Difficulty difficulty) {
    std::vector<uint8_t> buf = read_file_bytes(path);
    if (buf.size() <= kSrbHeaderSize)
        throw std::runtime_error("Truncated SRB file.");

    // Stream 1 (metadata) names the notes file; stream 2 is its bytes.
    size_t notes_offset = 0;
    std::vector<uint8_t> meta = srb_inflate_stream(
        buf.data(), buf.size(), kSrbHeaderSize, kSrbMaxMetadata, &notes_offset);
    SrbMetadata md;
    srb_parse_metadata(meta, md);

    // The notes file inflates to well under a hundred MB even for mega-charts;
    // a 1 GB ceiling only exists to bound hostile input.
    std::vector<uint8_t> notebytes = srb_inflate_stream(
        buf.data(), buf.size(), notes_offset, size_t{1} << 30, nullptr);

    std::string fn = ascii_casefold(md.notes_filename);
    auto fn_ends_with = [&fn](const char* suf) {
        size_t n = std::strlen(suf);
        return fn.size() >= n && fn.compare(fn.size() - n, n, suf) == 0;
    };
    bool is_mid;
    if (fn_ends_with(".mid"))
        is_mid = true;
    else if (fn_ends_with(".chart"))
        is_mid = false;
    else  // Unexpected filename: sniff the payload instead.
        is_mid = notebytes.size() >= 4 && std::memcmp(notebytes.data(), "MThd", 4) == 0;

    if (is_mid) return load_songbytes_mid(notebytes, pro, bass2x, difficulty);
    return load_songbytes_chart(notebytes, pro, bass2x, difficulty);
}

Song load_songpath(const std::string& path, bool pro, bool bass2x,
                   Difficulty difficulty) {
    std::string low = ascii_casefold(path);
    auto ends_with = [&low](const char* suf) {
        size_t n = std::strlen(suf);
        return low.size() >= n && low.compare(low.size() - n, n, suf) == 0;
    };
    if (ends_with(".mid")) return load_songpath_mid(path, pro, bass2x, difficulty);
    if (ends_with(".chart")) return load_songpath_chart(path, pro, bass2x, difficulty);
    if (ends_with(".sng")) return load_songpath_sng(path, pro, bass2x, difficulty);
    if (ends_with(".srb")) return load_songpath_srb(path, pro, bass2x, difficulty);
    throw std::runtime_error("unexpected chart type: " + path);
}

}  // namespace hydra
