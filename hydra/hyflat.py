"""Flatten a ScoreGraph into plain arrays for the native path search.

The search is the part of analysis worth moving to C++, but it cannot be
moved one function at a time: GraphPath touches ScoreGraphNode,
ScoreGraphEdge, Timecode and hydata.Path on nearly every step, and paying a
ctypes crossing per step costs more than the C++ saves (see hynative). The
whole search has to run on the other side of one crossing, which means the
graph has to get there as data.

This module is that translation. It walks a ScoreGraph once and produces
index-addressed arrays -- nodes referring to edges by position, edges
referring to nodes by position -- with no Python objects left in them.

The one fact that makes this cheap: a Timecode compares and hashes purely on
``ticks`` (hymisc.Timecode.__eq__/__lt__/__hash__), so every timecode in the
search, including the keys of the SP extension maps, is just an int64. Only
two things need real ms floats -- the activation fill deadline and backend
offsets -- and both are carried explicitly.

Nothing here changes the search. It is a description of the graph, and
``verify_roundtrip`` checks that description against the live objects.

"""

# sp_end_time is None on a path that is not in SP. None has no int64 spelling,
# so it travels as a sentinel below every real tick value.
NO_TIME = -1


class FlatEdge:
    """One ScoreGraphEdge with every Python reference replaced by an index."""

    __slots__ = (
        'dest', 'notecount', 'basescore', 'comboscore', 'spscore', 'soloscore',
        'accentscore', 'ghostscore', 'multsqueeze_count', 'sp_times',
        'frontend_points', 'frontend_is_accent', 'frontend_is_ghost',
        'skipped_dynamic_points', 'activation_fill_deadline_ms',
        'activation_initial_end_times', 'backends', 'sqinout_time',
        'sqinout_timing', 'sqout_time', 'sqin_time', 'late_sqin_count',
        'source',
    )

    def __init__(self):
        self.dest = -1
        self.notecount = 0
        self.basescore = 0
        self.comboscore = 0
        self.spscore = 0
        self.soloscore = 0
        self.accentscore = 0
        self.ghostscore = 0
        self.multsqueeze_count = 0
        # [(sp_tick, sp_ms, {from_tick: to_tick})] -- the extension map is what
        # advance() applies to sp_end_time while SP is active. The ms is
        # carried because a path that banks its second bar here keeps this
        # timecode as sp_ready_time, and the activation E offset is measured
        # from it in real milliseconds.
        self.sp_times = []
        self.frontend_points = 0
        # Dynamics of the activation note, which is what decides whether
        # skipping this fill costs accent or ghost points. Only read when
        # hymisc.FLAG_SKIPPED_DYNAMICS is on; carried unconditionally so the
        # flat graph does not depend on a process-wide flag.
        self.frontend_is_accent = False
        self.frontend_is_ghost = False
        self.skipped_dynamic_points = 0
        self.activation_fill_deadline_ms = None
        # Indexed by the path's SP meter at activation time.
        self.activation_initial_end_times = []
        # [(tick, offset_ms, points, sqout_points)]
        self.backends = []
        self.sqinout_time = NO_TIME
        # ms offset of the SqIn/SqOut note from the deactivation. This is the
        # value both markers are built from, so it has to travel as a float.
        self.sqinout_timing = 0.0
        self.sqout_time = NO_TIME
        self.sqin_time = NO_TIME
        self.late_sqin_count = 0
        # Kept for reconstruction and for verify_roundtrip; not sent to C++.
        self.source = None


class FlatNode:
    __slots__ = ('tick', 'is_sp', 'adv_edge', 'branch_edge', 'source')

    def __init__(self):
        self.tick = NO_TIME
        self.is_sp = False
        self.adv_edge = -1
        self.branch_edge = -1
        self.source = None


class FlatGraph:
    """A ScoreGraph as arrays. ``start`` indexes into ``nodes``."""

    __slots__ = ('nodes', 'edges', 'start', 'sp_meter_cap', 'length')

    def __init__(self):
        self.nodes = []
        self.edges = []
        self.start = -1
        self.sp_meter_cap = None
        self.length = 0

    def stats(self):
        return {
            'nodes': len(self.nodes),
            'edges': len(self.edges),
            'sp_times': sum(len(e.sp_times) for e in self.edges),
            'extension_entries': sum(
                len(m) for e in self.edges for _, _, m in e.sp_times),
            'backends': sum(len(e.backends) for e in self.edges),
        }


def _tick(timecode):
    return NO_TIME if timecode is None else timecode.ticks


def flatten(graph):
    """Walk a ScoreGraph and return the equivalent FlatGraph.

    Nodes are discovered by following adv_edge and branch_edge from the
    start node, which is how the search reaches them too, so anything
    unreachable is correctly absent.

    """
    flat = FlatGraph()
    flat.sp_meter_cap = graph.sp_meter_cap
    flat.length = graph.length

    node_ids = {}
    edge_ids = {}

    def node_id(node):
        if node is None:
            return -1
        key = id(node)
        existing = node_ids.get(key)
        if existing is not None:
            return existing

        idx = len(flat.nodes)
        node_ids[key] = idx
        fn = FlatNode()
        fn.source = node
        flat.nodes.append(fn)
        # Filled after the recursion below, so a cycle cannot loop forever.
        fn.tick = _tick(node.timecode)
        fn.is_sp = bool(node.is_sp)
        pending_nodes.append((fn, node))
        return idx

    def edge_id(edge):
        if edge is None:
            return -1
        key = id(edge)
        existing = edge_ids.get(key)
        if existing is not None:
            return existing

        idx = len(flat.edges)
        edge_ids[key] = idx
        fe = FlatEdge()
        fe.source = edge
        flat.edges.append(fe)
        pending_edges.append((fe, edge))
        return idx

    pending_nodes = []
    pending_edges = []

    flat.start = node_id(graph.start)

    # Breadth-first over whichever queue still has work; edges enqueue nodes
    # and nodes enqueue edges, so both drain together.
    while pending_nodes or pending_edges:
        while pending_nodes:
            fn, node = pending_nodes.pop()
            fn.adv_edge = edge_id(node.adv_edge)
            fn.branch_edge = edge_id(node.branch_edge)

        while pending_edges:
            fe, edge = pending_edges.pop()
            fe.dest = node_id(edge.dest)
            fe.notecount = edge.notecount
            fe.basescore = edge.basescore
            fe.comboscore = edge.comboscore
            fe.spscore = edge.spscore
            fe.soloscore = edge.soloscore
            fe.accentscore = edge.accentscore
            fe.ghostscore = edge.ghostscore
            fe.multsqueeze_count = len(edge.multsqueezes)

            fe.sp_times = [
                (_tick(sptc), sptc.ms,
                 {_tick(k): _tick(v) for k, v in extension_map.items()})
                for sptc, extension_map in edge.sp_times
            ]

            if edge.frontend is not None:
                fe.frontend_points = edge.frontend.points
                act_note = edge.frontend.chord.activation_note()
                if act_note is not None:
                    fe.frontend_is_accent = act_note.is_accent()
                    fe.frontend_is_ghost = act_note.is_ghost()
            # skipped_dynamic_points and activation_initial_end_times are
            # declared in ScoreGraphEdge.__slots__ but not set in __init__, so
            # they are genuinely absent on edges that never received one --
            # reading them directly raises AttributeError.
            fe.skipped_dynamic_points = getattr(
                edge, 'skipped_dynamic_points', 0) or 0
            fe.activation_fill_deadline_ms = edge.activation_fill_deadline_ms

            # A dict keyed by SP meter in the original; the meter is a small
            # non-negative int, so it flattens to a list.
            aiet = getattr(edge, 'activation_initial_end_times', None)
            if aiet:
                if isinstance(aiet, dict):
                    top = max(aiet)
                    fe.activation_initial_end_times = [
                        _tick(aiet.get(i)) for i in range(top + 1)]
                else:
                    fe.activation_initial_end_times = [_tick(t) for t in aiet]

            fe.backends = [
                (_tick(be.timecode), be.offset_ms, be.points, be.sqout_points)
                for be in edge.backends
            ]

            fe.sqinout_time = _tick(edge.sqinout_time)
            fe.sqinout_timing = (
                0.0 if edge.sqinout_timing is None else edge.sqinout_timing)
            fe.sqout_time = _tick(edge.sqout_time)
            fe.sqin_time = _tick(edge.sqin_time)
            fe.late_sqin_count = edge.late_sqin_count

    return flat


def verify_roundtrip(graph, flat):
    """Check the flat description against the live graph.

    Returns a list of human-readable mismatches; empty means the flattening
    lost nothing the search reads. This exists because a silently wrong
    graph would produce confidently wrong paths, which is the one failure
    mode this project cannot tolerate.

    """
    problems = []

    def check(cond, msg):
        if not cond:
            problems.append(msg)

    check(flat.sp_meter_cap == graph.sp_meter_cap, "sp_meter_cap differs")
    check(flat.length == graph.length, "length differs")
    check(flat.start >= 0, "no start node")

    for i, fn in enumerate(flat.nodes):
        node = fn.source
        check(fn.tick == _tick(node.timecode), f"node {i}: tick differs")
        check(fn.is_sp == bool(node.is_sp), f"node {i}: is_sp differs")

        if node.adv_edge is None:
            check(fn.adv_edge == -1, f"node {i}: adv_edge should be absent")
        else:
            check(0 <= fn.adv_edge < len(flat.edges),
                  f"node {i}: adv_edge index out of range")
            check(flat.edges[fn.adv_edge].source is node.adv_edge,
                  f"node {i}: adv_edge points at the wrong edge")

        if node.branch_edge is None:
            check(fn.branch_edge == -1, f"node {i}: branch_edge should be absent")
        else:
            check(0 <= fn.branch_edge < len(flat.edges),
                  f"node {i}: branch_edge index out of range")
            check(flat.edges[fn.branch_edge].source is node.branch_edge,
                  f"node {i}: branch_edge points at the wrong edge")

    for i, fe in enumerate(flat.edges):
        edge = fe.source
        if edge.dest is None:
            check(fe.dest == -1, f"edge {i}: dest should be absent")
        else:
            check(0 <= fe.dest < len(flat.nodes),
                  f"edge {i}: dest index out of range")
            check(flat.nodes[fe.dest].source is edge.dest,
                  f"edge {i}: dest points at the wrong node")

        for name in ('notecount', 'basescore', 'comboscore', 'spscore',
                     'soloscore', 'accentscore', 'ghostscore'):
            check(getattr(fe, name) == getattr(edge, name),
                  f"edge {i}: {name} differs")

        check(fe.multsqueeze_count == len(edge.multsqueezes),
              f"edge {i}: multsqueeze count differs")

        check(len(fe.sp_times) == len(edge.sp_times),
              f"edge {i}: sp_times length differs")
        for j, (sptc, extension_map) in enumerate(edge.sp_times):
            ftick, fms, fmap = fe.sp_times[j]
            check(ftick == _tick(sptc), f"edge {i}: sp_time {j} tick differs")
            check(fms == sptc.ms, f"edge {i}: sp_time {j} ms differs")
            check(len(fmap) == len(extension_map),
                  f"edge {i}: sp_time {j} extension map size differs")
            for k, v in extension_map.items():
                check(fmap.get(_tick(k)) == _tick(v),
                      f"edge {i}: sp_time {j} extension map entry differs")

        check(len(fe.backends) == len(edge.backends),
              f"edge {i}: backend count differs")
        for j, be in enumerate(edge.backends):
            ftick, foff, fpts, fsq = fe.backends[j]
            check(ftick == _tick(be.timecode), f"edge {i}: backend {j} tick differs")
            check(foff == be.offset_ms, f"edge {i}: backend {j} offset differs")
            check(fpts == be.points, f"edge {i}: backend {j} points differ")
            check(fsq == be.sqout_points,
                  f"edge {i}: backend {j} sqout points differ")

        if edge.frontend is None:
            check(fe.frontend_points == 0,
                  f"edge {i}: frontend points on an edge with no frontend")
            check(not fe.frontend_is_accent and not fe.frontend_is_ghost,
                  f"edge {i}: frontend dynamics on an edge with no frontend")
        else:
            check(fe.frontend_points == edge.frontend.points,
                  f"edge {i}: frontend points differ")
            act_note = edge.frontend.chord.activation_note()
            want_accent = act_note is not None and act_note.is_accent()
            want_ghost = act_note is not None and act_note.is_ghost()
            check(fe.frontend_is_accent == want_accent,
                  f"edge {i}: frontend accent differs")
            check(fe.frontend_is_ghost == want_ghost,
                  f"edge {i}: frontend ghost differs")

        check(fe.skipped_dynamic_points
              == (getattr(edge, 'skipped_dynamic_points', 0) or 0),
              f"edge {i}: skipped_dynamic_points differ")
        check(fe.activation_fill_deadline_ms == edge.activation_fill_deadline_ms,
              f"edge {i}: activation_fill_deadline_ms differs")

        check(fe.sqinout_time == _tick(edge.sqinout_time),
              f"edge {i}: sqinout_time differs")
        check(fe.sqinout_timing
              == (0.0 if edge.sqinout_timing is None else edge.sqinout_timing),
              f"edge {i}: sqinout_timing differs")
        check(fe.sqout_time == _tick(edge.sqout_time),
              f"edge {i}: sqout_time differs")
        check(fe.sqin_time == _tick(edge.sqin_time),
              f"edge {i}: sqin_time differs")
        check(fe.late_sqin_count == edge.late_sqin_count,
              f"edge {i}: late_sqin_count differs")

        # The activation table is read by SP meter value, so every meter the
        # original answers for must survive.
        aiet = getattr(edge, 'activation_initial_end_times', None)
        if aiet and isinstance(aiet, dict):
            for meter, tc in aiet.items():
                check(meter < len(fe.activation_initial_end_times)
                      and fe.activation_initial_end_times[meter] == _tick(tc),
                      f"edge {i}: activation end time for meter {meter} differs")

    return problems
