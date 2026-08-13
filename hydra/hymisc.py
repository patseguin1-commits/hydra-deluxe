import bisect
from functools import total_ordering
import os
import pathlib
import sys

"""Sort-of semantic version number for Hydra.

Major version update: Big milestones, like introducing a new major feature.
Minor version update: Most updates are these.
Patch version update: Hotfixes or tweaks.

For sanity reasons, records with a version that doesn't match completely
will not be supported and the app will just ask the user to re-analyze them.

"""
HYDRA_VERSION = (1,3,1)

"""Current Clone Hero version, just to display so the user can confirm."""
ENGINE_LABEL = "Clone Hero v1.1.0.6142"


"""Editions.

Every path Hydra finds rests on one rule taken from Clone Hero: the SP meter
holds 4 bars and nothing more, so a phrase collected on a full meter is
thrown away. That rule is why banking SP has a ceiling, why the longest
activation is 8 measures, and why a phrase collected late in an activation
can be worth nothing at all.

Hydra Uncapped is the same optimizer with that one rule removed, to answer
what the paths would look like if SP never overfilled. Its scores are not
reachable in Clone Hero; it's a what-if, not a simulation of the game.

The edition is fixed at startup rather than toggled, because it changes what
every stored record means. Records, settings and the database are kept
separate per edition (see the paths below and RECORD_VERSION), so the two can
sit side by side without either one overwriting or misreading the other.

Launch the uncapped edition with hydra_uncapped.py. For the command line
tools, set HYDRA_UNCAPPED=1 or pass --uncapped:

    python hydra_batch.py --uncapped

"""
UNCAPPED_SP = os.environ.get('HYDRA_UNCAPPED') == '1' or '--uncapped' in sys.argv

"""Display name of the running edition."""
EDITION_NAME = None

"""How many bars of SP the meter holds, or None for no ceiling.

An activation lasts 2 measures per bar spent, so this one number is also what
limits an activation to 8 measures and what limits how far a phrase collected
during SP can push the end of that activation out.

"""
SP_METER_CAP = None

"""Version a record is stamped with, and matched against when it's read back.

Capped and uncapped results are not interchangeable, so they get versions
that can never compare equal. A record from the other edition then reads as
out of date ("please re-analyze") instead of quietly showing paths that the
current edition would never have produced.

"""
RECORD_VERSION = None


def apply_edition(uncapped):
    """Make this process run the capped or uncapped edition.

    The three values above are a matched set: the rule used to path a chart
    and the version its record is stamped with have to agree, or the record
    ends up describing something other than what it claims. They are only
    ever set together, here.

    Worker processes call this. A worker is a fresh interpreter that worked
    out its own edition on import, and it is told the parent's instead of
    being trusted to have guessed the same one.

    File paths are deliberately not recomputed: they are read at startup, and
    a worker never opens the database anyway - it hands rows back.

    """
    global UNCAPPED_SP, EDITION_NAME, SP_METER_CAP, RECORD_VERSION

    UNCAPPED_SP = uncapped
    EDITION_NAME = "Hydra Uncapped" if uncapped else "Hydra"
    SP_METER_CAP = None if uncapped else 4

    RECORD_VERSION = HYDRA_VERSION + (('uncapped',) if uncapped else ())


apply_edition(UNCAPPED_SP)


def version_str():
    return '.'.join(str(n) for n in HYDRA_VERSION)


"""Ceilings the uncapped edition tries, in order, until the score settles.

Starts high enough that most charts answer on the first two rungs, and each
step doubles, so the run that finds the answer dominates the cost of proving
it. See hyutil._analyze_uncapped.

"""
SP_CAP_LADDER = (16, 32, 64, 128, 256, 512)

"""Seconds to spend raising the ceiling before settling for what's in hand.

Most charts settle in the first two rungs. A few do not, and they do not fail
politely: a 4-hour marathon chart was measured at just over 7 hours to reach
the top of the ladder, which in a library run means one worker gone for the
evening and a progress bar that reads as frozen.

Past this budget the search stops climbing and reports the best ceiling it
finished, flagged as unsettled. The first rung is always allowed to finish, so
there is always an answer to report.

"""
SP_CAP_TIME_BUDGET = 120


"""Feature flags"""
FLAG_SKIPPED_DYNAMICS = False

# Backends are collected out to hypath.SQUEEZE_WINDOW_MS because the scoring
# pass needs them all, but only those within this window are worth showing.
# Raise to match SQUEEZE_WINDOW_MS to see every backend the search considered.
BACKEND_DISPLAY_WINDOW_MS = 140


"""Static paths and files"""

if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
    ROOTPATH = pathlib.Path(sys._MEIPASS).resolve()
else:
    ROOTPATH = pathlib.Path(__file__).resolve().parent.parent

# Each edition writes its own files. Running uncapped from a source checkout
# otherwise lands in the same hyapp.db as the capped app, where the library
# scan and drop_stale_records() would happily delete the capped records for
# not matching the running version.
_EDITIONFILE = "_uncapped" if UNCAPPED_SP else ""

INIPATH = ROOTPATH / f"hyapp{_EDITIONFILE}.ini"
DBPATH = ROOTPATH / f"hyapp{_EDITIONFILE}.db"
FONTPATH_ANTQ = ROOTPATH / "resource" / "ShipporiAntiqueB1-Regular.ttf"
FONTPATH_MONO = ROOTPATH / "resource" / "CourierPrime-Regular.ttf"
BOOKPATH = ROOTPATH / f"records{_EDITIONFILE}.json"

ICOPATH_APP = ROOTPATH / "resource" / "icon_app.ico"
ICOPATH_RECORD = ROOTPATH / "resource" / "icon_record_32.png"
ICOPATH_STAR = ROOTPATH / "resource" / "icon_star_32.png"
ICOPATH_PENCIL = ROOTPATH / "resource" / "icon_pencil_32.png"
ICOPATH_HASH = ROOTPATH / "resource" / "icon_hash_32.png"


class ChartFileError(Exception):
    """Just a custom error for a chart file that doesn't work."""
    pass


def error_text(e):
    """What to show the user for a failed analysis.

    A ChartFileError is written for them to read, so show it as-is. Anything
    else is a surprise, and the type is the useful part.

    """
    return f"{e}" if isinstance(e, ChartFileError) else f"{e!r}"


class TempoMap(dict):
    """A song's tick-keyed meter or tempo map, with a cached lookup index.

    Converting a tick used to mean walking one of these from the top, so
    building a song's timecodes cost O(notes x tempo changes). On a chart
    with a busy tempo track that is most of the analysis: one 2800-note
    chart with 333 tempo marks spent 37% of its run inside _init_ms alone.
    The indexes below turn each conversion into a binary search.

    An index is derived from the map, and parsing writes tempo marks while
    timecodes are already being built, so it is cached on the map itself
    rather than beside it: there is then no way for one to outlive the state
    it describes. Every write drops it.

    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._ms_index = None
        self._mbt_index = None

    def _drop_indexes(self):
        self._ms_index = None
        self._mbt_index = None

    def __setitem__(self, key, value):
        self._drop_indexes()
        super().__setitem__(key, value)

    def __delitem__(self, key):
        self._drop_indexes()
        super().__delitem__(key)

    def update(self, *args, **kwargs):
        self._drop_indexes()
        super().update(*args, **kwargs)

    def clear(self):
        self._drop_indexes()
        super().clear()

    def pop(self, *args):
        self._drop_indexes()
        return super().pop(*args)

    def popitem(self):
        self._drop_indexes()
        return super().popitem()

    def setdefault(self, *args):
        self._drop_indexes()
        return super().setdefault(*args)

    def ms_index(self, tick_r):
        idx = self._ms_index
        if idx is None or idx.tick_r != tick_r:
            idx = self._ms_index = MsIndex(self, tick_r)
        return idx

    def mbt_index(self, tick_r):
        idx = self._mbt_index
        if idx is None or idx.tick_r != tick_r:
            idx = self._mbt_index = MeasureIndex(self, tick_r)
        return idx


def ms_index_for(bpm_map, tick_r):
    """The ms index for a tempo map, cached if the map can hold it.

    A plain dict still works - records loaded from the store bring their
    tempo map back as one - it just rebuilds the index each time, which is
    what every conversion used to do anyway.

    """
    if isinstance(bpm_map, TempoMap):
        return bpm_map.ms_index(tick_r)
    return MsIndex(bpm_map, tick_r)


def mbt_index_for(tpm_map, tick_r):
    """The measure index for a meter map. See ms_index_for."""
    if isinstance(tpm_map, TempoMap):
        return tpm_map.mbt_index(tick_r)
    return MeasureIndex(tpm_map, tick_r)


class MsIndex:
    """Where each tempo section starts, and the time elapsed by then."""
    __slots__ = ('tick_r', 'keys', 'tps', 'elapsed')

    def __init__(self, bpm_map, tick_r):
        self.tick_r = tick_r

        # A song's opening tempo is the mark at tick 0. The old walk read it
        # directly, so a map without one was a KeyError then too.
        if 0 not in bpm_map:
            raise KeyError(0)

        keys = sorted(bpm_map.keys())
        self.keys = keys
        self.tps = [bpm_map[k] * tick_r / 60 for k in keys]

        # Accumulated section by section, in the same order and with the same
        # arithmetic as the walk this replaces, so the two agree bit for bit.
        elapsed = [0.0]
        for i in range(1, len(keys)):
            elapsed.append(
                elapsed[-1] + (keys[i] - keys[i - 1]) / self.tps[i - 1] * 1000
            )
        self.elapsed = elapsed

    def at(self, ticks):
        i = bisect.bisect_right(self.keys, ticks) - 1
        if i < 0:
            # Before the first mark. An activation's E threshold can be a few
            # beats before the song starts, and the walk read those back at
            # the opening tempo.
            i = 0
        return self.elapsed[i] + (ticks - self.keys[i]) / self.tps[i] * 1000


class MeasureIndex:
    """Where each meter section starts, in ticks and in whole measures.

    Measures are counted the way the walk counted them: whole ones only, so a
    section boundary that doesn't land on a barline leaves a partial measure
    that carries into the next section.

    """
    __slots__ = ('tick_r', 'keys', 'tpm', 'starts', 'measures')

    def __init__(self, tpm_map, tick_r):
        self.tick_r = tick_r

        # As in MsIndex: the meter at tick 0 is what the walk started from.
        if 0 not in tpm_map:
            raise KeyError(0)

        keys = sorted(tpm_map.keys())
        self.keys = keys
        self.tpm = [tpm_map[k] for k in keys]

        # State on entering each section: the last barline at or before its
        # first tick, and how many measures have been counted by then.
        starts = [0]
        measures = [0]
        for i in range(1, len(keys)):
            whole = (keys[i] - starts[-1]) // self.tpm[i - 1]
            starts.append(starts[-1] + whole * self.tpm[i - 1])
            measures.append(measures[-1] + whole)
        self.starts = starts
        self.measures = measures

    def section_at(self, ticks):
        """Which section a tick is measured in.

        A tick sitting exactly on a section's first tick is measured with the
        section before it. That is what the walk did: it stopped as soon as it
        reached the tick it wanted, before picking up the new meter.

        """
        i = bisect.bisect_left(self.keys, ticks) - 1
        return i if i > 0 else 0


@total_ordering
class Timecode:
    """A point in time in a song, in multiple representations.
    
    The absolute way to measure time in songs is with ticks, but some contexts
    want to work with measures, beats, or milliseconds.
    
    Timecodes are created with a tick value and song context; the rest of the
    values are derived.
    
    All derived values are, like ticks, fully precise integers; except for
    milliseconds, which is a float value.

    """
    __slots__ = ('ticks', 'measure_beats_ticks', 'measures_decimal', 'ms')

    def __init__(self, ticks, tick_r, tpm_map, bpm_map):
        # Fundamental value
        self.ticks = ticks
        
        # Derived values
        self.measure_beats_ticks = [0, 0, 0]
        self.measures_decimal = 0.0
        self.ms = 0.0
        
        self._init_mbt(tick_r, tpm_map)
        self._init_ms(tick_r, bpm_map)
    
    def _init_mbt(self, tick_r, tpm_map):
        """Derive the measure/beat/tick position that corresponds to
        self.ticks from a song's meter.

        After all whole measures are counted, whole beats are counted.
        The remainder after measures and beats stays as ticks.

        """
        idx = mbt_index_for(tpm_map, tick_r)
        i = idx.section_at(self.ticks)
        tpm = idx.tpm[i]

        # Measures from the last barline the index knows about to our tick
        remaining = self.ticks - idx.starts[i]
        whole_measures = remaining // tpm
        remaining -= whole_measures * tpm

        measures = idx.measures[i] + whole_measures

        # Less than 1 measure remains: whole beats, then ticks left over
        self.measure_beats_ticks = (
            measures, remaining // tick_r, remaining % tick_r
        )

        # Alternate way to express being partway into a measure
        self.measures_decimal = measures + remaining / tpm

    def _init_ms(self, tick_r, bpm_map):
        """Derive milliseconds from a song's tempo map."""
        self.ms = ms_index_for(bpm_map, tick_r).at(self.ticks)

    def __eq__(self, other):
        return isinstance(other, Timecode) and self.ticks == other.ticks

    # Spelled out rather than left to @total_ordering, which builds the other
    # three from __lt__ and so answers every >= with an extra Python call.
    # Timecodes are compared several million times per chart: pathing asks on
    # every deactivation edge whether SP has run out yet, and the pending
    # deactivation heap orders by them.
    def __lt__(self, other):
        return self.ticks < other.ticks

    def __le__(self, other):
        return self.ticks <= other.ticks

    def __gt__(self, other):
        return self.ticks > other.ticks

    def __ge__(self, other):
        return self.ticks >= other.ticks

    def __hash__(self):
        return self.ticks
    
    def __repr__(self):
        return str(self.ticks)
        
    def is_measure_start(self):
        return self.measure_beats_ticks[1] == self.measure_beats_ticks[2] == 0 
    
    def measurestr(self, fixed_width=False):
        m, b, t = self.measure_beats_ticks
        if fixed_width:
            return f"{f"m{m+1}": >5}.{b + 1}.{t: <3}"
        else:
            return f"m{m + 1}.{b + 1}.{t}"        
    
    def plusmeasure(self, add_measures, song):
        """Returns a Timecode offset by the given number of measures.

        Partial measures will work by percentage rather than by
        number of beats or ticks. For example, "22 and a quarter measure"
        plus 2 measures will always equal "24 and a quarter measure" no
        matter the time signature.

        Results are cached per song. The answer is a pure function of the
        tick, the offset and the song's meter, but working it out walks the
        whole tpm map and then builds a Timecode, which walks it again. The
        same few offsets get asked for over and over -- ScoreGraph re-asks for
        every pending deactivation on every SP phrase -- and on a long chart
        that repetition is most of the time spent building the graph.

        Timecodes are values, compared and hashed by tick, so handing back the
        same instance twice is indistinguishable from building it twice.

        """
        cache = getattr(song, '_plusmeasure_cache', None)
        if cache is None:
            cache = song._plusmeasure_cache = {}

        key = (self.ticks, add_measures)
        if (cached := cache.get(key)) is not None:
            return cached

        result = self._plusmeasure_uncached(add_measures, song)
        cache[key] = result
        return result

    def _plusmeasure_uncached(self, add_measures, song):
        # Add the given measures (working in measures)
        m_decimal = self.measures_decimal + add_measures
        target_m = int(m_decimal)
        targetpartial = m_decimal % 1

        idx = mbt_index_for(song.tpm_changes, song.tick_resolution)

        # The section the target barline lands in: the first one whose measure
        # count reaches the target. Counting them one at a time was O(song
        # length) per call, and an activation asks for one of these per bar of
        # SP it could be holding.
        j = bisect.bisect_left(idx.measures, target_m, 1)
        i = min(j, len(idx.measures)) - 1
        handled_ticks = idx.starts[i] + (target_m - idx.measures[i]) * idx.tpm[i]

        if j < len(idx.measures) and handled_ticks == idx.keys[j]:
            # The target barline is the next section's first tick, so the
            # partial measure is measured in that section's meter.
            current_tpm = idx.tpm[j]
        else:
            current_tpm = idx.tpm[i]

        # Now that we have the right tpm, convert the partial measure to ticks
        partial = int(targetpartial * current_tpm)
        return Timecode(handled_ticks + partial, song.tick_resolution, song.tpm_changes, song.bpm_changes)

def to_multiplier(combo):
    if combo < 10:
        return 1
    elif combo < 20:
        return 2
    elif combo < 30:
        return 3
    else:
        return 4