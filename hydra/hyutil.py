import os
import json
import sqlite3
import configparser
import pathlib
import hashlib
import time
import struct
from dataclasses import dataclass

from . import hypath
from . import hydata
from . import hysong
from . import hymisc


@dataclass
class ScanItem:
    md5: str
    title: str
    artist: str
    charter: str
    notespath: str
    rootfolder: str
    
    def __repr__(self):
        return f"ScanItem{vars(self)}"
    
    def db_values(self):
        return (self.md5, self.title, self.artist, self.charter, self.notespath, self.rootfolder)
    
    @staticmethod
    def db_cols():
        return 'md5,name,artist,charter,path,folder'
    
    @staticmethod
    def from_notes_ini_pair(f_notes, f_ini, rootfolder=None):
        with open(f_notes, 'rb') as f:
            md5 = hashlib.file_digest(f, "md5").hexdigest()
        title, artist, charter = ScanItem.get_metadata_ini(f_ini)
        return ScanItem(md5, title, artist, charter, f_notes, rootfolder)
    
    @staticmethod
    def from_sng(f_sng, rootfolder=None):
        with open(f_sng, 'rb') as f:
            md5 = hashlib.file_digest(f, "md5").hexdigest()
        title, artist, charter = ScanItem.get_metadata_sng(f_sng)
        return ScanItem(md5, title, artist, charter, f_sng, rootfolder)

    @staticmethod
    def from_db(db_row):
        return ScanItem(*db_row)

    @staticmethod
    def get_metadata_ini(f_ini):
        config = configparser.ConfigParser(
            strict=False, allow_no_value=True, interpolation=None
        )
        # utf-8 should work but try to do other encodings if it doesn't
        for codec in ['utf-8', 'utf-8-sig', 'ansi']:
            try:
                config.read(f_ini, encoding=codec)
                break
            except (configparser.MissingSectionHeaderError, UnicodeDecodeError):
                continue
       
        # Song inis have one section
        if 'Song' in config:
            metadata = config['Song']
        elif 'song' in config:
            metadata = config['song']
        else:
            raise hymisc.ChartFileError(f"Invalid ini format: {f_ini}")
        
        title = metadata.get('name', "<unknown title>")
        artist = metadata.get('artist', "<unknown artist>")
        charter = metadata.get('charter', "<unknown charter>")
        
        return (title, artist, charter)
    
    @staticmethod
    def get_metadata_sng(f_sng):
        title = "<unknown title>"
        artist = "<unknown artist>"
        charter = "<unknown charter>"
        
        with open(f_sng, mode='rb') as bytes:
            METADATACOUNT_OFFSET = 34
            bytes.seek(METADATACOUNT_OFFSET, 0)
            metadata_count = struct.unpack('Q', bytes.read(8))[0]
            
            for i in range(metadata_count):
                key_len = struct.unpack('I', bytes.read(4))[0]
                key = bytes.read(key_len).decode('utf-8').casefold()
                value_len = struct.unpack('I', bytes.read(4))[0]
                value = bytes.read(value_len).decode('utf-8')
                
                match key:
                    case 'name':
                        title = value
                    case 'artist':
                        artist = value
                    case 'charter':
                        charter = value
        
        return (title, artist, charter)

def get_folder_count(rootfolders, cb_progress=None):
    """Same folder search as discover_charts, but only counts the folders.
    
    Allows the slower part of the scan to know how far along it is.
    """
    # DFS with no repeats
    unexplored = [(root, root) for root in rootfolders if os.path.isdir(root)]
    visited = set(rootfolders)
    
    while unexplored:
        dir, origin = unexplored.pop()
        
        subpaths = [os.path.join(dir, f) for f in os.listdir(dir)]
        for subpath in (os.path.join(dir, f) for f in os.listdir(dir)):
            if os.path.isdir(subpath) and subpath not in visited:
                visited.add(subpath)
                unexplored.append((subpath, origin))
                if cb_progress:
                    cb_progress(len(visited))
    
    return len(visited)
    
def discover_charts(rootfolders, cb_progress=None):
    """Recursively searches for charts in the given root folders.
    
    Re-encountered folders will be skipped.
    
    Procedure:
    - At each folder visited, check files present.
    - Add "notes.mid" if "song.ini" is present.
    - If there was no "notes.mid", add "notes.chart" if "song.ini" is present.
    - Add all .sng files.
    
    """
    scanitems = []
    errors = []
    
    def process_folder(folder, origin_folder):
        found_mid = None
        found_chart = None
        found_ini = None
        found_sngs = []
        
        for file, fullpath in ((f, os.path.join(folder, f)) for f in os.listdir(folder)):
            if os.path.isfile(fullpath):
                if file == "notes.mid":
                    found_mid = fullpath
                elif file == "notes.chart":
                    found_chart = fullpath
                elif file == "song.ini":
                    found_ini = fullpath
                elif file.casefold().endswith(".sng"):
                    found_sngs.append(fullpath)
        
        if found_mid and found_ini:
            # Add .mid
            scanitems.append(ScanItem.from_notes_ini_pair(found_mid, found_ini, rootfolder=origin_folder))
        elif found_chart and found_ini:
            # Add .chart
            scanitems.append(ScanItem.from_notes_ini_pair(found_chart, found_ini, rootfolder=origin_folder))        
        
        for f_sng in found_sngs:
            # Add .sng
            scanitems.append(ScanItem.from_sng(f_sng, rootfolder=origin_folder))
    
    # DFS with no repeats
    unexplored = [(root, root) for root in rootfolders if os.path.isdir(root)]
    visited = set(rootfolders)
    
    while unexplored:
        dir, origin = unexplored.pop()
        
        try:
            process_folder(dir, os.path.relpath(pathlib.Path(dir).parent, origin))
                
            subpaths = [os.path.join(dir, f) for f in os.listdir(dir)]
            for subpath in (os.path.join(dir, f) for f in os.listdir(dir)):
                if os.path.isdir(subpath) and subpath not in visited:
                    visited.add(subpath)
                    if cb_progress:
                        cb_progress(len(visited))
                    unexplored.append((subpath, origin))
                
        except Exception as e:
            errors.append(e)
            continue
    
    return (scanitems, errors)
    

def analyze_chart_file(
    filepath,
    m_difficulty, m_pro, m_bass2x,
    d_mode, d_value,
    ms_filter=None,
    cb_parsecomplete=None, cb_pathsprogress=None,
    export_tempomap=False
):
    """Entry point for analyzing a chart file.
    
    Accepts .mid, .chart, and .sng.
    
    """
    if filepath.casefold().endswith(".mid"):
        load_songpath = hysong.load_songpath_mid
    elif filepath.casefold().endswith(".chart"):
        load_songpath = hysong.load_songpath_chart
    elif filepath.casefold().endswith(".sng"):
        load_songpath = hysong.load_songpath_sng
    else:
        raise hymisc.ChartFileError(f"Unexpected chart filetype: {filepath}")
    
    song = load_songpath(filepath, m_difficulty, m_pro, m_bass2x)
    
    if cb_parsecomplete:
        cb_parsecomplete()
        
    return _analyze(song, m_difficulty, m_pro, m_bass2x, d_mode, d_value, ms_filter, cb_pathsprogress, export_tempomap)


def analyze_chart_bytes_mid(
    chartbytes,
    m_difficulty, m_pro, m_bass2x,
    d_mode, d_value,
    ms_filter=None,
    cb_parsecomplete=None, cb_pathsprogress=None,
    export_tempomap=False
):
    """Entry point for analyzing a chart from the bytes of a .mid file.
    
    Use this with an SNG that has already been decoded for its .mid file.
    Otherwise, use analyze_chart_file.
    
    """
    song = hysong.load_songbytes_mid(chartbytes, m_difficulty, m_pro, m_bass2x)
    
    if cb_parsecomplete:
        cb_parsecomplete()
    
    return _analyze(song, m_difficulty, m_pro, m_bass2x, d_mode, d_value, ms_filter, cb_pathsprogress, export_tempomap)

def analyze_chart_bytes_chart(
    chartbytes,
    m_difficulty, m_pro, m_bass2x,
    d_mode, d_value,
    ms_filter=None,
    cb_parsecomplete=None, cb_pathsprogress=None,
    export_tempomap=False
):
    """Entry point for analyzing a chart from the bytes of a .chart file.
    
    Use this with an SNG that has already been decoded for its .chart file.
    Otherwise, use analyze_chart_file.
    
    """
    song = hysong.load_songbytes_chart(chartbytes, m_difficulty, m_pro, m_bass2x)
    
    if cb_parsecomplete:
        cb_parsecomplete()
    
    return _analyze(song, m_difficulty, m_pro, m_bass2x, d_mode, d_value, ms_filter, cb_pathsprogress, export_tempomap)
    
def _analyze(
    song,
    m_difficulty, m_pro, m_bass2x,
    d_mode, d_value,
    ms_filter=None,
    cb_pathsprogress=None,
    export_tempomap=False
):
    """Uses hydra to produce a pathing result for one song with the given settings.
    
    Use analyze_chart_bytes_mid, analyze_chart_bytes_chart, or analyze_chart_file
    to get the song input for this function.
    
    """
    # Guitar-only charts, and charts whose drums track has nothing written at
    # this difficulty, parse into a song with no timestamps at all. Say so
    # here: ScoreGraph would otherwise reach for song.last and raise a bare
    # IndexError that says nothing about which chart or why.
    if song.is_empty():
        kit = "pro drums" if m_pro else "drums"
        raise hymisc.ChartFileError(f"No {m_difficulty} {kit} notes in this chart.")

    if hymisc.SP_METER_CAP is None:
        record = _analyze_uncapped(song, d_mode, d_value, ms_filter, cb_pathsprogress)
    else:
        record = _analyze_at_cap(
            song, hymisc.SP_METER_CAP, d_mode, d_value, ms_filter, cb_pathsprogress)

    if export_tempomap:
        tempo_map = {
            'res': song.tick_resolution,
            'tpm': {t: v for t,v in song.tpm_changes.items()},
            'bpm': {t: v for t,v in song.bpm_changes.items()}
        }
        return (record, tempo_map)

    return record


def _analyze_at_cap(
    song, sp_cap, d_mode, d_value, ms_filter, cb_pathsprogress, incumbent=None,
    build_cap=None
):
    """One pathing run with a given SP meter ceiling.

    incumbent is scores already shown to be reachable for this song at a lower
    ceiling; see GraphPather.read.

    build_cap is the ceiling the graph is actually built at, when that can be
    lowered without changing the answer; the record still reports sp_cap,
    because that is the ceiling the result is true for. See _analyze_uncapped.

    """
    graph = hypath.ScoreGraph(
        song, sp_meter_cap=sp_cap if build_cap is None else build_cap)

    pather = hypath.GraphPather()
    pather.read(
        graph, d_mode, d_value, ms_filter, cb_pathsprogress, incumbent=incumbent
    )

    pather.record.sp_cap = sp_cap
    return pather.record


def _analyze_uncapped(song, d_mode, d_value, ms_filter, cb_pathsprogress):
    """Path with no SP meter ceiling, by raising one until it stops mattering.

    Running with no ceiling at all is the honest way to ask the question and
    the wrong way to answer it. Cost grows steeply with the ceiling -- roughly
    2.5x per doubling -- because a path holding a different number of bars is
    a different path, and nothing merges them. On a discography, with hundreds
    of SP phrases, "no ceiling" means every bar count up to several hundred,
    and the search does not finish.

    It does not need to. The bars a chart can actually put to use are limited
    by the music, not by the meter: past some ceiling the optimizer stops
    finding anything to do with the extra SP and the score stops moving. So
    the ceiling is raised until two in a row agree, and that score is the
    uncapped answer.

    Two agreeing runs are strong evidence, not proof -- a chart could in
    principle sit still and then improve again. That is why the ceiling
    reached and whether it settled are both recorded: a result that ran out of
    ladder says so instead of quietly passing for converged.

    """
    record = None
    previous_score = None
    deadline = None
    incumbent = None

    # A path can only bank what the song hands out, so this is the largest
    # meter any path could ever fill, and a ceiling above it cannot bind: the
    # meter clamp is unreachable, and the overfill clamp in extend_deacts sits
    # at 2*cap measures past a phrase, further out than the most SP a path
    # could be holding there. Every ceiling at or above this count therefore
    # describes the same search.
    #
    # That is worth two things. The graph is built at the smaller ceiling, and
    # the ladder can stop the moment it reaches this count instead of running
    # another rung to confirm -- most charts have well under 16 phrases, which
    # collapses the whole ladder to one run at a fraction of its width.
    sp_phrases = sum(1 for ts in song._sequence if ts.flag_sp)

    for sp_cap in hymisc.SP_CAP_LADDER:
        # The record keeps reporting the rung, not the ceiling the graph was
        # built at: the result is true at the rung, which is what a reader of
        # "SP meter: n bars" is being told.
        build_cap = min(sp_cap, max(sp_phrases, 1))

        try:
            candidate = _analyze_at_cap(
                song, sp_cap, d_mode, d_value, ms_filter,
                _deadline_callback(cb_pathsprogress, deadline),
                incumbent=incumbent, build_cap=build_cap,
            )
        except _CapBudgetExceeded:
            # Out of time partway up. Keep the best rung that finished; the
            # abandoned one has a half-built record and is thrown away.
            break

        record = candidate
        score = record.best_path().totalscore() if record._paths else None

        # Settled by the argument above rather than by two rungs agreeing:
        # there is no higher ceiling left that could score differently.
        if sp_cap >= sp_phrases:
            record.sp_cap_converged = True
            return record

        if previous_score is not None and score == previous_score:
            record.sp_cap_converged = True
            return record

        previous_score = score

        # Hand the next rung the score this one reached. A path that fits
        # under this ceiling still fits under a larger one, so that score is
        # guaranteed to be matched up there and the next rung need not follow
        # anything that cannot reach it.
        #
        # Only the best score is carried, not the whole result list. The best
        # is certain to be available again; the runners-up are not, because a
        # looser ceiling can lift several of them onto the same score.
        #
        # This is only consumed by the bound pruning in hypath, which is
        # currently off because it does not earn its cost - see
        # hypath.ENABLE_BOUND_PRUNE. The plumbing stays because the rung
        # ordering that makes the guarantee true is the ladder's business,
        # not the pather's.
        incumbent = (score,) if score is not None else None

        # Only now does the clock start, so the first rung always finishes and
        # there is always something to report.
        if deadline is None and hymisc.SP_CAP_TIME_BUDGET:
            deadline = time.monotonic() + hymisc.SP_CAP_TIME_BUDGET

    # Either the ladder ran out or the budget did. Whichever rung got furthest
    # is the best answer available, and it is flagged as unsettled.
    if record is not None:
        record.sp_cap_converged = False
    return record


class _CapBudgetExceeded(Exception):
    """Raised out of the progress callback to abandon a too-slow rung."""


def _deadline_callback(cb_pathsprogress, deadline):
    """The caller's progress callback, with a stop-watch attached.

    Checking between rungs is not enough: a rung that will overrun the budget
    has to be abandoned partway, or the budget only bounds when the next one
    starts. The pather reports progress once per iteration, which makes it the
    one place a run can be interrupted without teaching it about deadlines.

    """
    if deadline is None:
        return cb_pathsprogress

    def guarded(timecode, progressf):
        if time.monotonic() > deadline:
            raise _CapBudgetExceeded
        if cb_pathsprogress:
            cb_pathsprogress(timecode, progressf)

    return guarded

def count_chart_chords(filepath):
    # Parse chart file and make a song object
    if filepath.endswith(".mid"):
        parser = hysong.MidiParser()
    elif filepath.endswith(".chart"):
        parser = hysong.ChartParser()
    else:
        raise hymisc.ChartFileError(f"Unexpected chart filetype: {filepath}")
    
    parser.parsefile(filepath, 'Expert', True, True)
    
    counts = {}
    for ts in parser.song._sequence:
        if ts.chord in counts:
            counts[ts.chord] += 1
        else:
            counts[ts.chord] = 1
    
    return counts