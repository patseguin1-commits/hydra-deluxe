"""Chart analysis as a unit of work, for running charts in parallel.

Pathing a chart is pure Python and CPU-bound, so threads cannot run two of
them at once: the GIL hands the interpreter to one thread at a time and the
second chart just waits. Separate processes genuinely run at once, which is
what a library of thousands of charts needs.

That shapes this module:

- It must import without dearpygui. A spawned worker re-imports the module
  its target function lives in, and no worker needs a UI.
- It must return plain data. Everything crosses a pipe, so the record is
  packed into its row here rather than shipped back as an object graph -
  which also puts the expensive half of a save on the worker.
- It must not raise. A failure is one chart's problem, and an exception
  that cannot be pickled would become the pool's problem instead, so the
  reason comes back as text.

"""

from . import hymisc
from . import hystore
from . import hyutil


def ping():
    """Round trip to prove a worker pool actually starts. See BatchJob."""
    return True


def analyze_for_store(job):
    """Analyze one chart into the values needed to store it.

    job is (hyhash, notespath, chartmode, settings, uncapped_sp), where
    settings is (difficulty, prodrums, bass2x, depth_mode, depth_value,
    ms_filter).

    Returns (row, tempomap, error). On success error is None; on failure
    row and tempomap are None and error is a line to show the user.

    """
    hyhash, notespath, chartmode, settings, uncapped_sp = job
    difficulty, prodrums, bass2x, depth_mode, depth_value, ms_filter = settings

    # The edition comes with the job rather than being left to whatever this
    # worker decided on import, so a library run cannot end up with capped
    # and uncapped records mixed together in one store.
    hymisc.apply_edition(uncapped_sp)

    try:
        record, tempomap = hyutil.analyze_chart_file(
            notespath,
            difficulty, prodrums, bass2x,
            depth_mode, depth_value,
            ms_filter,
            export_tempomap=True,
        )
        row = hystore.prepare_row(hyhash, chartmode, record)
    except Exception as e:
        return (None, None, hymisc.error_text(e))

    return (row, tempomap, None)
