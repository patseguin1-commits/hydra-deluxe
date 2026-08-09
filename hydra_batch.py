"""Batch-analyze many charts straight into the record store.

Reads difficulty / pro drums / 2x bass / depth from hyapp.ini, so results
match what the app would produce for the same songs.

    python hydra_batch.py                 # every folder in hyapp.ini
    python hydra_batch.py <folder> [...]  # specific folders instead
    python hydra_batch.py --redo          # re-analyze charts already stored
    python hydra_batch.py --reindex       # only rebuild sort columns, no analysis

Safe to interrupt and re-run: charts already stored for the current chartmode
are skipped unless --redo is given.

"""

import configparser
import sys
import time

import hydra.hymisc as hymisc
import hydra.hypath as hypath
import hydra.hystore as hystore
import hydra.hyutil as hyutil


def load_settings():
    """(difficulty, prodrums, bass2x, depth_mode, depth_value, ms_filter, folders)."""
    cfg = configparser.ConfigParser()
    try:
        with open(hymisc.INIPATH, 'r') as cfgfile:
            cfg.read_file(cfgfile)
    except FileNotFoundError:
        pass

    s = cfg['hydra'] if 'hydra' in cfg else {}

    folders = [f for f in s.get('chartfolders', "").strip().split('\n') if f]
    ms_filter = None
    if s.get('mslimit_enabled', 'False') == 'True':
        ms_filter = int(s.get('mslimit_value', '0'))

    return (
        s.get('view_difficulty', 'Expert'),
        s.get('view_prodrums', 'True') == 'True',
        s.get('view_bass2x', 'True') == 'True',
        s.get('depth_mode', 'scores'),
        int(s.get('depth_value', '4')),
        ms_filter,
        folders,
    )


def chartmode_key(difficulty, prodrums, bass2x):
    return (
        f"{difficulty} "
        f"{'Pro Drums' if prodrums else 'Drums'}, "
        f"{'2x Bass' if bass2x else '1x Bass'}"
    )


def main(argv):
    redo = '--redo' in argv
    reindex_only = '--reindex' in argv
    folder_args = [a for a in argv if not a.startswith('--')]

    difficulty, prodrums, bass2x, d_mode, d_value, ms_filter, ini_folders = load_settings()
    chartmode = chartmode_key(difficulty, prodrums, bass2x)

    store = hystore.RecordStore()

    if reindex_only:
        print("Rebuilding sort columns from stored records...")
        n = store.reindex()
        print(f"Reindexed {n} records.")
        store.close()
        return 0

    folders = folder_args or ini_folders
    if not folders:
        print("No chart folders. Add them in Hydra, or pass folders as arguments.")
        store.close()
        return 1

    print(f"Chart mode : {chartmode}")
    print(f"Depth      : {d_mode} {d_value}")
    print(f"Timing cap : {'none' if ms_filter is None else f'{ms_filter} ms'}")
    print(f"Squeeze win: {hypath.SQUEEZE_WINDOW_MS} ms")
    print(f"Folders    : {len(folders)}")
    for f in folders:
        print(f"    {f}")

    print("\nDiscovering charts...")
    scanitems, folder_errors = hyutil.discover_charts(folders)
    for err in folder_errors:
        print(f"  ! {err}")
    print(f"Found {len(scanitems)} charts.\n")

    analyzed = skipped = failed = 0
    failures = []
    started = time.time()

    for i, scanitem in enumerate(scanitems, 1):
        label = f"{scanitem.artist} - {scanitem.title}"

        if not redo and store.has_record(scanitem.md5, chartmode):
            skipped += 1
            continue

        try:
            record, tempomap = hyutil.analyze_chart_file(
                scanitem.notespath,
                difficulty, prodrums, bass2x,
                d_mode, d_value,
                ms_filter=ms_filter,
                export_tempomap=True,
            )
        except Exception as e:
            failed += 1
            failures.append(f"{label}: {e}")
            print(f"[{i}/{len(scanitems)}] FAILED {label}: {e}")
            continue

        store.add_song(scanitem, tempomap)
        store.add_record(scanitem.md5, chartmode, record)
        analyzed += 1

        best = record.best_path()
        print(f"[{i}/{len(scanitems)}] {best.totalscore():>10,}  {label[:52]:<52} {best.pathstring()[:36]}")

    elapsed = time.time() - started
    songs, records = store.counts()

    print(f"\nAnalyzed {analyzed}, skipped {skipped} already stored, {failed} failed"
          f" in {elapsed:.1f}s.")
    print(f"Store now holds {records} records across {songs} songs.")

    if failures:
        print("\nFailures:")
        for f in failures[:20]:
            print(f"  {f}")
        if len(failures) > 20:
            print(f"  ...and {len(failures) - 20} more.")

    store.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
