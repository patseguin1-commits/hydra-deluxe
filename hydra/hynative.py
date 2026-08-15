"""Loader for Hydra's native scoring core.

Chart analysis spends most of its non-parsing time in
``hypath.category_scores``. This module loads a C++ build of that function
from a plain C-ABI shared library and exposes it as ``category_scores``.

The library is optional. If it is missing, stale, or fails its self-test,
``AVAILABLE`` is False and callers use the pure-Python implementation. That
is not a fallback bolted on for safety alone -- it is how the tree stays
runnable on a machine with no C++ toolchain, and how the frozen app keeps
working if the DLL is left out of a build.

A C ABI is used rather than a CPython extension module because it does not
have to match CPython's ABI: the same DLL keeps working across Python
upgrades, and it can be produced by a portable MinGW that needs no admin
rights to install.

Set HYDRA_NO_NATIVE=1 to force the pure-Python path (used by the parity
harness to compare the two against each other). The path search runs natively
by default when the DLL is present; set HYDRA_NO_NATIVE_SEARCH=1 to keep the
DLL loaded but force the pure-Python search specifically.

"""

import ctypes
import os
import pathlib
import sys

from . import hydata

# Must match HY_ABI_VERSION in native/hydra_score.h.
ABI_VERSION = 4

# Must match the HY_NOTE_* flags in native/hydra_score.h.
NOTE_CYMBAL = 0x01
NOTE_ACCENT = 0x02
NOTE_GHOST = 0x04
NOTE_ACTIVATION = 0x08

HY_OK = 0

# Must match HY_NO_TIME in native/hydra_search.h and hyflat.NO_TIME.
NO_TIME = -1

# Must match the HY_DEPTH_* constants in native/hydra_search.h.
DEPTH_MODES = {'scores': 0, 'points': 1}
DEPTH_OTHER = 2

# Must match HY_SQ_IN / HY_SQ_OUT.
SQ_IN = 0
SQ_OUT = 1


class HyScores(ctypes.Structure):
    """Mirrors struct hy_scores. Field order is part of the ABI."""

    _fields_ = [
        ("base", ctypes.c_int32),
        ("combo", ctypes.c_int32),
        ("sp", ctypes.c_int32),
        ("accent", ctypes.c_int32),
        ("ghost", ctypes.c_int32),
        ("sqout_reduction", ctypes.c_int32),
        ("skipped_dynamic_reduction", ctypes.c_int32),
    ]


class HyNode(ctypes.Structure):
    _fields_ = [
        ("tick", ctypes.c_int64),
        ("base_suffix", ctypes.c_int64),
        ("max_suffix", ctypes.c_int64),
        ("total_spscore_suffix", ctypes.c_int64),
        ("max_spscore_density", ctypes.c_double),
        ("adv_edge", ctypes.c_int32),
        ("branch_edge", ctypes.c_int32),
        ("is_sp", ctypes.c_int32),
        ("remaining_sp_phrases", ctypes.c_int32),
    ]


class HySpTime(ctypes.Structure):
    _fields_ = [
        ("tick", ctypes.c_int64),
        ("ms", ctypes.c_double),
        ("map_begin", ctypes.c_int32),
        ("map_end", ctypes.c_int32),
    ]


class HyBackend(ctypes.Structure):
    _fields_ = [
        ("tick", ctypes.c_int64),
        ("offset_ms", ctypes.c_double),
        ("points", ctypes.c_int32),
        ("sqout_points", ctypes.c_int32),
    ]


class HyEdge(ctypes.Structure):
    _fields_ = [
        ("dest", ctypes.c_int32),
        ("notecount", ctypes.c_int32),
        ("basescore", ctypes.c_int32),
        ("comboscore", ctypes.c_int32),
        ("spscore", ctypes.c_int32),
        ("soloscore", ctypes.c_int32),
        ("accentscore", ctypes.c_int32),
        ("ghostscore", ctypes.c_int32),
        ("frontend_points", ctypes.c_int32),
        ("frontend_is_accent", ctypes.c_int32),
        ("frontend_is_ghost", ctypes.c_int32),
        ("skipped_dynamic_points", ctypes.c_int32),
        ("late_sqin_count", ctypes.c_int32),
        ("sp_begin", ctypes.c_int32),
        ("sp_end", ctypes.c_int32),
        ("aiet_begin", ctypes.c_int32),
        ("aiet_end", ctypes.c_int32),
        ("be_begin", ctypes.c_int32),
        ("be_end", ctypes.c_int32),
        ("activation_fill_deadline_ms", ctypes.c_double),
        ("sqinout_timing", ctypes.c_double),
        ("sqinout_time", ctypes.c_int64),
        ("sqout_time", ctypes.c_int64),
        ("sqin_time", ctypes.c_int64),
    ]


PROGRESS_FN = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_int32, ctypes.c_double)


class HySearchIn(ctypes.Structure):
    _fields_ = [
        ("nodes", ctypes.POINTER(HyNode)),
        ("edges", ctypes.POINTER(HyEdge)),
        ("sptimes", ctypes.POINTER(HySpTime)),
        ("ext_key", ctypes.POINTER(ctypes.c_int64)),
        ("ext_val", ctypes.POINTER(ctypes.c_int64)),
        ("backends", ctypes.POINTER(HyBackend)),
        ("aiet", ctypes.POINTER(ctypes.c_int64)),
        ("n_nodes", ctypes.c_int32),
        ("n_edges", ctypes.c_int32),
        ("n_sptimes", ctypes.c_int32),
        ("n_ext", ctypes.c_int32),
        ("n_backends", ctypes.c_int32),
        ("n_aiet", ctypes.c_int32),
        ("start_node", ctypes.c_int32),
        ("has_sp_cap", ctypes.c_int32),
        ("sp_cap", ctypes.c_int32),
        ("depth_mode", ctypes.c_int32),
        ("depth_value", ctypes.c_int32),
        ("has_ms_filter", ctypes.c_int32),
        ("ms_filter", ctypes.c_double),
        ("flag_skipped_dynamics", ctypes.c_int32),
        ("graph_length", ctypes.c_int32),
        ("enable_bound_prune", ctypes.c_int32),
        ("_pad2", ctypes.c_int32),
        ("progress", PROGRESS_FN),
    ]


class HyOutSq(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int32),
        ("_pad", ctypes.c_int32),
        ("offset", ctypes.c_double),
    ]


class HyOutAct(ctypes.Structure):
    _fields_ = [
        ("act_node", ctypes.c_int32),
        ("skips", ctypes.c_int32),
        ("sp_meter", ctypes.c_int32),
        ("deact_edge", ctypes.c_int32),
        ("sq_begin", ctypes.c_int32),
        ("sq_end", ctypes.c_int32),
        ("e_offset", ctypes.c_double),
    ]


class HyOutPath(ctypes.Structure):
    _fields_ = [
        ("score_base", ctypes.c_int32),
        ("score_combo", ctypes.c_int32),
        ("score_sp", ctypes.c_int32),
        ("score_solo", ctypes.c_int32),
        ("score_accents", ctypes.c_int32),
        ("score_ghosts", ctypes.c_int32),
        ("notecount", ctypes.c_int32),
        ("leftover_sp", ctypes.c_int32),
        ("skipped_accents", ctypes.c_int32),
        ("skipped_ghosts", ctypes.c_int32),
        ("var_point", ctypes.c_int32),
        ("depth", ctypes.c_int32),
        ("act_begin", ctypes.c_int32),
        ("act_end", ctypes.c_int32),
    ]


class HySearchOut(ctypes.Structure):
    _fields_ = [
        ("paths", ctypes.POINTER(HyOutPath)),
        ("acts", ctypes.POINTER(HyOutAct)),
        ("sqs", ctypes.POINTER(HyOutSq)),
        ("n_paths", ctypes.c_int32),
        ("n_acts", ctypes.c_int32),
        ("n_sqs", ctypes.c_int32),
        ("iterations", ctypes.c_int32),
        ("handle", ctypes.c_void_p),
    ]


def _library_name():
    if sys.platform == "win32":
        return "hydra_score.dll"
    if sys.platform == "darwin":
        return "libhydra_score.dylib"
    return "libhydra_score.so"


def _search_paths():
    """Where to look for the library, nearest build first.

    When frozen, PyInstaller unpacks data files next to the executable's
    _internal directory, which is what sys._MEIPASS points at; from source
    the build lands in native/build.

    """
    name = _library_name()
    here = pathlib.Path(__file__).resolve().parent
    root = here.parent

    roots = []
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        roots.append(pathlib.Path(meipass))
    roots += [root / "native" / "build", root / "native", root, here]

    return [r / name for r in roots]


def _load():
    """Return (lib, path) or (None, reason)."""
    if os.environ.get("HYDRA_NO_NATIVE"):
        return None, "disabled by HYDRA_NO_NATIVE"

    tried = []
    for candidate in _search_paths():
        if not candidate.exists():
            tried.append(str(candidate))
            continue
        try:
            lib = ctypes.CDLL(str(candidate))
        except OSError as e:
            return None, f"{candidate} failed to load: {e}"

        try:
            lib.hy_abi_version.restype = ctypes.c_int32
            lib.hy_abi_version.argtypes = []

            lib.hy_category_scores.restype = ctypes.c_int32
            lib.hy_category_scores.argtypes = [
                ctypes.POINTER(ctypes.c_uint8),
                ctypes.c_int32,
                ctypes.c_int32,
                ctypes.c_int32,
                ctypes.POINTER(HyScores),
            ]

            lib.hy_search.restype = ctypes.c_int32
            lib.hy_search.argtypes = [
                ctypes.POINTER(HySearchIn), ctypes.POINTER(HySearchOut),
            ]
            lib.hy_dp_search.restype = ctypes.c_int32
            lib.hy_dp_search.argtypes = [
                ctypes.POINTER(HySearchIn), ctypes.POINTER(HySearchOut),
            ]
            lib.hy_search_free.restype = None
            lib.hy_search_free.argtypes = [ctypes.POINTER(HySearchOut)]
        except AttributeError as e:
            return None, f"{candidate} is missing an expected symbol: {e}"

        found = lib.hy_abi_version()
        if found != ABI_VERSION:
            return None, (
                f"{candidate} has ABI {found}, expected {ABI_VERSION}"
                " -- rebuild it with: python native/build_native.py"
            )

        return lib, str(candidate)

    return None, "no library found in: " + ", ".join(tried)


_LIB, _STATUS = _load()


def _selftest():
    """One known chord, checked against the values the Python produces.

    A wrong-but-loadable DLL is worse than no DLL, so a failed self-test
    disables the native path rather than trusting it.

    """
    if _LIB is None:
        return False

    # Two notes: a plain cymbal then an accent note, at combo 0.
    flags = (ctypes.c_uint8 * 2)(NOTE_CYMBAL, NOTE_ACCENT)
    out = HyScores()
    rc = _LIB.hy_category_scores(flags, 2, 0, 0, ctypes.byref(out))
    if rc != HY_OK:
        return False

    # base: 50+50 notes, +15 cymbal, +0 dynamic cymbal
    # combo: multiplier is 1 for both notes, so no combo points
    # sp: base_note+sp duplicates -> (50+50)+(15)+0+0+(50)+(0)+0+0
    # accent: 50 from the accent note
    return (
        out.base == 115
        and out.combo == 0
        and out.sp == 165
        and out.accent == 50
        and out.ghost == 0
        and out.sqout_reduction == 65
        and out.skipped_dynamic_reduction == 0
    )


if _LIB is not None and not _selftest():
    _LIB, _STATUS = None, "self-test failed; using pure Python"

# The library loaded and agrees with the Python.
AVAILABLE = _LIB is not None
STATUS = _STATUS

# Whether callers should actually use it. Measured on the 98-chart corpus,
# the native path is ~1.6% *slower* than pure Python: category_scores is only
# ~13% of an analysis, it costs ~5us per call in Python, and a ctypes call
# costs 1-2us of marshalling on top of the ~1us of work -- so the boundary
# eats more than the C++ saves.
#
# It is therefore off unless HYDRA_NATIVE=1 is set. The code stays because
# the measurement is the useful part: it says the win is not in this function
# but in moving a coarser unit of work across the boundary, where the
# per-call cost would be amortised instead of dominant.
ENABLED = AVAILABLE and bool(os.environ.get("HYDRA_NATIVE"))

# Whether the native path search is used. Separate from ENABLED above because
# the two are different bets: category_scores crosses the boundary per chord
# and loses, the search crosses it once per chart and wins. Measured ~1.59x
# capped / ~2.56x uncapped end-to-end, so it is default-on when the DLL is
# present; set HYDRA_NO_NATIVE_SEARCH=1 to opt out (falls back to pure Python).
SEARCH_ENABLED = AVAILABLE and not bool(os.environ.get("HYDRA_NO_NATIVE_SEARCH"))


def note_flags(note, activation_note=None):
    """Pack one note's dynamic/cymbal state into a flag byte."""
    f = 0
    if note.is_cymbal():
        f |= NOTE_CYMBAL
    if note.is_accent():
        f |= NOTE_ACCENT
    if note.is_ghost():
        f |= NOTE_GHOST
    if activation_note is not None and note is activation_note:
        f |= NOTE_ACTIVATION
    return f


def category_scores(ordering, combo, flag_skipped_dynamics, activation_note):
    """Native category_scores. Returns the same dict the Python one does.

    ``ordering`` must already be base-sorted; the sort stays in Python
    because it is stable and the tie order is observable through the SqOut
    value, which reads note 0.

    """
    n = len(ordering)
    buf = (ctypes.c_uint8 * n)()
    for i, note in enumerate(ordering):
        buf[i] = note_flags(note, activation_note)

    out = HyScores()
    rc = _LIB.hy_category_scores(
        buf, n, combo, 1 if flag_skipped_dynamics else 0, ctypes.byref(out)
    )
    if rc != HY_OK:
        raise RuntimeError(f"hy_category_scores failed with code {rc}")

    return {
        'base': out.base,
        'combo': out.combo,
        'sp': out.sp,
        'accent': out.accent,
        'ghost': out.ghost,
        'sqout_reduction': out.sqout_reduction,
        'skipped_dynamic_reduction': out.skipped_dynamic_reduction,
    }


def _marshal(flat, depth_mode, depth_value, ms_filter, flag_skipped_dynamics,
             progress, enable_bound_prune=False):
    """Pack a hyflat.FlatGraph into the arrays hy_search reads.

    Returns (HySearchIn, keepalive). The keepalive list must outlive the call:
    ctypes does not retain the arrays a struct points into.

    """
    n_nodes = len(flat.nodes)
    nodes = (HyNode * n_nodes)()
    for i, fn in enumerate(flat.nodes):
        nd = nodes[i]
        nd.tick = fn.tick
        nd.adv_edge = fn.adv_edge
        nd.branch_edge = fn.branch_edge
        nd.is_sp = 1 if fn.is_sp else 0
        nd.base_suffix = fn.base_suffix
        nd.max_suffix = fn.max_suffix
        nd.total_spscore_suffix = fn.total_spscore_suffix
        nd.remaining_sp_phrases = fn.remaining_sp_phrases
        nd.max_spscore_density = fn.max_spscore_density

    # The variable-length parts of every edge go into shared arrays, with each
    # edge carrying a half-open slice of them.
    sp_rows = []
    ext_key = []
    ext_val = []
    be_rows = []
    aiet = []
    spans = []
    for fe in flat.edges:
        sp_begin = len(sp_rows)
        for tick, ms, extension_map in fe.sp_times:
            map_begin = len(ext_key)
            for k, v in extension_map.items():
                ext_key.append(k)
                ext_val.append(v)
            sp_rows.append((tick, ms, map_begin, len(ext_key)))

        aiet_begin = len(aiet)
        aiet.extend(fe.activation_initial_end_times)

        be_begin = len(be_rows)
        be_rows.extend(fe.backends)

        spans.append((sp_begin, len(sp_rows), aiet_begin, len(aiet),
                      be_begin, len(be_rows)))

    n_edges = len(flat.edges)
    edges = (HyEdge * n_edges)()
    for i, fe in enumerate(flat.edges):
        e = edges[i]
        e.dest = fe.dest
        e.notecount = fe.notecount
        e.basescore = fe.basescore
        e.comboscore = fe.comboscore
        e.spscore = fe.spscore
        e.soloscore = fe.soloscore
        e.accentscore = fe.accentscore
        e.ghostscore = fe.ghostscore
        e.frontend_points = fe.frontend_points
        e.frontend_is_accent = 1 if fe.frontend_is_accent else 0
        e.frontend_is_ghost = 1 if fe.frontend_is_ghost else 0
        e.skipped_dynamic_points = fe.skipped_dynamic_points
        e.late_sqin_count = fe.late_sqin_count
        (e.sp_begin, e.sp_end, e.aiet_begin, e.aiet_end,
         e.be_begin, e.be_end) = spans[i]
        # Only activation edges carry a deadline; the rest never have it read.
        e.activation_fill_deadline_ms = (
            0.0 if fe.activation_fill_deadline_ms is None
            else fe.activation_fill_deadline_ms)
        e.sqinout_timing = fe.sqinout_timing
        e.sqinout_time = fe.sqinout_time
        e.sqout_time = fe.sqout_time
        e.sqin_time = fe.sqin_time

    sptimes = (HySpTime * len(sp_rows))()
    for i, (tick, ms, mb, me) in enumerate(sp_rows):
        st = sptimes[i]
        st.tick = tick
        st.ms = ms
        st.map_begin = mb
        st.map_end = me

    backends = (HyBackend * len(be_rows))()
    for i, (tick, offset_ms, points, sqout_points) in enumerate(be_rows):
        b = backends[i]
        b.tick = tick
        b.offset_ms = offset_ms
        b.points = points
        b.sqout_points = sqout_points

    ext_key_a = (ctypes.c_int64 * len(ext_key))(*ext_key)
    ext_val_a = (ctypes.c_int64 * len(ext_val))(*ext_val)
    aiet_a = (ctypes.c_int64 * len(aiet))(*aiet)

    sin = HySearchIn()
    sin.nodes = nodes
    sin.edges = edges if n_edges else None
    sin.sptimes = sptimes if sp_rows else None
    sin.ext_key = ext_key_a if ext_key else None
    sin.ext_val = ext_val_a if ext_val else None
    sin.backends = backends if be_rows else None
    sin.aiet = aiet_a if aiet else None
    sin.n_nodes = n_nodes
    sin.n_edges = n_edges
    sin.n_sptimes = len(sp_rows)
    sin.n_ext = len(ext_key)
    sin.n_backends = len(be_rows)
    sin.n_aiet = len(aiet)
    sin.start_node = flat.start
    sin.has_sp_cap = 0 if flat.sp_meter_cap is None else 1
    sin.sp_cap = 0 if flat.sp_meter_cap is None else flat.sp_meter_cap
    sin.depth_mode = DEPTH_MODES.get(depth_mode, DEPTH_OTHER)
    sin.depth_value = depth_value
    sin.has_ms_filter = 0 if ms_filter is None else 1
    sin.ms_filter = 0.0 if ms_filter is None else float(ms_filter)
    sin.flag_skipped_dynamics = 1 if flag_skipped_dynamics else 0
    sin.graph_length = flat.length
    sin.enable_bound_prune = 1 if enable_bound_prune else 0
    sin._pad2 = 0
    sin.progress = progress if progress is not None else PROGRESS_FN()

    keepalive = [nodes, edges, sptimes, backends, ext_key_a, ext_val_a, aiet_a]
    return sin, keepalive


def _graph_multsqueezes(flat):
    """Every multiplier squeeze in the song, in the order a path meets them.

    The base and SP advance edges at a position carry the same MultSqueeze
    objects, so which track a path took cannot change what it collects. Walking
    the base track once therefore gives the list every path ends up with.

    """
    found = []
    i = flat.start
    while i >= 0:
        fn = flat.nodes[i]
        if fn.adv_edge < 0:
            break
        fe = flat.edges[fn.adv_edge]
        found.extend(fe.source.multsqueezes)
        i = fe.dest
    return found


def _rebuild(flat, out, multsqueezes):
    """Turn the decision log back into hydata.Path objects.

    The engine reports which node each activation was made at and which edge
    ended it; everything the UI reads off an activation -- its timecode, its
    chord, its backends -- is then taken from the graph those indices address,
    which is the same object the pure-Python search would have handed over.

    """
    top_level = []
    # by_depth[d] is the most recent path emitted at that depth; a variant
    # hangs off the one directly above it. Emission is preorder, so the parent
    # is always already there.
    by_depth = []

    for i in range(out.n_paths):
        op = out.paths[i]

        path = hydata.Path()
        path.score_base = op.score_base
        path.score_combo = op.score_combo
        path.score_sp = op.score_sp
        path.score_solo = op.score_solo
        path.score_accents = op.score_accents
        path.score_ghosts = op.score_ghosts
        path.notecount = op.notecount
        path.leftover_sp = op.leftover_sp
        path.skipped_accents = op.skipped_accents
        path.skipped_ghosts = op.skipped_ghosts
        path.multsqueezes = multsqueezes

        activations = []
        for j in range(op.act_begin, op.act_end):
            oa = out.acts[j]
            node = flat.nodes[oa.act_node].source

            act = hydata.Activation()
            act.skips = oa.skips
            act.timecode = node.timecode
            act.chord = node.chord
            act.sp_meter = oa.sp_meter
            act.frontend_points = node.branch_edge.frontend.points
            act.e_offset = oa.e_offset
            if oa.deact_edge >= 0:
                # The same list object the pure-Python search aliases onto the
                # activation, not a copy of it.
                act.backends = flat.edges[oa.deact_edge].source.backends
            act.sqinouts = [
                hydata.SqIn(out.sqs[k].offset) if out.sqs[k].kind == SQ_IN
                else hydata.SqOut(out.sqs[k].offset)
                for k in range(oa.sq_begin, oa.sq_end)
            ]
            activations.append(act)

        path._activations = activations
        # close_last_activation folds each activation into this running maximum
        # as the next one is appended, so a path that arrives from the engine
        # rather than from the search has to be given it. difficulty() re-walks
        # and so does not need it, but search_difficulty() and the ms filter
        # read it directly and would otherwise under-report.
        for act in activations[:-1]:
            d = act.difficulty()
            if d is not None and (path._difficulty_prefix is None
                                  or d > path._difficulty_prefix):
                path._difficulty_prefix = d

        depth = op.depth
        if depth == 0:
            top_level.append(path)
        else:
            path.var_point = op.var_point
            by_depth[depth - 1].variants.append(path)

        if len(by_depth) == depth:
            by_depth.append(path)
        else:
            by_depth[depth] = path

    for path in top_level:
        # The tree arrived finished rather than one merge at a time, which is
        # what recount_tied_paths is for.
        path.recount_tied_paths()
        # No detach_variants: nothing here is shared between two finished
        # paths, because each variant was rebuilt as its own object.
        path.prepare_variants()

    return top_level


def search(flat, depth_mode, depth_value, ms_filter,
           flag_skipped_dynamics=False, cb_pathsprogress=None,
           enable_bound_prune=False):
    """Run the whole BFS path search natively over a flattened ScoreGraph.

    Returns the finished hydata.Path objects, best score first, in the state
    GraphPather.read would leave them in.

    enable_bound_prune mirrors hypath.ENABLE_BOUND_PRUNE: when set the engine
    runs the same tight bound pruning the Python search does. Passed rather than
    read here so hynative does not import hypath (which imports hynative).

    """
    return _run_native(
        _LIB.hy_search, "hy_search", flat, depth_mode, depth_value, ms_filter,
        flag_skipped_dynamics, cb_pathsprogress, enable_bound_prune)


def dp_search(flat, depth_mode, depth_value, ms_filter,
              flag_skipped_dynamics=False, cb_pathsprogress=None,
              enable_bound_prune=False):
    """Run the activation DP natively over a flattened ScoreGraph.

    A drop-in alternative to search() that finds the same best score by the
    dynamic program in hypath.DPPather rather than the BFS. Same inputs, same
    hydata.Path results, same output marshalling -- only the C entry point
    differs. enable_bound_prune is accepted for signature parity and ignored
    (the DP has no bound-pruning pass).

    """
    return _run_native(
        _LIB.hy_dp_search, "hy_dp_search", flat, depth_mode, depth_value,
        ms_filter, flag_skipped_dynamics, cb_pathsprogress, enable_bound_prune)


def _run_native(fn, fn_name, flat, depth_mode, depth_value, ms_filter,
                flag_skipped_dynamics, cb_pathsprogress, enable_bound_prune):
    """Shared body for search() and dp_search(): marshal, cross the boundary
    once via fn, and rebuild the hydata.Path objects."""
    # A progress callback is allowed to raise in order to abandon the search:
    # that is how hyutil's adaptive SP meter ladder drops a rung that has
    # overrun its time budget. An exception cannot travel back through a C
    # callback -- ctypes prints it and returns anyway -- so it is caught here,
    # turned into a cancel, and re-raised once the engine has unwound.
    raised = []

    progress = None
    if cb_pathsprogress:
        def _on_progress(node_index, fraction):
            try:
                tc = (flat.nodes[node_index].source.timecode
                      if node_index >= 0 else None)
                cb_pathsprogress(tc, fraction)
            except BaseException as e:      # noqa: BLE001 - re-raised below
                raised.append(e)
                return 1
            return 0
        progress = PROGRESS_FN(_on_progress)

    sin, keepalive = _marshal(
        flat, depth_mode, depth_value, ms_filter, flag_skipped_dynamics,
        progress, enable_bound_prune)

    out = HySearchOut()
    rc = fn(ctypes.byref(sin), ctypes.byref(out))
    if raised:
        # Nothing was allocated on a cancel, so there is nothing to free.
        raise raised[0]
    if rc != HY_OK:
        raise RuntimeError(f"{fn_name} failed with code {rc}")

    try:
        return _rebuild(flat, out, _graph_multsqueezes(flat))
    finally:
        _LIB.hy_search_free(ctypes.byref(out))
        del keepalive
