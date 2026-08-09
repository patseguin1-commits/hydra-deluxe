"""SQLite-backed storage for analysis records.

Records used to live in a single records.json that was rewritten in full on
every add, which made one save O(library) and a whole session O(n^2). Here a
record is one row: saves are O(1), startup loads nothing, and the library
table reads denormalized summary columns instead of unpacking blobs.

Lives in the same hyapp.db as the chart library, but in its own tables --
scan_library() drops and recreates 'charts', so records must not be in it.

"""

import json
import sqlite3
import zlib

from . import hydata
from . import hymisc


_SCHEMA = """
CREATE TABLE IF NOT EXISTS songmeta (
    hyhash      TEXT PRIMARY KEY,
    ref_name    TEXT,
    ref_artist  TEXT,
    ref_charter TEXT,
    tempomap    TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS records (
    hyhash    TEXT NOT NULL,
    chartmode TEXT NOT NULL,
    hyversion TEXT NOT NULL,
    bestpath  TEXT NOT NULL,
    blob      BLOB NOT NULL,
    PRIMARY KEY (hyhash, chartmode)
);
"""

# Records are only read one at a time (the details panel), so a small cache is
# plenty; it exists to stop repeat views re-inflating the same blob.
_CACHE_LIMIT = 32

_ZLIB_LEVEL = 6


def _restore_timecodes(record, tempomap):
    """Turn a loaded record's raw tick values back into full Timecodes.

    Was HyAppRecordBook._init_timecodes, but scoped to one record so it runs
    on view instead of over the whole library at startup.

    """
    made = {}
    for path in record.all_paths():
        for act in path._activations:
            if act.timecode not in made:
                made[act.timecode] = hymisc.Timecode(act.timecode, *tempomap)
            act.timecode = made[act.timecode]

            for bsq in act.backends:
                if bsq.timecode not in made:
                    made[bsq.timecode] = hymisc.Timecode(bsq.timecode, *tempomap)
                bsq.timecode = made[bsq.timecode]


def _pack(record):
    """Serialize a record, keeping only the backends that get displayed.

    Backends are ~40% of a stock record and more once the squeeze window is
    widened, and a stored record's backends are display-only: scoring happens
    during analysis, against graph edges, never against a loaded record.

    The trim is done on a temporary swap so the caller's live record (which
    is about to be shown) keeps everything it had.

    """
    swapped = [
        (act, act.backends)
        for path in record.all_paths()
        for act in path._activations
    ]
    for act, _ in swapped:
        act.backends = act.display_backends()
    try:
        payload = json.dumps(record, default=hydata.json_save, separators=(',', ':'))
    finally:
        for act, original in swapped:
            act.backends = original

    return zlib.compress(payload.encode('utf-8'), _ZLIB_LEVEL)


def _unpack(blob):
    return json.loads(zlib.decompress(blob).decode('utf-8'), object_hook=hydata.json_load)


class RecordStore:
    """Reads and writes analysis records, one row each."""

    def __init__(self, dbpath=None):
        self.dbpath = hymisc.DBPATH if dbpath is None else dbpath
        self.cxn = sqlite3.connect(self.dbpath)
        self.cxn.executescript(_SCHEMA)
        self.cxn.commit()
        self._cache = {}

    def close(self):
        self.cxn.close()

    """Writing"""

    def add_song(self, scanitem, tempomap):
        """Register a song so records can be stored against it."""
        self.cxn.execute(
            "INSERT OR IGNORE INTO songmeta VALUES (?,?,?,?,?)",
            (
                scanitem.md5,
                scanitem.title,
                scanitem.artist,
                scanitem.charter,
                json.dumps(tempomap, separators=(',', ':')),
            ),
        )
        self.cxn.commit()

    def add_record(self, hyhash, chartmode, record):
        """Store one record. Constant cost, whatever the library size."""
        bestpath = ""
        if record.is_version_compatible() and record._paths:
            bestpath = record.best_path().pathstring()

        self.cxn.execute(
            "INSERT OR REPLACE INTO records VALUES (?,?,?,?,?)",
            (
                hyhash,
                chartmode,
                json.dumps(list(record.hyversion)),
                bestpath,
                _pack(record),
            ),
        )
        self.cxn.commit()
        self._cache.pop((hyhash, chartmode), None)

    """Reading"""

    def get_summary(self, hyhash, chartmode):
        """(hyversion, bestpath) for the library table, without touching blobs.

        Returns None if there's no record.

        """
        row = self.cxn.execute(
            "SELECT hyversion, bestpath FROM records WHERE hyhash=? AND chartmode=?",
            (hyhash, chartmode),
        ).fetchone()

        if row is None:
            return None

        return tuple(json.loads(row[0])), row[1]

    def get_record(self, hyhash, chartmode):
        """The full record, inflated on demand. None if there isn't one."""
        key = (hyhash, chartmode)
        if key in self._cache:
            return self._cache[key]

        row = self.cxn.execute(
            "SELECT blob FROM records WHERE hyhash=? AND chartmode=?", key
        ).fetchone()

        if row is None:
            return None

        record = _unpack(row[0])

        # An incompatible record is returned empty by json_load, so there are
        # no timecodes to rebuild and no tempomap lookup worth doing.
        if record.is_version_compatible():
            if (tempomap := self.get_tempomap(hyhash)) is not None:
                _restore_timecodes(record, tempomap)

        if len(self._cache) >= _CACHE_LIMIT:
            self._cache.clear()
        self._cache[key] = record

        return record

    def get_tempomap(self, hyhash):
        """(res, tpm_map, bpm_map), or None if the song isn't registered.

        Loaded through hydata.json_load because the tpm/bpm maps are keyed by
        integer tick values, and plain json would hand them back as strings.

        """
        row = self.cxn.execute(
            "SELECT tempomap FROM songmeta WHERE hyhash=?", (hyhash,)
        ).fetchone()

        if row is None:
            return None

        tm = json.loads(row[0], object_hook=hydata.json_load)
        return (tm['res'], tm['tpm'], tm['bpm'])

    """Maintenance"""

    def drop_stale_records(self):
        """Remove records that no longer match this Hydra version."""
        current = json.dumps(list(hymisc.HYDRA_VERSION))
        cur = self.cxn.execute("DELETE FROM records WHERE hyversion != ?", (current,))
        self.cxn.commit()
        self._cache.clear()
        return cur.rowcount

    def counts(self):
        songs = self.cxn.execute("SELECT COUNT(*) FROM songmeta").fetchone()[0]
        records = self.cxn.execute("SELECT COUNT(*) FROM records").fetchone()[0]
        return songs, records

    def migrate_json(self, jsonpath):
        """Import a legacy records.json. Returns (songs, records) imported.

        Safe to call repeatedly: songs are INSERT OR IGNORE and records are
        keyed, so a partial run just resumes.

        """
        with open(jsonpath, 'r') as jsonfile:
            book = json.load(jsonfile, object_hook=hydata.json_load)

        if not book:
            return 0, 0

        songs = records = 0
        for hyhash, info in book.items():
            tempomap_dict = info['tempomap']
            self.cxn.execute(
                "INSERT OR IGNORE INTO songmeta VALUES (?,?,?,?,?)",
                (
                    hyhash,
                    info.get('ref_name'),
                    info.get('ref_artist'),
                    info.get('ref_charter'),
                    json.dumps(tempomap_dict, separators=(',', ':')),
                ),
            )
            songs += 1

            tempomap = (tempomap_dict['res'], tempomap_dict['tpm'], tempomap_dict['bpm'])
            for chartmode, record in info['records'].items():
                # json_save reads timecode.ticks, so the raw tick values that
                # came out of the file have to become Timecodes again first.
                if record.is_version_compatible():
                    _restore_timecodes(record, tempomap)

                bestpath = ""
                if record.is_version_compatible() and record._paths:
                    bestpath = record.best_path().pathstring()

                self.cxn.execute(
                    "INSERT OR REPLACE INTO records VALUES (?,?,?,?,?)",
                    (
                        hyhash,
                        chartmode,
                        json.dumps(list(record.hyversion)),
                        bestpath,
                        _pack(record),
                    ),
                )
                records += 1

        self.cxn.commit()
        return songs, records
