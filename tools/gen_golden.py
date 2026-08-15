"""Generate the frozen golden oracle for the Hydra C++ port.

The port is gated, phase by phase, against the output of today's *pure-Python*
Hydra over the whole `test/input/` corpus. This script emits that output as
deterministic JSON so each C++ module can diff its result against a fixed
reference instead of against a live Python run.

    .venv\\Scripts\\python.exe tools\\gen_golden.py            # (re)generate golden/
    .venv\\Scripts\\python.exe tools\\gen_golden.py --check     # prove it is stable

Everything is produced from the pure-Python path (HYDRA_NO_NATIVE=1), because
the oracle is the *reference semantics*, not the current C++ accelerator (which
the port must also match anyway). The C++ tests never import Python; they read
these files.

Layout produced:
    golden/index.json              sorted list of chart relpaths + which blocks exist
    golden/chord_encode.json       hyencode.CHORD_ENCODE (hash -> string) for Phase 1
    golden/charts/<slug>.json      one file per chart (timing / midi / analysis)

Floats are stored as their Python repr() string (shortest round-trippable form)
so the value survives JSON exactly; the C++ side parses the string with strtod
and compares doubles bit-for-bit. Determinism: charts sorted, JSON keys sorted,
floats via repr, and the uncapped SP-cap time budget disabled so the ladder
always runs to the same rung regardless of machine speed.
"""

import argparse
import glob
import json
import os
import pathlib
import sys
import tempfile

# Force the pure-Python path before hydra imports anything native. hynative
# reads this at import time; setting it here makes the whole run the oracle.
os.environ["HYDRA_NO_NATIVE"] = "1"

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))

# Chart names carry non-cp1252 characters (e.g. a fullwidth slash); keep the
# progress prints from dying on the Windows console codepage.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

import hydra.hymisc as hymisc          # noqa: E402
import hydra.hynative as hynative      # noqa: E402
import hydra.hyencode as hyencode      # noqa: E402
import hydra.hysong as hysong          # noqa: E402
import hydra.hyutil as hyutil          # noqa: E402
import hydra.hystore as hystore        # noqa: E402

# Belt and suspenders: even if the env var were missed, keep both native paths
# off for the entire generation.
hynative.ENABLED = False
hynative.SEARCH_ENABLED = False

INPUT_ROOT = ROOT / "test" / "input"
GOLDEN_ROOT = ROOT / "golden"

# Fixed analysis settings, matching the regression / search-parity suites.
DIFFICULTY, PRO, BASS2X = "Expert", True, True

# The config matrix. Each entry drives one analysis. `edition` picks the SP
# meter ceiling: capped is what ships (4 bars), uncapped raises it up a ladder.
# Mirrors what test_regression and test_search_parity exercise: the default
# depth, the shallow depths that make reduction actually drop paths, points
# mode (the other branch of every depth comparison), and the ms filter (the one
# place the search compares floats). Uncapped is only run at the default depth,
# because its ladder calls the search several times per chart.
ANALYSIS_MATRIX = [
    {"key": "capped.scores.10", "edition": "capped",
     "d_mode": "scores", "d_value": 10, "ms_filter": None},
    {"key": "capped.scores.200", "edition": "capped",
     "d_mode": "scores", "d_value": 200, "ms_filter": None},
    {"key": "capped.scores.0", "edition": "capped",
     "d_mode": "scores", "d_value": 0, "ms_filter": None},
    {"key": "capped.scores.1", "edition": "capped",
     "d_mode": "scores", "d_value": 1, "ms_filter": None},
    {"key": "capped.scores.3", "edition": "capped",
     "d_mode": "scores", "d_value": 3, "ms_filter": None},
    {"key": "capped.points.5000", "edition": "capped",
     "d_mode": "points", "d_value": 5000, "ms_filter": None},
    {"key": "capped.scores.200.ms5", "edition": "capped",
     "d_mode": "scores", "d_value": 200, "ms_filter": 5.0},
    {"key": "capped.scores.200.ms20", "edition": "capped",
     "d_mode": "scores", "d_value": 200, "ms_filter": 20.0},
    {"key": "uncapped.scores.200", "edition": "uncapped",
     "d_mode": "scores", "d_value": 200, "ms_filter": None},
]


def fenc(x):
    """Encode a value for the golden JSON, floats as their exact repr string.

    Python's repr() of a float is the shortest string that round-trips, so
    strtod(repr(d)) == d. The C++ reader parses these back and compares the
    double it computed itself against this one bit-for-bit.
    """
    if isinstance(x, float):
        return repr(x)
    if isinstance(x, (list, tuple)):
        return [fenc(v) for v in x]
    if isinstance(x, dict):
        return {k: fenc(v) for k, v in x.items()}
    return x


def chart_files():
    found = []
    for ext in ("mid", "chart", "sng"):
        found += glob.glob(str(INPUT_ROOT / "**" / f"*.{ext}"), recursive=True)
    return sorted(found)


def relpath_of(path):
    return pathlib.PurePath(os.path.relpath(path, INPUT_ROOT)).as_posix()


def slug_of(relpath):
    """A filesystem-safe, collision-free filename for a chart's golden file."""
    out = []
    for ch in relpath:
        out.append(ch if (ch.isalnum() or ch in "-_.") else "_")
    return "".join(out)


def load_songpath(path):
    low = path.casefold()
    if low.endswith(".mid"):
        return hysong.load_songpath_mid(path, DIFFICULTY, PRO, BASS2X)
    if low.endswith(".chart"):
        return hysong.load_songpath_chart(path, DIFFICULTY, PRO, BASS2X)
    if low.endswith(".sng"):
        return hysong.load_songpath_sng(path, DIFFICULTY, PRO, BASS2X)
    raise ValueError(f"unexpected chart type: {path}")


# ---- timing block (feeds Phase 1 core/timing) ---------------------------

def sample_ticks(song):
    """Ticks worth converting: boundaries, edges, and a spread of real notes.

    Boundaries and their neighbours catch off-by-one section handling; a few
    negative ticks exercise the before-the-song branch; a stride over the real
    note ticks catches accumulation drift over the length of the chart.
    """
    ticks = set()

    for key in list(song.tpm_changes.keys()) + list(song.bpm_changes.keys()):
        ticks.update((key - 1, key, key + 1))

    res = song.tick_resolution
    ticks.update((-1, -res, -res * 4, 0))

    note_ticks = [ts.timecode.ticks for ts in song._sequence
                  if ts.timecode is not None]
    if note_ticks:
        last = note_ticks[-1]
        ticks.update((last - 1, last, last + 1, last + res))
        stride = max(1, len(note_ticks) // 300)
        ticks.update(note_ticks[::stride])

    return sorted(ticks)


def timing_block(song):
    tpm = {int(k): v for k, v in song.tpm_changes.items()}
    bpm = {int(k): v for k, v in song.bpm_changes.items()}

    samples = []
    for tick in sample_ticks(song):
        entry = {"tick": int(tick)}
        # ms is defined for every tick, including before the first tempo mark.
        tc = hymisc.Timecode(tick, song.tick_resolution,
                             song.tpm_changes, song.bpm_changes)
        entry["ms"] = fenc(tc.ms)
        # The measure/beat/tick breakdown is only meaningful from tick 0 on;
        # negative ticks take Python's floor-division branch which is not part
        # of what the app ever asks for.
        if tick >= 0:
            entry["mbt"] = [int(v) for v in tc.measure_beats_ticks]
            entry["measures_decimal"] = fenc(tc.measures_decimal)
        samples.append(entry)

    return {
        "res": int(song.tick_resolution),
        "tpm": fenc({str(k): tpm[k] for k in sorted(tpm)}),
        "bpm": fenc({str(k): bpm[k] for k in sorted(bpm)}),
        "samples": samples,
    }


# ---- midi block (feeds Phase 1 parse/midi) ------------------------------

# The metas hysong can match, split by which attribute carries the string --
# the same split test_midi_parity.event_view uses.
_TEXT_METAS = {"text", "copyright", "lyrics", "marker", "cue_marker"}
_NAME_METAS = {"track_name", "instrument_name", "device_name"}


def event_view(tracks):
    """Absolute-tick view of everything hysong can act on.

    Mirrors test/test_midi_parity.py:event_view exactly, so the C++ midi
    reader can be diffed against this without needing mido.
    """
    view = []
    for track in tracks:
        tick = 0
        events = []
        for msg in track:
            tick += msg.time
            mtype = getattr(msg, "type", None)
            if mtype == "note_on":
                events.append([tick, "note_on", msg.note, msg.velocity])
            elif mtype == "note_off":
                events.append([tick, "note_off", msg.note, msg.velocity])
            elif mtype == "set_tempo":
                events.append([tick, "set_tempo", msg.tempo])
            elif mtype == "time_signature":
                events.append([tick, "time_signature",
                               msg.numerator, msg.denominator])
            elif mtype in _TEXT_METAS:
                events.append([tick, mtype, "text", msg.text])
            elif mtype in _NAME_METAS:
                events.append([tick, mtype, "name", msg.name])
        view.append(events)
    return view


def midi_block(path):
    import hydra.hymidi as hymidi
    with open(path, "rb") as f:
        raw = f.read()
    mid = hymidi.MidiFile(data=raw)
    return {
        "ticks_per_beat": int(mid.ticks_per_beat),
        "track_names": [t.name for t in mid.tracks],
        "events": event_view(mid.tracks),
    }


# ---- song block (feeds Phase 2 parse/song + core/model) -----------------

def song_block(song):
    """The parsed timestamp sequence: enough to diff the parsers and the chord
    codes independently of the (heavier) analysis block.

    Each event is a chord's tick, its Chord.code() (which exercises the whole
    ChordNote/Chord hash + encode-table path), and the gameplay flags. features
    carries whatever check_activations tagged the song with.
    """
    events = []
    for ts in song._sequence:
        events.append({
            "tick": ts.timecode.ticks,
            "code": ts.chord.code(),
            "flag_solo": ts.flag_solo,
            "flag_sp": ts.flag_sp,
            "activation_length": ts.activation_length,
        })
    return {"features": list(song.features), "events": events}


# ---- analysis block (feeds Phases 3-4, and validates the whole spine) ----

def encode_activation(act):
    return {
        "skips": act.skips,
        "sp_meter": act.sp_meter,
        "e_offset": fenc(act.e_offset),
        "frontend_points": act.frontend_points,
        "timecode_ticks": (act.timecode.ticks
                           if act.timecode is not None else None),
        "backends_count": len(act.backends),
        "notationstr": act.notationstr(),
        "sqinouts": [[type(s).__name__, fenc(s.offset)]
                     for s in act.sqinouts],
    }


def encode_path(path):
    return {
        "totalscore": path.totalscore(),
        "pathstring": path.pathstring(),
        "pathstring_verbose": path.pathstring_verbose(),
        "tied_pathcount": path.tied_pathcount(),
        "notecount": path.notecount,
        "leftover_sp": path.leftover_sp,
        "skipped_accents": path.skipped_accents,
        "skipped_ghosts": path.skipped_ghosts,
        "score_base": path.score_base,
        "score_combo": path.score_combo,
        "score_sp": path.score_sp,
        "score_solo": path.score_solo,
        "score_accents": path.score_accents,
        "score_ghosts": path.score_ghosts,
        "var_point": path.var_point,
        "activations": [encode_activation(a) for a in path.all_activations()],
        "variants": [encode_path(v) for v in path.variants],
    }


def store_block(record):
    """Mirrors hystore.prepare_row + summarize_record — the Phase 4 gate."""
    summary = hystore.summarize_record(record)
    bestpath = ""
    if record.is_version_compatible() and record._paths:
        bestpath = record.best_path().pathstring()

    return {
        "hyversion": list(record.hyversion),
        "bestpath": bestpath,
        "score": summary["score"],
        "actcount": summary["actcount"],
        "maxskip": summary["maxskip"],
        "hardest_ms": fenc(summary["hardest_ms"]),
        "avgmult": fenc(summary["avgmult"]),
        "notecount": summary["notecount"],
        "sqin_count": summary["sqin_count"],
        "sqout_count": summary["sqout_count"],
        "pathcount": summary["pathcount"],
    }


def run_analysis(path, cfg):
    # apply_edition keeps SP_METER_CAP, RECORD_VERSION and UNCAPPED_SP in
    # lockstep (see hymisc.apply_edition) so record.hyversion — which the
    # store block below depends on — matches what a real run of this edition
    # would stamp, not whatever edition the process happened to start in.
    saved_uncapped = hymisc.UNCAPPED_SP
    hymisc.apply_edition(cfg["edition"] == "uncapped")
    try:
        record = hyutil.analyze_chart_file(
            path, DIFFICULTY, PRO, BASS2X,
            cfg["d_mode"], cfg["d_value"], cfg["ms_filter"],
        )
        # store_block reads record.is_version_compatible(), which compares
        # against the *currently applied* hymisc.RECORD_VERSION -- has to run
        # before apply_edition reverts it below, or an uncapped record reads
        # back against the capped version and looks stale.
        result = {
            "sp_cap": record.sp_cap,
            "sp_cap_converged": record.sp_cap_converged,
            "paths": [encode_path(p) for p in record._paths],
            "store": store_block(record),
        }
    finally:
        hymisc.apply_edition(saved_uncapped)

    return result


def analysis_block(path):
    out = {}
    for cfg in ANALYSIS_MATRIX:
        try:
            out[cfg["key"]] = run_analysis(path, cfg)
        except hymisc.ChartFileError as e:
            out[cfg["key"]] = {"error": str(e)}
    return out


# ---- driver -------------------------------------------------------------

def build_chart(path):
    relpath = relpath_of(path)
    doc = {"relpath": relpath,
           "filetype": pathlib.PurePath(path).suffix.lower().lstrip(".")}

    song = load_songpath(path)
    doc["timing"] = timing_block(song)
    doc["song"] = song_block(song)

    if path.casefold().endswith(".mid"):
        doc["midi"] = midi_block(path)

    doc["analysis"] = analysis_block(path)
    return doc


def dump_json(obj):
    return json.dumps(obj, sort_keys=True, indent=2, ensure_ascii=True)


def generate(dest_root):
    charts_dir = dest_root / "charts"
    charts_dir.mkdir(parents=True, exist_ok=True)

    # Keep the uncapped ladder deterministic: with the time budget disabled it
    # always climbs to the same settling rung, no matter how fast this machine
    # is. Otherwise a slow run could abandon a rung a fast run finishes.
    saved_budget = hymisc.SP_CAP_TIME_BUDGET
    hymisc.SP_CAP_TIME_BUDGET = 0

    index = []
    try:
        files = chart_files()
        for i, path in enumerate(files, 1):
            relpath = relpath_of(path)
            slug = slug_of(relpath)
            print(f"[{i}/{len(files)}] {relpath}", flush=True)
            doc = build_chart(path)
            (charts_dir / f"{slug}.json").write_text(
                dump_json(doc), encoding="utf-8")
            index.append({"relpath": relpath, "slug": slug,
                          "has_midi": "midi" in doc})
    finally:
        hymisc.SP_CAP_TIME_BUDGET = saved_budget

    (dest_root / "index.json").write_text(
        dump_json({"charts": index}), encoding="utf-8")

    (dest_root / "chord_encode.json").write_text(
        dump_json({str(k): v for k, v in hyencode.CHORD_ENCODE.items()}),
        encoding="utf-8")


def check():
    """Regenerate into a temp dir and diff against golden/, to prove stability."""
    if not GOLDEN_ROOT.exists():
        print("error: golden/ does not exist; run without --check first",
              file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp_root = pathlib.Path(tmp)
        generate(tmp_root)

        # report_meta.json belongs to tools/gen_golden_report.py, not this
        # generator; leave it out of the stability diff.
        old = {p.relative_to(GOLDEN_ROOT).as_posix()
               for p in GOLDEN_ROOT.rglob("*.json")
               if not p.name.startswith("report")}
        new = {p.relative_to(tmp_root).as_posix()
               for p in tmp_root.rglob("*.json")}

        diffs = []
        for name in sorted(old ^ new):
            diffs.append(f"  only in {'golden' if name in old else 'rebuild'}: {name}")
        for name in sorted(old & new):
            a = (GOLDEN_ROOT / name).read_bytes()
            b = (tmp_root / name).read_bytes()
            if a != b:
                diffs.append(f"  differs: {name}")

        if diffs:
            print("golden is NOT stable:", file=sys.stderr)
            print("\n".join(diffs), file=sys.stderr)
            return 1

    print("golden is stable: rebuild is byte-identical.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="regenerate to a temp dir and diff against golden/")
    args = ap.parse_args()

    if not INPUT_ROOT.exists():
        print(f"error: corpus not found at {INPUT_ROOT}", file=sys.stderr)
        return 2

    if args.check:
        return check()

    generate(GOLDEN_ROOT)
    print(f"wrote golden to {GOLDEN_ROOT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
