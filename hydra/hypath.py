import bisect
import heapq
import shutil
import copy
import math
import json
import sys
from enum import Enum

from . import hydata
from . import hyflat
from . import hymisc
from . import hynative

# Bound once: category_scores calls this per note, and the attribute lookup
# through the module showed up next to the work itself.
to_multiplier = hymisc.to_multiplier


# How far apart (ms) a note and a deactivation can be and still be found as
# a SqIn/SqOut. Raised from the stock 140ms to widen that search.
#
# Note that edge.backends must stay complete out to this same window: it is
# not just a display list, it is what create_deactivated_path() iterates to
# reduce the SqOut note from full SP points down to sqout_points. Dropping a
# backend here silently leaves that note scored as if it were still in SP.
# Trim for display instead (see hymisc.BACKEND_DISPLAY_WINDOW_MS).
SQUEEZE_WINDOW_MS = 500

# How many paths to keep at any one tied score. A long chart can tie the same
# score hundreds of ways, and every one of them is carried, prepared and
# serialized with the record for no extra information: the score is the same,
# and nobody reads the four hundredth way to reach it. Once a score has this
# many paths, further ties are dropped where they are found rather than
# collected.
MAX_TIED_PATHS = 4

# "Argument not given", so that an explicit sp_meter_cap of None (no ceiling)
# is distinguishable from leaving it to the running edition.
_UNSET = object()

# Results of ScoreGraphEdge.deactivation_type. Small ints rather than strings:
# see that method.
DEACT_NONE = 0
DEACT_NORMAL = 1
DEACT_SQINOUT = 2

# Whether to drop paths that provably cannot reach the results.
#
# Off, because it was measured and it still does not pay -- but the machinery
# is complete, exact, and available (flip this to True to use it).
#
# Two bounds have been tried. The loose one (score + max_suffix) removed 0.13%
# of paths, because max_suffix assumes the whole rest of the song is played in
# SP, which every live path is within a whisker of. The tight bound in
# _prune_hopeless_paths replaces that spscore assumption with what a path can
# physically bank in the 2*(b+r) measures of SP it has left, charged at the
# densest spscore rate remaining (compute_bounds' three suffix numbers). It is
# still exact -- every other term of max_suffix is preserved -- so results are
# identical to a full search, verified against a prune-off reference on the
# whole corpus, capped and uncapped, on both engines.
#
# On the blink-182 discography (uncapped, settling at cap 128) it removed just
# 2,229 of 61.9M paths examined -- 0.0036%, fewer than the loose bound -- and
# cost ~7% (native ~8.5s -> ~9.1s). A discography keeps r (SP phrases left)
# large until the very end, so 2*(b+r) rarely falls below total_spscore_suffix
# and the min() degenerates back to max_suffix. The bound bites only late in a
# short chart, which is where it is cheapest to just finish the search. Kept in
# place because it is exact and because the suffix index it builds is the
# groundwork the activation DP (Phases 3-4) reuses.
ENABLE_BOUND_PRUNE = False


class ScoreGraph:
    """Description of a song in terms of pathing choices and outcomes.
    
    Consists of nodes representing timecodes for the song and edges
    representing either getting further in the song (advancing) or
    switching between inactive and active SP (branching).
    
    To that end, the nodes form two tracks: the normal track and SP
    track. There are only branch edges between the tracks where it's
    possible to activate (with a fill) or deactivate (run out of) SP.
    
    Edges store information about what is gained (mainly points and SP)
    from taking that edge.
    
    Advancing down a track gets further in the song and accrues points.
    Branching does not advance time, but possibly accrues features like
    backends which are specific to the act of toggling SP.
    
    Because "your current SP" isn't encoded on the graph, not every path
    through it is a valid path for the song. However, the graph contains
    all the info needed to go from start to finish while tracking sp, in
    order to explore valid paths through the song.
    
    """
    def __init__(self, song, sp_meter_cap=_UNSET):
        # Finished state
        self.song = song
        # Bars the meter holds, or None for no ceiling. Carried on the graph
        # rather than read from hymisc at each use, so that an adaptive run can
        # build graphs at several caps without editing process-wide state that
        # another analysis on another thread is also reading.
        self.sp_meter_cap = (
            hymisc.SP_METER_CAP if sp_meter_cap is _UNSET else sp_meter_cap
        )
        self.start = ScoreGraphNode(song.start_time(), False)
        self.length = 0

        # Processing state
        self._head_time = None
        self._base_track_head = self.start
        self._sp_track_head = ScoreGraphNode(song.start_time(), True)
        # Kept so the finished graph can be walked from the top of both
        # tracks; _sp_track_head is the far end by the time building is done.
        self.sp_start = self._sp_track_head
        self._combo = 0
        self._sp_phrase_count = 0
        self._pending_deacts = set([])
        # The same deactivations, as a min-heap, so the next one due can be
        # read without ordering the whole set. Entries are removed lazily:
        # anything popped that is no longer in the set above has already been
        # handled or extended past, and is skipped.
        self._deact_heap = []
        self._recent_deact_edges = []       # Used for SqIn backend detection (notes after deacts are usually Out, but recent deacts can make it a SqIn)
        self._recent_backends = []          # Used for SqOut detection (notes before deacts are usually In, but recent notes can be SqOut)
        self._proto_base_edge = ScoreGraphEdge()
        self._proto_sp_edge = ScoreGraphEdge()
        
        for timestamp in song._sequence:
            # SP can fall off between timestamps, so handle those first if any.
            # Only the earliest pending deactivations can be due, so the heap
            # is read from the front rather than the set being sorted here:
            # sorting ran once per timestamp, which on a discography is 172,000
            # sorts of a set that grows all song.
            heap = self._deact_heap
            while heap and heap[0] < timestamp.timecode:
                pending_deact = heapq.heappop(heap)
                if pending_deact not in self._pending_deacts:
                    continue        # stale entry, already dealt with
                self.set_head_time(pending_deact)
                self.handle_deact(pending_deact, None)


            self.set_head_time(timestamp.timecode)
            
            self.store_notecount(timestamp.chord.count())
            if timestamp.flag_solo:
                self.store_soloscore(100 * timestamp.chord.count())
            
            score_groups = category_scores(timestamp.chord, self._combo)
            
            try:
                msq = hydata.MultSqueeze(timestamp.chord, self._combo)
                self.store_multsqueeze(msq)
            except ValueError:
                pass
            
            self.store_basescore(score_groups['base'])
            self.store_comboscore(score_groups['combo'])
            self.store_spscore(score_groups['sp'])
            self.store_accentscore(score_groups['accent'])
            self.store_ghostscore(score_groups['ghost'])
            
            self._combo += timestamp.chord.count()
            
            self.store_new_backend(timestamp, score_groups['sp'], score_groups['sp'] - score_groups['sqout_reduction'])
            
            if timestamp.flag_sp:
                self._sp_phrase_count += 1

                # If any deacts are only the squeeze window away, keep a non-extended copy of them (SqOut)
                sqout_deacts = set([tc for tc in self._pending_deacts if tc.ms - timestamp.timecode.ms < SQUEEZE_WINDOW_MS])

                # Deact timecodes that can be extended by this sp: current pending deacts as well as very recently handled deacts (SqIn)
                extendable_tcs = self._pending_deacts.union(set([e.dest.timecode for e in self._recent_deact_edges]))

                # Deact timecodes after extension
                extension_map = self.extend_deacts(extendable_tcs, timestamp.timecode, song)

                # Update deacts. Every pending time moved, so the whole heap is
                # stale; rebuilding it is O(n) and happens once per SP phrase
                # rather than once per timestamp.
                self._pending_deacts = set(extension_map.values()).union(sqout_deacts)
                self._deact_heap = list(self._pending_deacts)
                heapq.heapify(self._deact_heap)


                # Save info on the graph
                self._proto_base_edge.sp_times.append((timestamp.timecode, extension_map))
                self._proto_sp_edge.sp_times.append((timestamp.timecode, extension_map))
                
            # handle acts
            if timestamp.has_activation():
                self.advance_tracks(timestamp.timecode, timestamp.chord)
                act_edge = self.add_act_edge(timestamp.chord, score_groups['sp'], score_groups['skipped_dynamic_reduction'], timestamp.activation_length, song)

                # A path activating here ends up at exactly one of that edge's
                # end times, so those are the deacts this activation puts in
                # play. Capped, that's the familiar +4/+6/+8 measures.
                for end_time in act_edge.activation_initial_end_times.values():
                    if end_time not in self._pending_deacts:
                        self._pending_deacts.add(end_time)
                        heapq.heappush(self._deact_heap, end_time)

            # handle deacts
            if timestamp.timecode in self._pending_deacts:
                self.handle_deact(timestamp.timecode, timestamp.chord)

        self.advance_tracks(song.last.timecode, song.last.chord)

        self.compute_bounds()

    def compute_bounds(self):
        """Annotate every node with what the rest of the song is worth.

        Two numbers per position, both over the remaining song:

        base_suffix - exactly what a path on the base track scores if it never
            activates again. Activating is always optional and a path that
            never branches takes no score adjustments, so this is not merely a
            bound, it is achievable. That makes it a *lower* bound on what
            that path finishes on.

        max_suffix - more than any continuation can possibly score. It assumes
            the whole remainder is played in SP (SP track edges carry the same
            categories as base track edges plus spscore, so they dominate
            term by term), and then adds every activation frontend and every
            positive backend adjustment still to come, none of which any real
            path can collect all of. An over-estimate is what makes it safe.

        The pather uses the pair to drop paths that cannot catch up: if a
        path's best possible finish is below a score that some other path is
        already guaranteed, it cannot appear in the results and following it
        further is wasted work. Both tracks are walked in lockstep, so a
        position's two nodes get the same max_suffix -- a base track path may
        activate immediately and so is bounded by the SP track too.

        Three more numbers per position feed the tighter ceiling in
        _prune_hopeless_paths, which replaces max_suffix's "whole rest in SP"
        assumption for its spscore term with what a path can physically bank:

        remaining_sp_phrases - SP phrases still to come from here. Each is one
            entry in an advance edge's sp_times (one per flag_sp timestamp).

        total_spscore_suffix - exact sum of SP-track advance-edge spscore over
            the remainder. This is what max_suffix over-approximates for its
            spscore component, and the cap on how much any path can collect.

        max_spscore_density - the densest spscore-per-measure of any single
            SP-track advance edge in the remainder. A path in SP for m measures
            cannot bank more than m * this.

        """
        base_nodes = []
        node = self.start
        while node is not None:
            base_nodes.append(node)
            node = node.adv_edge.dest if node.adv_edge else None

        sp_nodes = []
        node = self.sp_start
        while node is not None:
            sp_nodes.append(node)
            node = node.adv_edge.dest if node.adv_edge else None

        base_running = 0
        max_running = 0

        # The three tighter-ceiling accumulators, over the same suffix.
        sp_running = 0              # total_spscore_suffix
        phrase_running = 0         # remaining_sp_phrases
        density_running = 0.0      # max_spscore_density

        # Backwards: the last node has nothing left to play and so is worth 0.
        for i in range(len(base_nodes) - 1, -1, -1):
            b_node = base_nodes[i]
            s_node = sp_nodes[i] if i < len(sp_nodes) else None

            b_node.base_suffix = base_running
            b_node.max_suffix = max_running
            b_node.total_spscore_suffix = sp_running
            b_node.remaining_sp_phrases = phrase_running
            b_node.max_spscore_density = density_running
            if s_node is not None:
                s_node.base_suffix = base_running
                s_node.max_suffix = max_running
                s_node.total_spscore_suffix = sp_running
                s_node.remaining_sp_phrases = phrase_running
                s_node.max_spscore_density = density_running

            # Walking to the previous position adds that position's edges.
            b_edge = base_nodes[i - 1].adv_edge if i else None
            s_edge = sp_nodes[i - 1].adv_edge if i and i - 1 < len(sp_nodes) else None

            if b_edge is not None:
                base_running += (
                    b_edge.basescore + b_edge.comboscore + b_edge.spscore
                    + b_edge.soloscore + b_edge.accentscore + b_edge.ghostscore
                )
            if s_edge is not None:
                max_running += (
                    s_edge.basescore + s_edge.comboscore + s_edge.spscore
                    + s_edge.soloscore + s_edge.accentscore + s_edge.ghostscore
                )

                # The spscore-bearing track is the SP track; base advance edges
                # carry none (see store_spscore). This edge runs from the
                # previous position to this one, so its span is measured
                # between their two SP-track nodes.
                sp_running += s_edge.spscore
                phrase_running += len(s_edge.sp_times)

                src = sp_nodes[i - 1]
                span = (
                    s_edge.dest.timecode.measures_decimal
                    - src.timecode.measures_decimal
                )
                if span > 0:
                    density = s_edge.spscore / span
                    if density > density_running:
                        density_running = density

            # Everything a branch at the previous position could add on top.
            if i:
                act_edge = base_nodes[i - 1].branch_edge
                if act_edge is not None and act_edge.frontend is not None:
                    max_running += act_edge.frontend.points

                deact_edge = (
                    sp_nodes[i - 1].branch_edge if i - 1 < len(sp_nodes) else None
                )
                if deact_edge is not None:
                    for be in deact_edge.backends:
                        gain = be.points if be.points > 0 else 0
                        if be.sqout_points > gain:
                            gain = be.sqout_points
                        max_running += gain


    def store_notecount(self, count):
        self._proto_base_edge.notecount += count
        self._proto_sp_edge.notecount += count
        
    def store_soloscore(self, points):
        self._proto_base_edge.soloscore += points
        self._proto_sp_edge.soloscore += points
        
    def store_basescore(self, points):
        self._proto_base_edge.basescore += points
        self._proto_sp_edge.basescore += points
        
    def store_comboscore(self, points):
        self._proto_base_edge.comboscore += points
        self._proto_sp_edge.comboscore += points
        
    def store_spscore(self, points):
        self._proto_sp_edge.spscore += points
        
    def store_accentscore(self, points):
        self._proto_base_edge.accentscore += points
        self._proto_sp_edge.accentscore += points
        
    def store_ghostscore(self, points):
        self._proto_base_edge.ghostscore += points
        self._proto_sp_edge.ghostscore += points
        
    def store_multsqueeze(self, msq):
        self._proto_base_edge.multsqueezes.append(msq)
        self._proto_sp_edge.multsqueezes.append(msq)
    
    def store_new_backend(self, timestamp, sp_points, sqout_points):
        """Create a backend and apply it to recent deact edges.
        These backends are late, i.e. they have positive offsets.
        """
        backend = hydata.BackendSqueeze(
            timestamp.timecode, timestamp.chord,
            sp_points, sqout_points,
            timestamp.flag_sp
        )
        
        self._recent_backends.append(backend)
        
        for recent_edge in self._recent_deact_edges:
            offset_ms = timestamp.timecode.ms - recent_edge.dest.timecode.ms
            recent_edge.backends.append(copy.copy(backend))
            recent_edge.backends[-1].offset_ms = offset_ms

            if recent_edge.backends[-1].is_sp and not recent_edge.sqinout_time:
                # SqIn timing is only relevant for the 1st sp backend encountered
                # If there are more than 1, it's probably a charting error, but ya know
                recent_edge.sqinout_time = timestamp.timecode
                recent_edge.sqinout_timing = offset_ms
                recent_edge.late_sqin_count += 1
                recent_edge.sqin_time = recent_edge.sqin_time.plusmeasure(2, self.song)
        
    def max_sp_bars(self):
        """Most SP bars a path could be holding at the point reached so far.

        Two ceilings apply and the smaller wins. The meter size is one. The
        other is every SP phrase written up to here: a path cannot be holding
        a bar it has not had the chance to collect, whatever the meter would
        allow. Uncapped there is no meter, so only the second one is left.

        Taking the smaller matters because this decides how many end times an
        activation gets (see add_act_edge), and each one becomes a pending
        deactivation and so a node in the graph. Using the meter size alone
        manufactured states no path could reach -- the whole of a 32-bar
        meter three phrases into a song -- and the cost of carrying them
        climbed with the ceiling, which is exactly where the uncapped ladder
        hurts most.

        """
        if self.sp_meter_cap is None:
            return self._sp_phrase_count
        return min(self.sp_meter_cap, self._sp_phrase_count)

    def extend_deacts(self, deact_tcs, sp_timecode, song):
        """Where each pending deactivation moves to when an SP phrase is
        collected during an activation: 2 measures later.

        A capped meter cannot be pushed past its own size, so the result is
        held to a full meter's worth of measures from the phrase. That clamp
        is the overfill Clone Hero discards, and dropping it is the whole of
        what the uncapped edition changes here: every phrase is then worth its
        full 2 measures no matter how much SP is already banked.

        """
        extended = {tc: tc.plusmeasure(2, song) for tc in deact_tcs}

        if self.sp_meter_cap is None:
            return extended

        ceiling = sp_timecode.plusmeasure(2 * self.sp_meter_cap, song)
        return {tc: min(ext, ceiling) for tc, ext in extended.items()}

    def head_time_offset(self, timecode):
        return self._head_time.ms - timecode.ms
        
    def is_recent_to_head(self, timecode):
        return self.head_time_offset(timecode) < SQUEEZE_WINDOW_MS
        
    def set_head_time(self, timecode):
        """ Update head time and any mechanics based on being 'recent'"""
        self._head_time = timecode
        self._recent_deact_edges = [edge for edge in self._recent_deact_edges if self.is_recent_to_head(edge.dest.timecode)]
        self._recent_backends = [be for be in self._recent_backends if self.is_recent_to_head(be.timecode)]

    def handle_deact(self, deact_tc, chord):
        if deact_tc not in self._pending_deacts:
            return
        self.advance_tracks(deact_tc, chord)
        self.add_deact_edge()
        self._pending_deacts.remove(deact_tc)
    
    def advance_tracks(self, timecode, chord):
        if self._base_track_head.timecode >= timecode: 
            return
        
        self.length += 1
        
        self._proto_base_edge.dest = ScoreGraphNode(timecode, False)
        self._proto_sp_edge.dest = ScoreGraphNode(timecode, True)
        self._proto_base_edge.dest.chord = chord
        self._proto_sp_edge.dest.chord = chord
            
        self._base_track_head.adv_edge = self._proto_base_edge
        self._sp_track_head.adv_edge = self._proto_sp_edge
        
        self._proto_base_edge = ScoreGraphEdge()
        self._proto_sp_edge = ScoreGraphEdge()
        
        self._base_track_head = self._base_track_head.adv_edge.dest
        self._sp_track_head = self._sp_track_head.adv_edge.dest
    
    def add_act_edge(self, frontend_chord, frontend_points, skipped_dynamic_reduction, fill_length_ticks, song):
        act_edge = ScoreGraphEdge()
        act_edge.dest = self._sp_track_head
        
        act_edge.notecount = 0
        act_edge.basescore = 0
        act_edge.comboscore = 0
        act_edge.spscore = 0
        act_edge.soloscore = 0
        act_edge.accentscore = 0
        act_edge.ghostscore = 0
        
        act_edge.frontend = hydata.FrontendSqueeze(frontend_chord, frontend_points)
        
        # E threshold is 4 beats before the fill marker.
        tc_E = hymisc.Timecode(act_edge.dest.timecode.ticks - fill_length_ticks - 4*song.tick_resolution, song.tick_resolution, song.tpm_changes, song.bpm_changes)
        act_edge.activation_fill_deadline_ms = tc_E.ms
        
        # Two measures of SP per bar spent, for every amount of SP a path could
        # arrive here holding: 2 through a full meter, or 2 through however
        # many phrases the song has offered so far when uncapped.
        act_edge.activation_initial_end_times = {
            sp: act_edge.dest.timecode.plusmeasure(2 * sp, song)
            for sp in range(2, self.max_sp_bars() + 1)
        }

        act_edge.skipped_dynamic_points = skipped_dynamic_reduction

        self._base_track_head.branch_edge = act_edge
        return act_edge
    
    def add_deact_edge(self):
        deact_edge = ScoreGraphEdge()
        deact_edge.dest = self._base_track_head
        
        deact_edge.notecount = 0
        deact_edge.basescore = 0
        deact_edge.comboscore = 0
        deact_edge.spscore = 0
        deact_edge.soloscore = 0
        deact_edge.accentscore = 0
        deact_edge.ghostscore = 0
        
        deact_edge.sqout_time = deact_edge.dest.timecode
        deact_edge.sqin_time = deact_edge.dest.timecode
        
        # notes just prior to this deactivation, which are normally in sp but could be squeezed out
        for recent_backend in self._recent_backends:
            deact_edge.backends.append(copy.copy(recent_backend))
            offset_ms = recent_backend.timecode.ms - deact_edge.dest.timecode.ms
            deact_edge.backends[-1].offset_ms = offset_ms

            if recent_backend.is_sp and not deact_edge.sqinout_time:
                deact_edge.sqinout_time = recent_backend.timecode
                deact_edge.sqinout_timing = offset_ms
                deact_edge.sqout_time = deact_edge.sqout_time.plusmeasure(2, self.song)
                deact_edge.sqin_time = deact_edge.sqin_time.plusmeasure(2, self.song)
                
        self._recent_deact_edges.append(deact_edge)
        self._sp_track_head.branch_edge = deact_edge


class ScoreGraphNode:
    """Represents a point in the song and whether SP is active or not.
    
    The only possible edges are 1 advancing edge leading farther into the song
    and 1 branch node that does not move forward but toggles SP.
    """
    __slots__ = (
        'timecode', 'adv_edge', 'branch_edge', 'is_sp', 'chord',
        'base_suffix', 'max_suffix',
        'remaining_sp_phrases', 'total_spscore_suffix', 'max_spscore_density',
    )

    def __init__(self, timecode, is_sp):
        self.timecode = timecode

        self.adv_edge = None
        self.branch_edge = None
        self.is_sp = is_sp
        self.chord = None

        # Score bounds for the rest of the song from here. See
        # ScoreGraph.compute_bounds.
        self.base_suffix = 0
        self.max_suffix = 0

        # Inputs to the tight SP-time ceiling in _prune_hopeless_paths, all
        # over the remainder of the song from here. See compute_bounds.
        self.remaining_sp_phrases = 0
        self.total_spscore_suffix = 0
        self.max_spscore_density = 0.0
    
    def __repr__(self):
        lines = [self.name()]
        if self.adv_edge:
            lines.append(self.adv_edge.__repr__())
        if self.branch_edge:
            lines.append(self.branch_edge.__repr__())
        
        return '\n'.join(lines)
    
    def name(self):
        return f"{self.timecode.measurestr()}{' SP' if self.is_sp else ''}"


class ScoreGraphEdge:
    """Represents a movement from one node in the graph to another.
    
    Edges store information so that when a path uses an edge, the path
    simply adds to itself whatever is stored on that edge.
    
    The three possible movements:
    - Advancing further into the song
    - Activating SP (branch)
    - Deactivating SP (branch)
    
    The graph is created such that every possible place where it's possible
    to activate or run out of SP has a branch edge there.

    """
    __slots__ = (
        'dest', 'notecount', 'basescore', 'comboscore', 'spscore', 'soloscore',
        'accentscore', 'ghostscore', 'sp_times', 'frontend', 'backends',
        'multsqueezes', 'activation_fill_deadline_ms',
        'activation_initial_end_times', 'skipped_dynamic_points',
        'sqinout_time', 'sqinout_timing', 'late_sqin_count', 'sqout_time',
        'sqin_time',
    )

    def __init__(self):
        self.dest = None
        
        self.notecount = 0
        
        self.basescore = 0
        self.comboscore = 0
        self.spscore = 0
        self.soloscore = 0
        self.accentscore = 0
        self.ghostscore = 0
        
        self.sp_times = []
        
        self.frontend = None
        self.backends = []
        
        self.multsqueezes = []
        
        # SP must become ready by this time in order for the fill to show.
        self.activation_fill_deadline_ms = None
        
        self.sqinout_time = None
        self.sqinout_timing = None
        
        self.late_sqin_count = 0
        self.sqout_time = None
        self.sqin_time = None
    
    def __repr__(self):
        return f" --> {self.dest.name()}, frontend = {self.frontend}"
        
    def deactivation_type(self, sp_end_time):
        """Which kind of deactivation is possible if this deact edge is reached
        by a GraphPath object with the given sp end time.

        This result is a function of the sp end time, the edge's time,
        the edge's early SP backends (already applied to the incoming sp end
        time), and the edge's late SP backends.

        DEACT_NONE: It's not possible to deactivate here.
        DEACT_NORMAL: SP runs out here and the path is forced to deactivate.
        DEACT_SQINOUT:

        Returns one of the DEACT_* constants rather than a string: this is
        asked ~13M times per uncapped discography, and the caller's match over
        string literals cost more than the comparison that produced them.
        Timecodes are compared on .ticks directly for the same reason -- both
        sides are always Timecodes here, so Timecode.__eq__'s isinstance check
        is pure overhead at this call site.

        """
        if self.sqinout_time:
            # This deact has SP on it, so if valid, the deact path will be a
            # SqOut and the continuing path will be a SqIn.
            if sp_end_time.ticks == self.sqout_time.ticks:
                # end time + early SP backends == edge time + early SP backends
                return DEACT_SQINOUT
            else:
                return DEACT_NONE
        else:
            # Normal backend, no sqin/sqouts. SP cannot already have run out
            # before this edge; the assert that said so ran in every shipped
            # build (asserts are only stripped under -O) for 12M Timecode
            # comparisons a chart, and the condition is structural.
            if sp_end_time.ticks == self.dest.timecode.ticks:
                # SP ends here
                return DEACT_NORMAL
            else:
                # SP ends later
                return DEACT_NONE


class GraphPather:
    """Responsible for creating multiple paths and for creating records.
    
    Reads ScoreGraphs; stores a record for the latest graph that was read.
    
    """
    def __init__(self):
        self.record = hydata.HydraRecord()
        
    def read(
        self, graph, depth_mode, depth_value, ms_filter, cb_pathsprogress=None,
        incumbent=None
    ):
        """Find the paths through graph.

        incumbent is scores already known to be reachable for this song,
        descending, from an earlier run at a lower SP meter ceiling. Every path
        legal under a smaller meter is still legal under a larger one, so those
        scores are guaranteed to be matched here, and any path that cannot
        reach them is not worth following. See _reduce_iteration_paths.

        """
        self.record.ms_limit = ms_filter

        if hynative.SEARCH_ENABLED:
            self._read_native(
                graph, depth_mode, depth_value, ms_filter, cb_pathsprogress)
            return

        paths = [GraphPath()]
        paths[0].currentnode = graph.start
        length = 0

        self._incumbent = incumbent or ()
        sp_cap = graph.sp_meter_cap

        # to do: paths should complete at the same time, might improve performance
        while any(p.currentnode is not None for p in paths):
            iteration_paths = self._iteration_paths = []
            add_path = iteration_paths.append
            for p in paths:
                # Spelled out rather than calling is_complete(): this runs
                # once per path per iteration, ~1.9M times per chart, and the
                # method call was costing more than the check.
                assert p.currentnode is not None
                p.advance(sp_cap)

                node = p.currentnode
                if node is None:
                    add_path(p)
                    continue

                if node.is_sp:
                    can_extend, branchpath = p.branch_deactivate()
                    if can_extend:
                        add_path(p)
                else:
                    branchpath = p.branch_activate()
                    add_path(p)

                if branchpath:
                    add_path(branchpath)

            # Update the path list with branching results
            self._reduce_iteration_paths(depth_mode, depth_value, ms_filter)
            paths = self._iteration_paths
            
            length += 1
            if cb_pathsprogress:
                tc = paths[0].currentnode.timecode if paths[0].currentnode else None
                cb_pathsprogress(tc, length / graph.length)
        
        # Order the completed paths by score
        paths.sort(key=lambda p: p.data.totalscore(), reverse=True)
        
        # Finalize paths and copy from processing objects to hydata
        for path in paths:
            path.data.leftover_sp = path.sp
            # Variants were shared between paths while the search ran; each
            # finished path needs its own before prepare_variants writes to
            # them. See hydata.Path.detach_variants.
            path.data.detach_variants()
            path.data.prepare_variants()
            self.record._paths.append(path.data)
    
    def _read_native(
        self, graph, depth_mode, depth_value, ms_filter, cb_pathsprogress
    ):
        """read(), with the search run by the C++ engine.

        The graph is flattened to arrays, crosses the boundary once, and comes
        back as a decision log that hynative turns into the same hydata.Path
        objects this class would have built.

        The bound pruning runs natively too when ENABLE_BOUND_PRUNE is set. The
        ladder incumbent is still not passed across: it only ever raises the
        prune bar (more aggressive, never a different result), so leaving it out
        keeps the ABI change small at the cost of the native run pruning
        slightly less on the upper rungs than the Python run would.

        """
        flat = hyflat.flatten(graph)
        paths = hynative.search(
            flat, depth_mode, depth_value, ms_filter,
            hymisc.FLAG_SKIPPED_DYNAMICS, cb_pathsprogress,
            enable_bound_prune=ENABLE_BOUND_PRUNE,
        )
        self.record._paths.extend(paths)

    def _reduce_iteration_paths(self, depth_mode, depth_value, ms_filter):
        """Eliminates paths that are guaranteed to not make it into the final
        result because the score is too low (depth settings) or the timing
        difficulty is too high (ms filter).

        Also creates variants for paths that have identical scores.

        For a song in progress, two paths are only compared if they're in
        identical SP situations.
        """
        paths = self._iteration_paths

        # Before path comparisons, check each path against the ms filter.
        # Filtered paths cannot be used to eliminate paths, and are eliminated
        # immediately if worse than a single path.
        filtered_paths = set()
        if ms_filter is not None:
            for p in paths:
                if not p.data.passes_ms_filter(ms_filter):
                    filtered_paths.add(p)

        cmp_groups = {}
        optimal_score = None
        # Scores this song is already known to reach, from paths that can no
        # longer lose them: a finished path's score, and an unfinished base
        # track path's score plus the rest of the song played without
        # activating again. Both are achievable, so anything that cannot reach
        # them is out of the running. See _prune_hopeless_paths.
        pruning = ENABLE_BOUND_PRUNE
        guaranteed = list(self._incumbent) if pruning else None
        node_bounds = None
        for p in paths:
            # Every decision below is made on score. p.score is maintained by
            # the path itself as it moves, so this pass reads it rather than
            # re-summing the six categories once per path per iteration.
            score = p.score
            node = p.currentnode
            is_complete = node is None

            # Don't consider paths that recently SqIn/SqOuted as they have
            # interacted with an SP phrase earlier than other paths.
            if is_complete and (optimal_score is None or score > optimal_score):
                optimal_score = score

            if pruning:
                if is_complete:
                    guaranteed.append(score)
                else:
                    if node_bounds is None:
                        node_bounds = (node.base_suffix, node.max_suffix)
                    if not node.is_sp:
                        guaranteed.append(score + node.base_suffix)

            if p.buffered_sqinout_sp == 0:
                is_sp = not is_complete and node.is_sp
                if is_complete:
                    sp_value = 0
                else:
                    sp_value = p.sp_end_time if is_sp else p.sp

                cmp_group = (is_sp, sp_value)
                group = cmp_groups.get(cmp_group)
                if group is None:
                    cmp_groups[cmp_group] = [p]
                else:
                    group.append(p)

        paths_to_remove = set()
        for cmp_paths in cmp_groups.values():
            if len(cmp_paths) > 1:
                self._reduce_group(
                    cmp_paths, filtered_paths, optimal_score,
                    depth_mode, depth_value, paths_to_remove
                )

        if pruning:
            self._prune_hopeless_paths(
                paths, guaranteed, node_bounds, filtered_paths,
                depth_mode, depth_value, paths_to_remove
            )

        if paths_to_remove:
            self._iteration_paths = [p for p in paths if p not in paths_to_remove]

    def _prune_hopeless_paths(
        self, paths, guaranteed, node_bounds, filtered_paths,
        depth_mode, depth_value, paths_to_remove
    ):
        """Drop paths that cannot reach the results whatever they do next.

        The reduction above only ever compares paths that hold the same SP,
        because that is the only way to compare them exactly. That leaves
        paths in different SP situations unable to eliminate each other no
        matter how far apart their scores are, and raising the meter ceiling
        manufactures those situations by the hundred -- which is why cost
        climbs so steeply with the ceiling.

        This closes that gap without giving up exactness, by comparing what a
        path could *at best* still finish on against what other paths are
        *already guaranteed*. Both come from ScoreGraph.compute_bounds, and
        each errs in the safe direction, so a path is only dropped when it
        provably cannot appear:

        - a path's ceiling is its score plus max_suffix, an over-estimate
        - the bar is the depth setting applied to guaranteed scores, each of
          which some path can actually deliver

        With depth in 'scores' mode the results keep the best depth_value + 1
        distinct scores, so the bar is the (depth_value + 1)th largest
        distinct guaranteed score: clearing it is necessary to be listed.

        The loose max_suffix ceiling caught almost nothing, because it assumes
        the entire rest of the song is played in SP and no path can do that: a
        path holding b bars with r phrases left can be in SP for at most
        2 * (b + r) measures. The tighter ceiling below keeps every safe term
        of max_suffix but replaces its spscore component -- the "whole rest in
        SP" part -- with what a path can physically bank in that many measures,
        charged at the densest spscore rate left (compute_bounds' three suffix
        numbers). The frontend and backend terms stay at their over-estimated
        max_suffix values, so the ceiling is still >= any achievable finish and
        no path that could appear is ever dropped.

        The tight form holds exactly only for a base track path, whose b is the
        bars it is really holding. An SP-active path has already spent its
        meter (b would read 0) and 2*(0+r) understates the SP time left in its
        current activation, so those keep the loose max_suffix ceiling, which
        is always safe.

        """
        if node_bounds is None or not guaranteed:
            return      # every path finished this iteration

        if depth_mode == 'scores':
            band = depth_value + 1
            distinct = sorted(set(guaranteed), reverse=True)
            if len(distinct) < band:
                return  # not enough guaranteed results to rule anything out
            bar = distinct[band - 1]
        elif depth_mode == 'points':
            bar = max(guaranteed) - depth_value
        else:
            return

        for p in paths:
            node = p.currentnode
            if node is None or p in paths_to_remove:
                continue

            max_suffix = node.max_suffix
            if node.is_sp:
                ceiling = p.score + max_suffix
            else:
                total_sp = node.total_spscore_suffix
                sp_measures = 2 * (p.sp + node.remaining_sp_phrases)
                tight_sp = sp_measures * node.max_spscore_density
                if tight_sp > total_sp:
                    tight_sp = total_sp
                ceiling = p.score + max_suffix - total_sp + tight_sp

            if ceiling < bar:
                paths_to_remove.add(p)

    def _reduce_group(
        self, cmp_paths, filtered_paths, optimal_score,
        depth_mode, depth_value, paths_to_remove
    ):
        """Reduce one group of paths that are in the same SP situation.

        Every judgement here is a comparison of scores, and a score orders the
        group completely, so the group is read as a whole rather than pair by
        pair. That matters: comparing every pair meant a group of 2257 paths -
        which an uncapped chart reaches - cost 2.5 million comparisons in a
        single call, and the group grows with the song.

        """
        # Groups are small and extremely numerous: an uncapped discography
        # calls this ~4M times on groups averaging five paths, and the dict,
        # set, sort and per-path bisect below then cost more than the
        # comparisons they organise.
        #
        # In 'scores' mode a path is dropped only when more than depth_value
        # distinct scores beat it, so a group holding at most depth_value + 1
        # of them cannot drop anything. With no ties there is nothing to merge
        # either, and with nothing filtered nothing to remove on that account,
        # which leaves the general case below with no work to do.
        n = len(cmp_paths)
        if depth_mode == 'scores' and n <= depth_value + 1:
            scores = {p.score for p in cmp_paths}
            if len(scores) == n and not any(p in filtered_paths for p in cmp_paths):
                return

        # Ties first. Paths that score the same under the same filter status
        # are one result reached different ways, so the first of them carries
        # the rest as variants and continues on behalf of all of them.
        survivors = []
        tie_leaders = {}
        for p in cmp_paths:
            leader = tie_leaders.setdefault((p.score, p in filtered_paths), p)
            if leader is p:
                survivors.append(p)
                continue

            # Up to MAX_TIED_PATHS ways of scoring this much. Past that p is
            # simply dropped: it is neither kept nor followed any further.
            if leader.data.tied_pathcount() + p.data.tied_pathcount() <= MAX_TIED_PATHS:
                leader.data.add_variant(p.data, len(leader.data))
            paths_to_remove.add(p)

        if len(survivors) < 2:
            return

        # Distinct scores over the whole group, achievable or not. A filtered
        # path is kept only while it is still within the depth band *here* --
        # that is, while it could still turn out to be the single best path,
        # which is shown even when its timing is unachievable. Without this
        # band a filtered path is dropped only when an *achievable* path beats
        # it, and on an uncapped chart the achievable frontier scores far below
        # the hard paths, so every hard path survives and the frontier
        # explodes. The band collapses each group back to the depth setting,
        # exactly as it does for an unfiltered search, and cannot drop the
        # eventual best path (score dominance keeps the top band at every
        # step). See the module note on the ms filter.
        dominating = sorted({p.score for p in survivors})
        n_dominating = len(dominating)
        best_all = dominating[-1]

        # The scores that are allowed to eliminate an *achievable* path. A
        # filtered path can't, unless it's optimal. May be empty mid-search
        # (every live path in this group is filtered), which is fine: there is
        # then no achievable path to prune, and the band above still reins the
        # filtered ones in.
        beating_scores = sorted({
            p.score for p in survivors
            if p not in filtered_paths or p.score == optimal_score
        })
        best = beating_scores[-1] if beating_scores else None

        # Hoisted out of the loop below: the group can hold thousands of
        # paths, and these were being re-resolved for every one of them.
        n_beating = len(beating_scores)
        bisect_right = bisect.bisect_right
        mode_is_points = depth_mode == 'points'
        mode_is_scores = depth_mode == 'scores'

        for p in survivors:
            score = p.score

            if p in filtered_paths:
                # Removed once an achievable path beats it (as before), and
                # also once it falls outside the overall depth band -- past
                # that it can neither be shown nor become the best path.
                outscored_by = n_beating - bisect_right(beating_scores, score)
                if outscored_by:
                    paths_to_remove.add(p)
                elif mode_is_points:
                    if score + depth_value < best_all:
                        paths_to_remove.add(p)
                elif mode_is_scores:
                    outscored_by_all = (
                        n_dominating - bisect_right(dominating, score))
                    if outscored_by_all > depth_value:
                        paths_to_remove.add(p)
            elif mode_is_points:
                if score + depth_value < best:
                    paths_to_remove.add(p)
            elif mode_is_scores:
                # How many distinct achievable scores beat this path.
                outscored_by = n_beating - bisect_right(beating_scores, score)
                if outscored_by > depth_value:
                    paths_to_remove.add(p)

class GraphPath:
    """Quick early note:
    
    This class should do as little work as possible as it navigates the
    score graph. Any time that a GraphPath is *building* something, move
    it to ScoreGraph if at all possible.

    """
    # Paths are made by the million on a long chart, so they carry no
    # per-object dict.
    __slots__ = (
        'data', 'currentnode', 'sp', 'currentskips', 'buffered_sqinout_sp',
        'sp_end_time', 'sp_ready_time', 'skipped_e_offset', 'score',
    )

    def __init__(self, parent_path=None):
        # Running total of the six score categories on self.data, which is what
        # the reduction pass compares paths on. Kept in step by every place
        # here that scores a path, rather than re-summed per path per
        # iteration: that sum ran ~20M times per uncapped discography.
        # data.totalscore() remains the source of truth and is what the
        # finished paths are ordered by.
        self.score = 0

        if parent_path:
            self.data = parent_path.data.copy()

            self.score = parent_path.score
            self.currentnode = parent_path.currentnode
            self.sp = parent_path.sp
            self.currentskips = parent_path.currentskips
            self.buffered_sqinout_sp = parent_path.buffered_sqinout_sp
            self.sp_end_time = parent_path.sp_end_time
            self.sp_ready_time = parent_path.sp_ready_time
            self.skipped_e_offset = parent_path.skipped_e_offset
        else:
            self.data = hydata.Path()
            
            self.currentnode = None
            self.sp = 0
            self.currentskips = 0
            self.buffered_sqinout_sp = 0 # sp that was handled during a recent sqin/sqout, and needs to not be double counted
            self.sp_end_time = None
            self.sp_ready_time = None
            self.skipped_e_offset = None
    
    # Develop along the edge that leads farther into the song.
    # Always moves a path closer to being complete, unless it's already complete.
    def advance(self, sp_cap):
        #print("Path advancing:")

        # Runs ~1.9M times per chart, so the attribute lookups are hoisted:
        # is_complete() is spelled out to save the method call, and self.data
        # is read once instead of once per score component.
        node = self.currentnode
        if node is None:
            #print("\tOops, I was already done.")
            return

        adv_edge = node.adv_edge
        if adv_edge:
            data = self.data
            data.score_base += adv_edge.basescore
            data.score_combo += adv_edge.comboscore
            data.score_sp += adv_edge.spscore
            data.score_solo += adv_edge.soloscore
            data.score_accents += adv_edge.accentscore
            data.score_ghosts += adv_edge.ghostscore
            self.score += (
                adv_edge.basescore + adv_edge.comboscore + adv_edge.spscore
                + adv_edge.soloscore + adv_edge.accentscore + adv_edge.ghostscore
            )

            data.notecount += adv_edge.notecount
            
            #print(f"\tGoing to {adv_edge.dest.timecode.measurestr()}.")
            # Applying SP on this edge
            sp_times = adv_edge.sp_times
            buffered = self.buffered_sqinout_sp
            if node.is_sp:
                # Path is in SP: Immediately "spend" SP bars and adjust the sp end time
                #print(f"\tSP: {self.sp} + {len(adv_edge.sp_times)} = {min(max(0, self.sp + len(adv_edge.sp_times)), 4)}.")

                # Handling each sp individually since SP capping is based on each one's particular time
                # To do: Find a way to store the plusmeasures on the graph
                # The sp_times can be a tuple with the sp time and the sp time + 8 measures
                # The sp extension is actually the same as the pending_deact extensions in ScoreGraph so let's use those
                if sp_times:
                    sp_end_time = self.sp_end_time
                    for sptc, extension_map in sp_times:
                        if buffered > 0:
                            buffered -= 1
                        else:
                            sp_end_time = extension_map[sp_end_time]
                    self.sp_end_time = sp_end_time
                    self.buffered_sqinout_sp = buffered

            else:
                # Path isn't in SP: Add SP bars, discarding whatever overfills
                # the meter. Uncapped, nothing overfills and nothing is lost.
                old_sp = self.sp
                sp = old_sp + len(sp_times) - buffered
                if sp_cap is not None and sp > sp_cap:
                    sp = sp_cap
                self.sp = sp

                if old_sp < 2 and sp >= 2:
                    self.sp_ready_time = sp_times[1 - old_sp + buffered][0]

                self.buffered_sqinout_sp = 0

            data.multsqueezes += adv_edge.multsqueezes

            self.currentnode = adv_edge.dest
                    
            
        else:
            #print("\tReached the end actually, achieving enlightenment.")
            self.currentnode = None
   
    def branch_activate(self):
        """If valid, create a new path that is a clone of this path
        except that it has used the current branch edge on the graph
        to go from inactive to active sp.
        """
        assert(not self.is_complete())
        if not (br_edge := self.currentnode.branch_edge):
            return None
            
        # Must have enough SP
        if self.sp < 2:
            return None
            
        # sp ready time must be before this activation fill's deadline
        e_offset = br_edge.activation_fill_deadline_ms - self.sp_ready_time.ms
        
        # Thanks to the timing window, the cutoff is -70ms not 0ms
        if e_offset < -70:
            return None
                    
        new_path = GraphPath(parent_path=self)
        new_path.currentnode = br_edge.dest
        new_path.currentskips = 0
        
        # Activated paths immediately "spend" the sp and just know what time the sp ends.
        new_path.sp = 0
        
        new_act = hydata.Activation()
        new_act.skips = self.currentskips
        new_act.timecode = self.currentnode.timecode
        new_act.chord = self.currentnode.chord
        new_act.sp_meter = self.sp
        new_act.frontend_points = br_edge.frontend.points
        new_act.e_offset = self.skipped_e_offset if self.skipped_e_offset is not None else e_offset
        
        # The activation this one follows can no longer change, so fold it into
        # the running difficulty maximum before it stops being the last.
        new_path.data.close_last_activation()
        new_path.data._activations.append(new_act)
        new_path.data.score_sp += br_edge.frontend.points
        new_path.score += br_edge.frontend.points
        new_path.skipped_e_offset = None
        new_path.sp_ready_time = None
        new_path.sp_end_time = br_edge.activation_initial_end_times[self.sp]
        
        self.currentskips += 1
        
        if hymisc.FLAG_SKIPPED_DYNAMICS:
            if br_edge.frontend.chord.activation_note().is_accent():
                self.data.score_accents -= br_edge.skipped_dynamic_points
                self.data.skipped_accents += 1
                self.score -= br_edge.skipped_dynamic_points

            if br_edge.frontend.chord.activation_note().is_ghost():
                self.data.score_ghosts -= br_edge.skipped_dynamic_points
                self.data.skipped_ghosts += 1
                self.score -= br_edge.skipped_dynamic_points
            
        # Even if the E fill is skipped, the eventual activation should know about it
        if self.skipped_e_offset is None:
            self.skipped_e_offset = e_offset
    
        return new_path
        
    def create_deactivated_path(self, is_sq_out):
        new_path = GraphPath(parent_path=self)
        new_path.currentnode = self.currentnode.branch_edge.dest
        new_path.sp = 1 if is_sq_out else 0
        
        new_path.sp_end_time = None
        
        new_path.data._activations[-1].backends = self.currentnode.branch_edge.backends
        
        if is_sq_out:
            new_path.data._activations[-1].sqinouts.append(hydata.SqOut(self.currentnode.branch_edge.sqinout_timing))
        
        # Backend scoring adjustments. Accumulated once and applied to the
        # breakdown and the running total together, so the two cannot drift.
        sp_delta = 0
        for be in self.currentnode.branch_edge.backends:
            is_already_counted = be.offset_ms <= 0
            is_leeway = be.offset_ms > 0 and be.offset_ms < 3

            if is_sq_out:
                is_before_sqout = be.timecode < self.currentnode.branch_edge.sqinout_time
                is_exact_sqout = be.timecode == self.currentnode.branch_edge.sqinout_time
                is_after_sqout = be.timecode > self.currentnode.branch_edge.sqinout_time

                if is_already_counted:
                    if is_exact_sqout:
                        # Replace already-counted SP points with reduced sqout points.
                        sp_delta += -be.points + be.sqout_points
                    elif is_after_sqout:
                        # Remove aready-counted SP points, since in this path
                        # this backend was forced out of SP even though it's early.
                        sp_delta += -be.points
                elif is_leeway:
                    if is_before_sqout:
                        # Leeway squeeze (counted even though it's late).
                        sp_delta += be.points
                    elif is_exact_sqout:
                        # Leeway squeeze, but with the reduced sqout points.
                        sp_delta += be.sqout_points
            else:
                if is_leeway:
                    # Leeway squeeze (counted even though it's late)
                    sp_delta += be.points

        if sp_delta:
            new_path.data.score_sp += sp_delta
            new_path.score += sp_delta

        return new_path
        
    def branch_deactivate(self):
        """If valid, create a new path that is a clone of this path
        except that it has used the current branch edge on the graph
        to go from active to inactive sp.
        
        Also returns whether the path can be extended OR deactivated,
        which is not usually the case but can happen with sp phrase squeezes.
        """
        if not (br_edge := self.currentnode.branch_edge):
            return True, None
        
        deact_type = br_edge.deactivation_type(self.sp_end_time)
        if deact_type == DEACT_NONE:
            return True, None
        elif deact_type == DEACT_NORMAL:
            normal_deact = self.create_deactivated_path(False)
            return False, normal_deact
        elif deact_type == DEACT_SQINOUT:
            sqout_deact = self.create_deactivated_path(True)
            self.data._activations[-1].sqinouts.append(hydata.SqIn(br_edge.sqinout_timing))
            self.sp_end_time = br_edge.sqin_time
            # Avoid double-counting this SP when the path advances.
            self.buffered_sqinout_sp = br_edge.late_sqin_count
            sqout_deact.buffered_sqinout_sp = br_edge.late_sqin_count
            return True, sqout_deact
        else:
            raise Exception(f"Unexpected deactivation type: {deact_type}")
    
    def is_complete(self):
        return self.currentnode is None

    def is_active_sp(self):
        node = self.currentnode
        return node is not None and node.is_sp


# Backtrack plan sentinels for DPPather. A plan is either one of these or a
# tuple ('act', candidate_index, outcome_index, child_plan).
_PLAN_STOP = ('stop',)   # follow the base track to the end, never activating
_PLAN_DONE = ('done',)   # the path already completed (ran out the song in SP)


class DPPather:
    """Activation dynamic program over an already-built ScoreGraph.

    A drop-in alternative to GraphPather that finds the same optimal score by a
    different method. GraphPather enumerates every live path breadth-first;
    DPPather observes that between two activations a path follows the base track
    with no choices at all -- every note and SP phrase is collected
    deterministically -- so the only decisions are *where* to activate (which in
    turn fixes *how much* SP, since an activation always spends the whole meter,
    see GraphPath.branch_activate). It evaluates each decision once instead of
    carrying millions of paths.

    Phase 3 (this prototype) targets **best-score parity**: DPPather.record's
    best totalscore() must equal GraphPather's on every chart and every cap. It
    is the correctness reference for the eventual C++ port (Phase 4). Full
    field-by-field / ordering / variant parity is out of scope here.

    Method. Rather than re-derive Hydra's SP, squeeze and backend scoring, the
    DP *drives the real GraphPath state machine* over each deterministic
    segment, branching only at genuine activation choices. Every segment score
    is therefore identical to the BFS by construction. Reset state is the only
    thing memoized:

        key = (base_node_index, residual_sp, buffered_sqinout_sp)

    From a reset (song start, or a base-track node just returned to by a
    deactivation) the SP meter and the sp_ready_time along the forward base walk
    are fully determined, so the fill-deadline legality check is exact without
    carrying sp_ready_time as fuzzy state. Best(key) returns the top
    (depth_value + 1) distinct future scores, each with a backtrack plan; the
    winning plans are replayed once through the real GraphPath code to build
    genuine hydata.Path objects.

    Known limitations (Phase 3): the reset-state memo is only sound when
    hymisc.FLAG_SKIPPED_DYNAMICS is False (its default) -- the skipped-dynamic
    penalty makes future score depend on skip history, which the memo does not
    carry. incumbent, ms_filter and cb_pathsprogress are accepted for interface
    compatibility but ignored.
    """

    def __init__(self):
        self.record = hydata.HydraRecord()

    def read(
        self, graph, depth_mode, depth_value, ms_filter, cb_pathsprogress=None,
        incumbent=None
    ):
        self.record.ms_limit = ms_filter

        if hynative.SEARCH_ENABLED:
            self._read_native(
                graph, depth_mode, depth_value, ms_filter, cb_pathsprogress)
            return

        self._graph = graph
        self._sp_cap = graph.sp_meter_cap
        self._depth_mode = depth_mode
        self._depth_value = depth_value
        self._k = depth_value + 1

        # Index the base track. It is a single linear chain, and every
        # deactivation lands on one of its nodes, so identity -> index is a
        # complete addressing scheme for reset points and candidates.
        self._base_nodes = []
        self._index = {}
        node = graph.start
        while node is not None:
            self._index[node] = len(self._base_nodes)
            self._base_nodes.append(node)
            node = node.adv_edge.dest if node.adv_edge else None

        # sp_ready_time is only read for the fill-deadline check, and only its
        # .ms matters. The start of the song is the earliest ms in it, so using
        # it as a stand-in ready time when pre-computing an activation's SP
        # outcomes can never make a genuinely-legal activation look illegal.
        self._dummy_ready = graph.start.timecode

        self._node_memo = {}
        self._act_memo = {}

        # Deep charts chain many activations; the DP recurses once per activation
        # in the longest surviving chain. Give it headroom.
        old_limit = sys.getrecursionlimit()
        sys.setrecursionlimit(max(old_limit, 1000000))
        try:
            entries = self._best_from_node(0, 0, None, 0)
        finally:
            sys.setrecursionlimit(old_limit)

        datas = [self._replay(plan) for _score, plan in entries]
        datas.sort(key=lambda d: d.totalscore(), reverse=True)
        for data in datas:
            data.detach_variants()
            data.prepare_variants()
            self.record._paths.append(data)

    def _read_native(
        self, graph, depth_mode, depth_value, ms_filter, cb_pathsprogress
    ):
        """read(), with the DP run by the C++ engine (hy_dp_search).

        Mirrors GraphPather._read_native: the graph is flattened, crosses the
        boundary once, and comes back as the same hydata.Path objects the pure
        Python DP would have built. ms_filter and incumbent are not passed --
        the DP ignores them, exactly as the Python prototype does.

        """
        flat = hyflat.flatten(graph)
        paths = hynative.dp_search(
            flat, depth_mode, depth_value, ms_filter,
            hymisc.FLAG_SKIPPED_DYNAMICS, cb_pathsprogress,
        )
        self.record._paths.extend(paths)

    # -- The DP -------------------------------------------------------------

    def _best_from_node(self, node_index, sp, spr, buffered):
        """Top-k (score, plan) achievable from *arriving at* a base node.

        This is the candidate-granularity recurrence. Its only two moves are
        "activate here" (if this node is a legal activation candidate) and "skip
        to the next candidate". Skipping recurses a *single* step to the next
        candidate rather than walking to the end of the song and fanning out
        over every downstream candidate, and the memo is keyed at every
        candidate and landing -- so paths from different resets that converge on
        the same (node, meter) state share one sub-result. That is what turns
        the old O(candidates^2) reset fan-out into a per-candidate DP.

        The memo key carries sp_ready_time (its tick) because it gates the
        fill-deadline legality of activating here and is *not* implied by
        (node, sp): two histories can reach a node with the same meter but a
        different ready time. It is set once when the meter first reaches two
        bars and then held unchanged across skips, so it rides through the
        chain until an activation resets it. buffered rides along for the rare
        squeeze-out landing that lands directly on a candidate.

        The score returned excludes the base track already consumed to *reach*
        this node; the caller adds that. Plans are identical in shape to before
        (('act', base_index, outcome_index, child) / _PLAN_STOP / _PLAN_DONE),
        so _replay is unchanged.
        """
        spr_key = spr.ticks if spr is not None else -1
        key = (node_index, sp, spr_key, buffered)
        cached = self._node_memo.get(key)
        if cached is not None:
            return cached

        options = []
        node = self._base_nodes[node_index]
        be = node.branch_edge

        # Option A: activate at this node, if it is a legal candidate.
        if (
            be is not None and sp >= 2 and spr is not None
            and be.activation_fill_deadline_ms - spr.ms >= -70
        ):
            for oc in self._activation_outcomes(node, sp):
                if oc['completed']:
                    options.append((
                        oc['delta'],
                        ('act', node_index, oc['idx'], _PLAN_DONE),
                    ))
                else:
                    landing_index = self._index[oc['landing']]
                    for fut_score, fut_plan in self._best_from_node(
                        landing_index, oc['residual'], None, oc['buffered']
                    ):
                        options.append((
                            oc['delta'] + fut_score,
                            ('act', node_index, oc['idx'], fut_plan),
                        ))

        # Option B: skip -- walk the base track to the next candidate (or the
        # end), then continue from there.
        base_seg, next_index, sp2, spr2, buf2 = self._walk_to_next_candidate(
            node_index, sp, spr, buffered)
        if next_index is None:
            options.append((base_seg, _PLAN_STOP))
        else:
            for fut_score, fut_plan in self._best_from_node(
                next_index, sp2, spr2, buf2
            ):
                options.append((base_seg + fut_score, fut_plan))

        result = self._merge_topk(options)
        self._node_memo[key] = result
        return result

    def _walk_to_next_candidate(self, node_index, sp, spr, buffered):
        """Drive the base track forward from a node to the next candidate.

        Returns (base_score, next_candidate_index_or_None, sp, sp_ready_time,
        buffered) at that candidate. Reuses GraphPath.advance so the base score
        and the meter/ready-time evolution are exactly the BFS's. The base score
        between two base nodes is state-independent, but the meter is not, so
        this is driven per (entry state) rather than precomputed here.
        """
        seg = GraphPath()
        seg.currentnode = self._base_nodes[node_index]
        seg.sp = sp
        seg.buffered_sqinout_sp = buffered
        seg.sp_ready_time = spr
        seg.sp_end_time = None
        seg.data = hydata.Path()
        seg.score = 0

        sp_cap = self._sp_cap
        while True:
            seg.advance(sp_cap)
            node = seg.currentnode
            if node is None or node.is_sp:
                # End of song (or the guard the base track never trips).
                return (seg.score, None, seg.sp, seg.sp_ready_time,
                        seg.buffered_sqinout_sp)
            if node.branch_edge is not None:
                return (seg.score, self._index[node], seg.sp,
                        seg.sp_ready_time, seg.buffered_sqinout_sp)

    def _activation_outcomes(self, cand_node, sp):
        """Every way activating at cand_node holding `sp` bars can resolve.

        Returns a list of outcome dicts with pure score deltas (frontend + SP
        track + backend adjustments, excluding all base-track score before the
        activation), each landing the path back on the base track or completing
        it in SP. Depends only on (candidate, sp), so it is memoized.
        """
        key = (self._index[cand_node], sp)
        cached = self._act_memo.get(key)
        if cached is not None:
            return cached

        seed = GraphPath()
        seed.currentnode = cand_node
        seed.sp = sp
        seed.sp_ready_time = self._dummy_ready
        seed.data = hydata.Path()
        seed.score = 0
        activated = seed.branch_activate()

        outcomes = self._simulate_sp(activated)
        self._act_memo[key] = outcomes
        return outcomes

    def _simulate_sp(self, active_path):
        """Follow an activated path through the SP track, collecting outcomes.

        Reuses GraphPath.advance / branch_deactivate, so the SP scoring, the
        sqin/sqout branching (which yields a squeezed-out deactivation *and* a
        continuing path) and backend adjustments are exactly the BFS's.
        """
        sp_cap = self._sp_cap
        outcomes = []
        p = active_path
        idx = 0
        while p.currentnode is not None:
            p.advance(sp_cap)
            node = p.currentnode
            if node is None:
                # The song ended while still in SP: a completed path.
                outcomes.append({
                    'idx': idx, 'completed': True,
                    'delta': p.data.totalscore(), 'leftover': p.sp,
                })
                break
            if node.is_sp:
                can_extend, branchpath = p.branch_deactivate()
                if branchpath is not None:
                    outcomes.append({
                        'idx': idx, 'completed': False,
                        'delta': branchpath.data.totalscore(),
                        'landing': branchpath.currentnode,
                        'residual': branchpath.sp,
                        'buffered': branchpath.buffered_sqinout_sp,
                    })
                    idx += 1
                if not can_extend:
                    break
            else:
                break
        return outcomes

    def _merge_topk(self, options):
        """Collapse (score, plan) options to the best plan per distinct score,
        then keep the depth-mode's slice: the top k distinct scores ('scores'),
        or every score within depth_value points of the best ('points')."""
        if not options:
            return []
        best_plan = {}
        for score, plan in options:
            if score not in best_plan:
                best_plan[score] = plan
        scores = sorted(best_plan, reverse=True)
        if self._depth_mode == 'points':
            best = scores[0]
            scores = [s for s in scores if s + self._depth_value >= best]
        else:
            scores = scores[:self._k]
        return [(s, best_plan[s]) for s in scores]

    # -- Reconstruction -----------------------------------------------------

    def _replay(self, plan):
        """Rebuild a hydata.Path by replaying a backtrack plan through the real
        GraphPath code, so the result carries genuine activations, backends and
        squeezes -- not just the right total."""
        sp_cap = self._sp_cap
        p = GraphPath()
        p.currentnode = self._graph.start

        while True:
            if plan is _PLAN_DONE:
                break
            if plan is _PLAN_STOP:
                while p.currentnode is not None:
                    p.advance(sp_cap)
                    node = p.currentnode
                    if node is not None and not node.is_sp and node.branch_edge:
                        p.branch_activate()
                break

            _tag, cand_index, outcome_idx, child = plan

            # Advance to the chosen candidate, mirroring skipped activations so
            # skip counts and e-offsets stay faithful.
            while self._index.get(p.currentnode) != cand_index:
                p.advance(sp_cap)
                node = p.currentnode
                if node is None:
                    break
                if (
                    not node.is_sp and node.branch_edge
                    and self._index[node] != cand_index
                ):
                    p.branch_activate()

            activated = p.branch_activate()

            # Follow the SP track to the recorded outcome.
            q = activated
            idx = 0
            landing = None
            while q.currentnode is not None:
                q.advance(sp_cap)
                node = q.currentnode
                if node is None:
                    landing = ('completed', q)
                    break
                if node.is_sp:
                    can_extend, branchpath = q.branch_deactivate()
                    if branchpath is not None:
                        if idx == outcome_idx:
                            landing = ('deact', branchpath)
                            break
                        idx += 1
                    if not can_extend:
                        landing = ('deact', branchpath)
                        break
                else:
                    break

            if landing is None or landing[0] == 'completed':
                p = landing[1] if landing else q
                plan = _PLAN_DONE
                continue
            p = landing[1]
            plan = child

        p.data.leftover_sp = p.sp
        return p.data


def category_scores(chord, combo):
    """Calculates the score for hitting this chord with the current combo.
    
    Builds a complete score breakdown for base score, SP, combo, cymbals, and
    dynamics, then returns the combinations that Clone Hero uses.
    
    Some mechanics can result in a lower score for the chord.
    For sanity reasons these lower scores don't have their own complete
    score breakdowns, but for multiplier squeezes the point difference is just
    nice to know, its breakdown is not as important; and for SqOuts the points
    are all SP points despite the lack of complete detail.
    
    Some mechanics (multiplier squeezes and SP squeezes) can overlap in a way
    that makes the optimal strategy more complicated. This is super rare, so
    for now we're ignoring this possibility.
    
    """
    # The seventeen per-source buckets this used to accumulate into a dict
    # collapse to the five totals actually returned. Writing them directly
    # avoids building a 17-entry dict and doing ~34 lookups into it on every
    # call, and this runs once per chord per path.
    #
    # Derivation, per note, with c = cymbal points (15 or 0), d = 50 when the
    # note is dynamic else 0, dc = c when the note is dynamic else 0, and
    # mult/extra the combo multiplier and multiplier-1:
    #
    #   base  = 50 + c + dc                       (base_note, base_cymbal,
    #                                              dynamic_cymbal)
    #   K     = 50 + c + d + dc
    #   combo = extra * K                         (combo_* + combodynamic_*)
    #   sp    = mult  * K                         (sp_* + combosp_*
    #                                              + spdynamic_* + combospdynamic_*)
    #
    # combo and sp sharing K is not a coincidence: the sp buckets are the
    # combo buckets plus one unmultiplied copy, i.e. extra + 1 == mult.
    base_total = 0
    combo_total = 0
    sp_total = 0
    accent_total = 0
    ghost_total = 0

    # How many points to subtract if this chord is a SqOut
    sqout_reduction = 0

    # How many points to subtract if this chord is an activation chord
    # that gets skipped (the activation note cannot get its dynamic points)
    skipped_dynamic_reduction = 0
    
    ordering = chord.notes(basesorted=True)

    # The C++ core computes the same breakdown from the sorted notes. The
    # sort stays here because Python's sort is stable and the tie order is
    # observable through sqout_reduction, which reads note 0.
    if hynative.ENABLED:
        return hynative.category_scores(
            ordering,
            combo,
            hymisc.FLAG_SKIPPED_DYNAMICS,
            chord.activation_note() if hymisc.FLAG_SKIPPED_DYNAMICS else None,
        )

    skipped_dynamics = hymisc.FLAG_SKIPPED_DYNAMICS
    activation_note = chord.activation_note() if skipped_dynamics else None

    for i, note in enumerate(ordering):
        combo += 1
        combo_multiplier = to_multiplier(combo)
        extra = combo_multiplier - 1

        basevalue = 50
        cymbvalue = 15

        is_accent = note.is_accent()
        is_ghost = note.is_ghost()
        is_dynamic = is_accent or is_ghost
        cymb = cymbvalue if note.is_cymbal() else 0
        dyn_cymb = cymb if is_dynamic else 0
        dyn_note = basevalue if is_dynamic else 0

        k = basevalue + cymb + dyn_note + dyn_cymb

        base_total += basevalue + cymb + dyn_cymb
        combo_total += extra * k
        sp_total += combo_multiplier * k
        if is_accent:
            accent_total += basevalue
        elif is_ghost:
            ghost_total += basevalue

        # Quick and dirty SqOut calculation
        if i == 0:
            sqout_reduction = (
                (basevalue + cymb) * combo_multiplier * (2 if is_dynamic else 1)
            )

        if skipped_dynamics:
            if note == activation_note and is_dynamic:
                skipped_dynamic_reduction = (
                    basevalue + cymb + basevalue * extra + cymb * extra
                )

    return {
        'base': base_total,
        'combo': combo_total,
        'sp': sp_total,
        'accent': accent_total,
        'ghost': ghost_total,
        'sqout_reduction': sqout_reduction,
        'skipped_dynamic_reduction': skipped_dynamic_reduction
    }
