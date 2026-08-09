import os
import pathlib
import configparser
import sqlite3
import time
import traceback

import pyperclip

import dearpygui.dearpygui as dpg
import dearpygui.demo as demo

import hydra.hymisc as hymisc
import hydra.hyutil as hyutil
import hydra.hystore as hystore

from timeit import default_timer as timer

"""Error reporting"""


def log_error(context):
    """Record the current exception to a log beside the database.

    A windowed build has no console, and dearpygui swallows exceptions raised
    inside callbacks, so without this a failed refresh is completely silent.

    """
    try:
        with open(hymisc.ROOTPATH / "hydra_errors.log", 'a', encoding='utf-8') as f:
            f.write(f"--- {time.strftime('%Y-%m-%d %H:%M:%S')} :: {context} ---\n")
            f.write(traceback.format_exc())
            f.write("\n")
    except Exception:
        # Logging must never be the thing that breaks the app.
        pass


"""Song Library (database)"""


def scan_library():
    """Makes a database out of the charts found in the given root folder.
    
    Replaces the current one if it's already there.
    
    """
    errors = []
    
    folder_count = hyutil.get_folder_count(appstate.usettings.chartfolders, on_scan_findprogress)
    on_scan_findcomplete(folder_count)
    
    # Map out the file locations first
    scanitems, folder_errors = hyutil.discover_charts(
        appstate.usettings.chartfolders,
        on_scan_addprogress
    )
    errors += folder_errors
    
    cxn = sqlite3.connect(hymisc.DBPATH)
    cur = cxn.cursor()
    
    # Initialize db
    cur.execute("DROP TABLE IF EXISTS charts")
    cur.execute(f"CREATE TABLE charts({hyutil.ScanItem.db_cols()})")
    
    # 'md5,name,artist,charter,path,folder'
    for i, scanitem in enumerate(scanitems):
        try:
            cur.execute(
                f"INSERT INTO charts VALUES (?, ?, ?, ?, ?, ?)",
                scanitem.db_values()
            )
        except Exception as e:
            errors.append(str(e))
        
    cxn.commit()
    cxn.close()
    
    return len(scanitems), errors
    

"""Settings"""


class HyAppUserSetting:
    """Streamlines editing runtime state and the underlying config file
    at the same time.
    
    """
    def __init__(self, astype='str'):
        self.astype = astype
        
    def __set_name__(self, owner, name):
        self.key = name
    
    def __get__(self, obj, objtype=None):
        match self.astype:
            case 'bool':
                return obj.cfg['hydra'].getboolean(self.key)
            case 'strlist':
                strs = obj.cfg['hydra'][self.key].strip().split('\n')
                return [s for s in strs if s != ""]
            case _:
                return obj.cfg['hydra'][self.key]
        
    def __set__(self, obj, value):
        match self.astype:
            case 'bool':
                obj.cfg['hydra'][self.key] = str(value)
            case 'strlist':
                obj.cfg['hydra'][self.key] = '\n'.join(value)
            case _:
                obj.cfg['hydra'][self.key] = str(value)
        
        obj.savecfg()


class HyAppUserSettings:
    """Interface for hydra's saveable settings.
    
    Automatically loads from config on init.
    
    HyApp reads setting values from here.
    
    Changing settings in HyApp changes variables in here and simultaneously
    updates the config.
    
    """
    version = HyAppUserSetting()
    chartfolders = HyAppUserSetting(astype='strlist')
    is_rescan = HyAppUserSetting(astype='bool')
    view_difficulty = HyAppUserSetting()
    view_prodrums = HyAppUserSetting(astype='bool')
    view_bass2x = HyAppUserSetting(astype='bool')
    depth_value = HyAppUserSetting()
    depth_mode = HyAppUserSetting()
    mslimit_enabled = HyAppUserSetting(astype='bool')
    mslimit_value = HyAppUserSetting()
    
    def __init__(self):
        # Load setting values from config
        self.cfg = configparser.ConfigParser()
        try:
            with open(hymisc.INIPATH, 'r') as cfgfile:
                self.cfg.read_file(cfgfile)
        except FileNotFoundError:
            pass
        
        # Bootstrap the cfg if it was essentially empty
        if 'hydra' not in self.cfg:
            self.cfg['hydra'] = {}
        
        loadedsettings = self.cfg['hydra']
        
        # Always override version number
        loadedsettings['version'] = str(hymisc.HYDRA_VERSION)
        
        # Fill in any missing values manually with defaults
        for key, default in [
            ('chartfolders', ""),
            ('is_rescan', 'False'),
            ('view_difficulty', 'Expert'),
            ('view_prodrums', 'True'),
            ('view_bass2x', 'True'),
            ('depth_value', '4'),
            ('depth_mode', 'scores'),
            ('mslimit_enabled', 'True'),
            ('mslimit_value', '10')
        ]:
            if key not in loadedsettings:
                loadedsettings[key] = default
            
        # Save in case anything was filled in
        self.savecfg()

    def add_chartfolder(self, folder):
        if folder not in self.chartfolders:
            self.chartfolders += [folder]
        
    def remove_chartfolder(self, folder):
        self.chartfolders = [f for f in self.chartfolders if f != folder]

    def savecfg(self):
        with open(hymisc.INIPATH, 'w') as cfgfile:
            self.cfg.write(cfgfile)
            
    def chartmode_key(self):
        """A combined string to match up values of (difficulty, prodrums, bass2x)"""
        prodrums = "Pro Drums" if appstate.usettings.view_prodrums else "Drums"
        bass = "2x Bass" if appstate.usettings.view_bass2x else "1x Bass"
        return f"{appstate.usettings.view_difficulty} {prodrums}, {bass}"

class HyAppRecordBook:
    """Code-accessible collection of records that have been generated.

    Backed by hystore.RecordStore (tables in hyapp.db), keyed by
    (hyhash, chartmode). Nothing is held in memory: records are written one
    row at a time and read back on demand.

    hyhash: Hash of the chart file. Not comparable to other apps' song hashes.

    """
    def __init__(self):
        """Open the record store, importing a legacy records.json if present."""
        self.store = hystore.RecordStore()
        self._migrate_legacy_json()

    def _migrate_legacy_json(self):
        """One-time import of records.json into the database.

        The original file is kept, renamed, rather than deleted.

        """
        if not hymisc.BOOKPATH.exists():
            return

        print("Migrating records.json into the database:")
        starttime = timer()
        try:
            songs, records = self.store.migrate_json(hymisc.BOOKPATH)
        except Exception as e:
            # A bad legacy file shouldn't stop the app from starting; the
            # records just stay unmigrated and the file is left alone.
            print(f"\tFailed, leaving records.json in place: {e}")
            return
        endtime = timer()
        print(f"\tMoved {records} records ({songs} songs) in {endtime - starttime:.6f} seconds.")

        backup = hymisc.BOOKPATH.with_suffix(".json.migrated")
        backup.unlink(missing_ok=True)
        hymisc.BOOKPATH.rename(backup)
        print(f"\tOriginal kept at {backup.name}.")
    
    def add_song(self, scanitem, tempomap):
        """Add an entry for the given scanned song.
        
        Essentially just adds a key (the md5) and tempomap
        for records to be added under.
        
        Duplicate md5s with different metadata are possible, but
        these metadata values are just for reference to put readable info next
        to md5s in the json.
        
        """
        self.store.add_song(scanitem, tempomap)

    def add_record(self, hyhash, chartmode, record):
        """Adds a record in the given place.

        Requires the song to have been added beforehand.

        """
        self.store.add_record(hyhash, chartmode, record)
 
class HyAppState:
    """Manages Hydra's state."""
    TABLE_ROWCOUNT = 15
    TABLE_COLCOUNT = 5
    
    def __init__(self):
        self.usettings = HyAppUserSettings()
        self.hydatabook = HyAppRecordBook()
        self.table_viewpage = 0
        self.librarysize = 0
        self.search = None
        
        self.current_path_selectable = None
        self.current_path_copytext = None
        
        self.selected_scanitem = None

        # Running library analysis, if any (see BatchJob).
        self.batch = None
    
        self.scanmodal_height_short = 190
        self.scanmodal_height_long = 320
        self.scan_foldercount = None
        
        self.input = {'C': False}
    
    def pagecount(self):
        return self.librarysize // self.TABLE_ROWCOUNT
            
            
    def get_record(self, hyhash, chartmode):
        return self.hydatabook.store.get_record(hyhash, chartmode)

    def get_record_summary(self, hyhash, chartmode):
        """(hyversion, bestpath) for the library table, or None.

        Avoids inflating a record just to label a row.

        """
        return self.hydatabook.store.get_summary(hyhash, chartmode)
            
    def get_selected_record(self):
        return self.get_record(self.selected_scanitem.md5, self.usettings.chartmode_key())
        
"""UI callbacks"""


def on_viewport_resize():
    # Loading modal sizes: Heavily inset window
    dpg.configure_item(
        "songdetails_progresspanel", pos=(400, 200),
        width=dpg.get_viewport_width() - 22 - 800
    )
    
    # Song details sizes: Slightly inset window
    dpg.configure_item(
        "songdetails", pos=(40, 40),
        width=dpg.get_viewport_width() - 22 - 80,
        height=dpg.get_viewport_height() - 22 - 80
    )
    
    # New UI framework soon...?
    # dpg.configure_item(
        # "enginelabel", pos=(dpg.get_viewport_width() - 260, dpg.get_viewport_height() - 74)
    # )

def set_scanmodal_height(long=False):
    h = appstate.scanmodal_height_long if long else appstate.scanmodal_height_short
    dpg.configure_item(
        "scanprogress", pos=(400, 200),
        width=dpg.get_viewport_width() - 22 - 800,
        height=h
    )
                        
def on_add_chartfolder():
    dpg.show_item("select_chartfolder")
        
def on_chartfolder_added(sender, app_data):
    appstate.usettings.add_chartfolder(app_data['file_path_name'])
    appstate.usettings.is_rescan = False
    dpg.set_value("songfolder_parent", True)
    refresh_chartfolder()
    
def on_chartfolder_removed(sender, app_data, user_data):
    appstate.usettings.remove_chartfolder(user_data)
    appstate.usettings.is_rescan = False
    refresh_chartfolder()

def on_viewdifficulty(sender, app_data):
    appstate.usettings.view_difficulty = app_data
    refresh_viewdifficulty()
    refresh_tableview()

def on_viewprodrums(sender, app_data):
    appstate.usettings.view_prodrums = app_data
    refresh_viewprodrums()
    refresh_tableview()
    
def on_viewbass2x(sender, app_data):
    appstate.usettings.view_bass2x = app_data
    refresh_viewbass2x()
    refresh_tableview()

def on_depth_value(sender, app_data):
    appstate.usettings.depth_value = app_data

def on_depth_mode(sender, app_data):
    appstate.usettings.depth_mode = app_data

def on_mslimit_check(sender, app_data, user_data):
    appstate.usettings.mslimit_enabled = app_data
    refresh_mslimit_check()
    
def on_mslimit_value(sender, app_data, user_data):
    appstate.usettings.mslimit_value = app_data

def refresh_mslimit_check():
    dpg.configure_item("inp_mslimit", enabled=appstate.usettings.mslimit_enabled)
    dpg.bind_item_theme("mslimit_mstext", "default_theme" if appstate.usettings.mslimit_enabled else "disabled_text")

def init_mslimit_state():
    dpg.set_value("mslimit_check", appstate.usettings.mslimit_enabled)
    dpg.set_value("inp_mslimit", int(appstate.usettings.mslimit_value))
    
def on_scan():
    reset_scan_modal()
    dpg.show_item("scanprogress")
    set_scanmodal_height(long=False)
    count, errors = scan_library()
    set_scanmodal_height(long=len(errors) > 0)
    dpg.configure_item("scanprogress_bar", overlay=f"{appstate.scan_foldercount}/{appstate.scan_foldercount}")
    dpg.show_item("scanprogress_done")
    if errors:
        dpg.show_item("scanprogress_failedsongslabel")
        dpg.show_item("scanprogress_failedcontainer")
    dpg.show_item("scanprogress_dismiss")
    
    dpg.delete_item("scanprogress_failedcontainer", children_only=True)
    for errormsg in errors:
        dpg.add_text(errormsg, parent="scanprogress_failedcontainer")
    
class BatchJob:
    """One chart per rendered frame, so the UI keeps painting and Cancel works.

    dearpygui runs callbacks inside the render, so looping over the whole
    library inside the button callback would freeze the window until it
    finished. Instead the main loop advances this a step at a time.

    """
    def __init__(self, scanitems, chartmode, redo):
        self.items = scanitems
        self.chartmode = chartmode
        self.redo = redo

        self.index = 0
        self.analyzed = 0
        self.skipped = 0
        self.failed = 0
        self.failures = []
        self.cancelled = False

    def is_done(self):
        return self.cancelled or self.index >= len(self.items)

    def step(self):
        """Handle the next chart. Returns False once there's nothing left."""
        if self.is_done():
            return False

        scanitem = self.items[self.index]
        self.index += 1

        if not self.redo and appstate.hydatabook.store.has_record(scanitem.md5, self.chartmode):
            self.skipped += 1
            return True

        try:
            record, tempomap = hyutil.analyze_chart_file(
                scanitem.notespath,
                appstate.usettings.view_difficulty,
                appstate.usettings.view_prodrums,
                appstate.usettings.view_bass2x,
                appstate.usettings.depth_mode,
                int(appstate.usettings.depth_value),
                int(appstate.usettings.mslimit_value) if appstate.usettings.mslimit_enabled else None,
                export_tempomap=True,
            )
        except Exception as e:
            self.failed += 1
            self.failures.append(f"{scanitem.artist} - {scanitem.title}: {e!r}")
            return True

        appstate.hydatabook.add_song(scanitem, tempomap)
        appstate.hydatabook.add_record(scanitem.md5, self.chartmode, record)
        self.analyzed += 1
        return True


def library_scanitems(search=None):
    """Charts in the library as ScanItems, matching the same filter the
    library table uses so "Analyze search" covers exactly what's on screen."""
    cxn = sqlite3.connect(hymisc.DBPATH)
    try:
        if search:
            param = f"%{search}%"
            rows = cxn.execute(
                "SELECT * FROM charts WHERE name LIKE ? OR artist LIKE ? ORDER BY name",
                (param, param),
            ).fetchall()
        else:
            rows = cxn.execute("SELECT * FROM charts ORDER BY name").fetchall()
    except sqlite3.OperationalError:
        rows = []
    cxn.close()

    scanitems = []
    for row in rows:
        try:
            scanitems.append(hyutil.ScanItem.from_db(row))
        except Exception:
            # A malformed row just isn't analyzable; skip it rather than
            # taking the whole run down.
            pass
    return scanitems


def refresh_batchbutton(matchcount=None):
    """Label the batch button with what it will actually act on."""
    if appstate.search:
        count = "" if matchcount is None else f" ({matchcount})"
        dpg.configure_item("batchbutton", label=f"Analyze search{count}")
    else:
        dpg.configure_item("batchbutton", label="Analyze library")


def on_batch():
    """Analyze the current search, or the whole library if nothing is typed."""
    if appstate.batch is not None:
        return

    scanitems = library_scanitems(appstate.search)

    reset_batch_modal()
    dpg.show_item("batchprogress")

    if not scanitems:
        # Don't leave the click doing nothing at all.
        dpg.set_value("batch_title", "Nothing to analyze.")
        dpg.set_value(
            "batch_counts",
            "No charts match your search." if appstate.search
            else "Scan a song folder first."
        )
        dpg.hide_item("batch_cancelbutton")
        dpg.show_item("batch_dismiss")
        return

    scope = f'search "{appstate.search}"' if appstate.search else "library"
    dpg.set_value("batch_title", f"Analyzing {scope} ({len(scanitems)} charts)...")

    appstate.batch = BatchJob(
        scanitems,
        appstate.usettings.chartmode_key(),
        dpg.get_value("batch_redo"),
    )
    refresh_batch_progress()


def refresh_batch_progress():
    job = appstate.batch
    total = len(job.items)

    dpg.set_value("batch_bar", job.index / total if total else 1.0)
    dpg.configure_item("batch_bar", overlay=f"{job.index}/{total}")
    dpg.set_value(
        "batch_counts",
        f"{job.analyzed} analyzed, {job.skipped} already stored, {job.failed} failed."
    )

    if job.index < total:
        nextitem = job.items[job.index]
        dpg.set_value("batch_current", f"{nextitem.artist} - {nextitem.title}"[:64])


def batch_step():
    """Advance the running batch by one chart. Called once per frame."""
    job = appstate.batch
    if job is None:
        return

    if job.step():
        refresh_batch_progress()
        return

    # Finished or cancelled.
    dpg.set_value("batch_current", "")
    dpg.set_value("batch_title", "Cancelled." if job.cancelled else "Finished.")
    dpg.set_value("batch_bar", job.index / len(job.items) if job.items else 1.0)
    dpg.show_item("batch_done")
    dpg.hide_item("batch_cancelbutton")
    dpg.show_item("batch_dismiss")

    if job.failures:
        dpg.configure_item("batchprogress", height=380)
        dpg.show_item("batch_failedlabel")
        dpg.show_item("batch_failedcontainer")
        dpg.delete_item("batch_failedcontainer", children_only=True)
        for msg in job.failures:
            dpg.add_text(msg, parent="batch_failedcontainer")

    appstate.batch = None


def on_batch_cancel():
    if appstate.batch is not None:
        appstate.batch.cancelled = True


def on_batch_dismiss():
    dpg.hide_item("batchprogress")
    cache_librarysize()
    refresh_tableview()


def reset_batch_modal():
    dpg.configure_item("batchprogress", height=200)
    dpg.set_value("batch_title", "Analyzing library...")
    dpg.set_value("batch_current", "")
    dpg.set_value("batch_counts", "")
    dpg.set_value("batch_bar", 0.0)
    dpg.hide_item("batch_done")
    dpg.hide_item("batch_failedlabel")
    dpg.hide_item("batch_failedcontainer")
    dpg.hide_item("batch_dismiss")
    dpg.show_item("batch_cancelbutton")


def on_scan_dismiss():
    dpg.hide_item("scanprogress")
    
    appstate.usettings.is_rescan = True
    
    cache_librarysize()
    
    refresh_librarytitle()
    dpg.set_value("songfolder_parent", False)
    refresh_chartfolder()
    appstate.table_viewpage = 0
    refresh_tableview()

def on_search_text(sender, app_data):
    appstate.search = None if app_data == "" else app_data
    appstate.table_viewpage = 0
    refresh_tableview()

def on_library_rowclick(sender, app_data, user_data):
    # Cancel out selected states (we just want button functionality)
    for s in all_table_row_selectables():
        dpg.set_value(s, False)
        
    # Do nothing for empty rows
    if user_data is None:
        return
        
    # Update selected row and update UI
    appstate.selected_scanitem = user_data
    view_showsongdetails()

def on_path_selected(sender, app_data, path):
    """
    
    user_data: HyRecordPath.
    
    """
    
    # "Unlock" the previous selectable
    if appstate.current_path_selectable:
        dpg.set_value(appstate.current_path_selectable, False)
        dpg.configure_item(appstate.current_path_selectable, enabled=True)
    
    appstate.current_path_selectable = sender
    appstate.current_path_copytext = path.pathstring_verbose()
    
    # "Lock" the new selected path: only deselectable by selecting another.
    dpg.configure_item(sender, enabled=False)
    
    # Rebuild path details panel
    dpg.delete_item("songdetails_pathdetails", children_only=True)
    
    dpg.add_spacer(height=8, parent="songdetails_pathdetails")
    dpg.add_button(label="Copy path string", width=120, height=30, indent=6, parent="songdetails_pathdetails", callback=on_copy_current_pathstring)
    dpg.bind_item_font(dpg.last_item(), "MainFont")
    dpg.add_spacer(height=2, parent="songdetails_pathdetails")
    
    with dpg.tree_node(label="Multiplier squeezes", parent="songdetails_pathdetails", default_open=True):
        dpg.bind_item_font(dpg.last_item(), "MainFont24")
        if path.multsqueezes:
            for msq in path.multsqueezes:
                with dpg.tree_node(label=f"{msq.notationstr()}   (+{msq.points} pts):   {msq.chord.rowstr()}", default_open=False):
                    dpg.bind_item_font(dpg.last_item(), "MonoFont")
                    dpg.add_text(msq.howto)
                    dpg.bind_item_font(dpg.last_item(), "MainFont24")
                    dpg.add_spacer(height=8)
        else:
            dpg.add_text("None.")
            dpg.bind_item_font(dpg.last_item(), "MonoFont")
        
    with dpg.tree_node(label="Activations", parent="songdetails_pathdetails", default_open=True):
        dpg.bind_item_font(dpg.last_item(), "MainFont24")
        if path.has_activations():
            for act in path.all_activations():
                act_header = f"{act.notationstr():6}({act.sp_meter} SP)\t{act.timecode.measurestr(): >9}"
                if (act_ms := act.difficulty()) is not None:
                    act_header += f"\t{act_ms:7.1f}ms"
                with dpg.tree_node(label=act_header, default_open=False, indent=4):
                    dpg.bind_item_font(dpg.last_item(), "MonoFont")
                    if act.is_difficult():
                        dpg.bind_item_theme(dpg.last_item(), "warning_theme")
                    with dpg.group(indent=12):
                        dpg.bind_item_theme(dpg.last_item(), "default_theme")
                        
                        # E
                        if act.is_e_critical():
                            cftext = f"Calibration fill: {act.e_offset + 0.0:.1f}ms"
                            cftext += " (required)" if act.is_E0() else " (optional)"
                            dpg.add_text(cftext)
                        
                        # Frontend
                        dpg.add_text(f"Frontend: {act.chord.rowstr() if act.chord is not None else "None"}")
                        
                        # SP squeezes
                        for sq in act.sqinouts:
                            dpg.add_text(sq.description)
                            if sq.is_difficult:
                                dpg.bind_item_theme(dpg.last_item(), "warning_theme")
                        
                        # Backends
                        # act.backends runs out to the full squeeze window for
                        # scoring purposes; only show the ones near the deact,
                        # plus whatever note is actually being squeezed out of
                        # SP, however far past the display window it lands.
                        shown_backends = act.display_backends()
                        if shown_backends:
                            dpg.add_text("Backends:")
                            
                            with dpg.table(width=-10, borders_outerH=True, borders_outerV=True):
                                dpg.add_table_column(label="Timing", width_fixed=True, init_width_or_weight=80)
                                dpg.add_table_column(label="Chord", width_fixed=True, init_width_or_weight=80)
                                dpg.add_table_column(label="Points", width_fixed=True, init_width_or_weight=80)
                                dpg.add_table_column(label="Rating", width_stretch=True)
                                    
                                for bsq in shown_backends:
                                    is_squeezed_out = act.is_sqout_backend(bsq)
                                    with dpg.table_row():
                                        dpg.add_text(f"{bsq.offset_ms:6.1f}")
                                        dpg.add_text(f"{bsq.chord.notationstr()}")
                                        # A squeezed-out note only scores its reduced
                                        # sqout value, not the full SP value.
                                        dpg.add_text(f"{bsq.sqout_points if is_squeezed_out else bsq.points:4,d}")
                                        sqtext = bsq.summarystr()
                                        if is_squeezed_out:
                                            sqtext += f" <-- squeezed out (-{bsq.points - bsq.sqout_points:,d})"
                                        dpg.add_text(sqtext)
                                        if is_squeezed_out:
                                            dpg.bind_item_theme(dpg.last_item(), "warning_theme")
                        else:
                            dpg.add_text("Backends: None.")
                        dpg.add_spacer(height=16)
        else:
            dpg.add_text("None.")
            dpg.bind_item_font(dpg.last_item(), "MonoFont")
            
        # Regardless of what activations were displayed, leftover SP is 
        # a possibility.
        dpg.add_text(f"Leftover SP: {path.leftover_sp}.")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        
        if path.skipped_accents > 0:
            dpg.add_text(f"This path has {path.skipped_accents} skipped (unhittable) accent{"" if path.skipped_accents == 1 else "s"}!")
            dpg.bind_item_font(dpg.last_item(), "MonoFont")
            dpg.bind_item_theme(dpg.last_item(), "warning_theme")
        if path.skipped_ghosts > 0:
            dpg.add_text(f"This path has {path.skipped_ghosts} skipped (unhittable) ghost{"" if path.skipped_ghosts == 1 else "s"}!")
            dpg.bind_item_font(dpg.last_item(), "MonoFont")
            dpg.bind_item_theme(dpg.last_item(), "warning_theme")
    
    with dpg.tree_node(label="Score breakdown", parent="songdetails_pathdetails", default_open=True):
        dpg.bind_item_font(dpg.last_item(), "MainFont24")
        dpg.add_text(f"Avg. Multiplier:      {(str(path.avg_mult()) + "000")[:5]}x")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"\nNotes:            {path.score_base: >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"Combo Bonus:      {path.score_combo: >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"Star Power:       {path.score_sp: >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"Solo Bonus:       {path.score_solo: >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"Accent Notes:     {path.score_accents: >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"Ghost Notes:      {path.score_ghosts: >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")
        dpg.add_text(f"\nTotal Score:      {path.totalscore(): >10,}")
        dpg.bind_item_font(dpg.last_item(), "MonoFont")

def on_copy_current_pathstring(sender, app_data):
    # Coming from the path-viewing context
    copy_current_pathstring()

def copy_current_pathstring():    
    pyperclip.copy(appstate.current_path_copytext)

def on_key_c_press(sender, app_data):
    if not appstate.input['C']:
        appstate.input['C'] = True
        if dpg.is_key_down(dpg.mvKey_LControl):
            # Path-viewing context?
            # to do: Better state than is visible?
            if dpg.is_item_visible("songdetails"):
                copy_current_pathstring()

def on_key_c_release(sender, app_data):
    appstate.input['C'] = False

def on_pageleft(sender, app_data):
    appstate.table_viewpage = max(appstate.table_viewpage - 1, 0)
    refresh_tableview()
   
def on_pageright(sender, app_data):
    appstate.table_viewpage = min(appstate.table_viewpage + 1, appstate.pagecount())
    refresh_tableview()

def on_run_chart(sender, app_data, user_data):
    # Show modal
    dpg.configure_item("songdetails", no_title_bar=True, no_close=True)
    dpg.hide_item("songdetails_upperpanel")
    dpg.hide_item("songdetails_lowerpanel")
    dpg.show_item("songdetails_progresspanel")
    reset_analyze_modal()
    # run chart
    try:
        chartfile = appstate.selected_scanitem.notespath
        record, tempomap = hyutil.analyze_chart_file(
            chartfile,
            appstate.usettings.view_difficulty, appstate.usettings.view_prodrums, appstate.usettings.view_bass2x,
            appstate.usettings.depth_mode, int(appstate.usettings.depth_value),
            int(appstate.usettings.mslimit_value) if appstate.usettings.mslimit_enabled else None,
            on_analyze_parsecomplete, on_analyze_pathsprogress,
            export_tempomap=True
        )
    except Exception as e:
        dpg.configure_item("songdetails_progresspanel", height=180)
        dpg.set_value("analyze_errorcontent", repr(e))
        dpg.show_item("analyze_errorlabel")
        dpg.show_item("analyze_errorcontent")
        dpg.show_item("analyze_dismissbutton")
        return
    
    dpg.configure_item("analyze_opt_bar", overlay="")
    dpg.set_value("analyze_opt_bar", 1)
    dpg.show_item("analyze_opt_done")
    
    appstate.hydatabook.add_song(appstate.selected_scanitem, tempomap)
    appstate.hydatabook.add_record(appstate.selected_scanitem.md5, appstate.usettings.chartmode_key(), record)

    time.sleep(0.5)
    on_analyze_dismiss(None, None)


def on_analyze_dismiss(sender, app_data):
    # update record displays
    refresh_tableview()
    refresh_songdetails()
    
    # hide modal
    dpg.show_item("songdetails_upperpanel")
    dpg.show_item("songdetails_lowerpanel")
    dpg.hide_item("songdetails_progresspanel")
    dpg.configure_item("songdetails", no_title_bar=False, no_close=False)


"""Progress callbacks (reactions to processing rather than UI interactions)"""


def on_scan_findprogress(count):
    """As charts are discovered during a scan."""
    indicator = '.' * (count//100 % 5 + 1)
    dpg.set_value("scanprogress_discovering", f"Discovering charts{indicator}")
    
def on_scan_findcomplete(count):
    """When charts have been fully discovered during a scan."""
    dpg.set_value("scanprogress_discovering", f"Discovering folders...")
    dpg.set_value("scanprogress_chartsfound", f"{count} folders found.")
    dpg.show_item("scanprogress_chartsfound")
    dpg.show_item("scanprogress_bartext")
    dpg.show_item("scanprogress_bar")
    dpg.set_value("scanprogress_bar", 0.0)
    dpg.configure_item("scanprogress_bar", overlay=f"0/{count}")
    appstate.scan_foldercount = count

def on_scan_addprogress(count):
    """As charts are added to library during a scan."""
    dpg.set_value("scanprogress_bar", count/appstate.scan_foldercount)
    dpg.configure_item("scanprogress_bar", overlay=f"{count}/{appstate.scan_foldercount}")

    
"""UI view controls"""


def view_main():
    """Main view with a small upper pane for settings and a large lower pane
    to view the song library.
    
    """
    dpg.hide_item("preload")
    dpg.show_item("mainwindowcontent")
    
    dpg.configure_item("scanprogress", width=-1, height=-1)
    
    cache_librarysize()
    
    refresh_chartfolder()
    refresh_viewdifficulty()
    refresh_viewprodrums()
    refresh_viewbass2x()
    refresh_librarytitle()
    refresh_tableview()
    
    refresh_depthvalue()
    refresh_depthmode()
    init_mslimit_state()
    refresh_mslimit_check()
    
    
def view_showsongdetails():
    """Shows the song details window and necessarily updates the info.
    
    Closing the song details window is just via close button.
    
    """
    dpg.show_item("songdetails")
    refresh_songdetails()
    
    
"""UI population"""


def reset_scan_modal():
    dpg.hide_item("scanprogress_chartsfound")
    dpg.hide_item("scanprogress_bartext")
    dpg.hide_item("scanprogress_bar")
    dpg.hide_item("scanprogress_done")
    dpg.hide_item("scanprogress_failedsongslabel")
    dpg.hide_item("scanprogress_failedcontainer")
    dpg.hide_item("scanprogress_dismiss")
    
def reset_analyze_modal():
    dpg.configure_item("songdetails_progresspanel", height=125)
    dpg.hide_item("analyze_opt_label")
    dpg.hide_item("analyze_opt_bar")
    dpg.hide_item("analyze_opt_done")
    dpg.hide_item("analyze_errorlabel")
    dpg.hide_item("analyze_errorcontent")
    dpg.hide_item("analyze_dismissbutton")
    dpg.set_value("analyze_opt_bar", 0)
    
def on_analyze_parsecomplete():
    dpg.show_item("analyze_opt_label")
    dpg.show_item("analyze_opt_bar")
    
def on_analyze_pathsprogress(timecode, progressf):
    s = timecode.measurestr(fixed_width=True) if timecode else ""
    dpg.configure_item("analyze_opt_bar", overlay=s)
    dpg.set_value("analyze_opt_bar", progressf)

def refresh_chartfolder():
    dpg.delete_item("songfolder_contents", children_only=True)
    if appstate.usettings.chartfolders:
        dpg.configure_item("songfolder_contents", height=4+28*len(appstate.usettings.chartfolders))
        dpg.add_spacer(parent="songfolder_contents", height=0)
        for songfolder in appstate.usettings.chartfolders:
            with dpg.group(parent="songfolder_contents", horizontal=True):
                dpg.add_button(label="X", width=20, indent=6, callback=on_chartfolder_removed, user_data=songfolder)
                dpg.bind_item_theme(dpg.last_item(), "delete_theme")
                dpg.add_text(songfolder)
        
        labeltxt = "Refresh scan" if appstate.usettings.is_rescan else "Scan charts"
        dpg.configure_item("scanbutton", enabled=True, label=labeltxt)
        
    else:
        dpg.set_value("songfolder_parent", True)
        dpg.configure_item("songfolder_contents", height=28)
        dpg.add_text("(None.)", parent="songfolder_contents")
        dpg.configure_item("scanbutton", enabled=False, label="Scan charts")

def refresh_viewdifficulty():
    dpg.set_value("view_difficulty_combo", appstate.usettings.view_difficulty)
def refresh_viewprodrums():
    dpg.set_value("view_prodrums_check", appstate.usettings.view_prodrums)
def refresh_viewbass2x():
    dpg.set_value("view_bass2x_check", appstate.usettings.view_bass2x)

def refresh_depthvalue():
    dpg.set_value("inp_depthvalue", int(appstate.usettings.depth_value))
def refresh_depthmode():
    dpg.set_value("inp_depthmode", appstate.usettings.depth_mode)

def reconcile_library_visibility():
    """Make the empty/populated panels agree with the library size.

    Kept separate and always run, so a failure while drawing rows can't
    strand the "No songs scanned" message over a populated library.

    """
    if appstate.librarysize > 0:
        dpg.hide_item("libraryempty")
        dpg.show_item("librarypopulated")
    else:
        dpg.hide_item("librarypopulated")
        dpg.show_item("libraryempty")


def refresh_tableview():
    """Display a particular page of the song library."""
    try:
        _draw_tableview()
    except Exception:
        log_error("refresh_tableview")
    finally:
        reconcile_library_visibility()


def _draw_tableview():
    """Fill in the visible page of the library table.

    Other info like the size of the library is already known and doesn't need
    the db to be accessed.

    """
    cxn = sqlite3.connect(hymisc.DBPATH)
    cur = cxn.cursor()

    try:
        if appstate.search:
            searchparam = f"%{appstate.search}%"
            where = "WHERE name LIKE ? OR artist LIKE ?"
            fullcount = cur.execute(f"SELECT COUNT(*) FROM charts {where}", (searchparam, searchparam)).fetchone()[0]
            page_db_rows = cur.execute(f"SELECT * FROM charts {where} ORDER BY name LIMIT {appstate.TABLE_ROWCOUNT} OFFSET {appstate.table_viewpage * appstate.TABLE_ROWCOUNT}", (searchparam,searchparam)).fetchall()
        else:
            fullcount = appstate.librarysize
            page_db_rows = cur.execute(f"SELECT * FROM charts ORDER BY name LIMIT {appstate.TABLE_ROWCOUNT} OFFSET {appstate.table_viewpage * appstate.TABLE_ROWCOUNT}").fetchall()
        cxn.close()
    except sqlite3.OperationalError:
        page_db_rows = []
        cxn.close()
    
    scan_items = []
    for row in page_db_rows:
        try:
            scan_items.append(hyutil.ScanItem.from_db(row))
        except Exception:
            # A row we can't read is just an unshowable song, not a reason to
            # abandon the whole page. (This used to name a nonexistent
            # exception class, so any failure here raised AttributeError and
            # took the rest of the refresh down with it.)
            pass

    for r in range(appstate.TABLE_ROWCOUNT):
        try:
            scan_item = scan_items[r]
            dpg.set_value(f"table[{r}, {0}]", scan_item.title or "")
            dpg.set_value(f"table[{r}, {1}]", scan_item.artist or "")
            dpg.set_value(f"table[{r}, {2}]", scan_item.charter or "")
            dpg.set_value(f"table[{r}, {3}]", scan_item.rootfolder or "")

            summary = appstate.get_record_summary(scan_item.md5, appstate.usettings.chartmode_key())
            if summary:
                hyversion, bestpath = summary
                if hyversion == hymisc.HYDRA_VERSION:
                    dpg.configure_item(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", label=bestpath, user_data=scan_item)
                    dpg.bind_item_theme(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", "bestpath_theme")
                else:
                    dpg.configure_item(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", label="(Update...)", user_data=scan_item)
                    dpg.bind_item_theme(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", "warning_theme")
            else:
                dpg.configure_item(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", label="(New...)", user_data=scan_item)
                dpg.bind_item_theme(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", "newsong_theme")
        except IndexError:
            # The page data doesn't use all rows and this is a trailing empty row
            for i in range(appstate.TABLE_COLCOUNT - 1):
                dpg.set_value(f"table[{r}, {i}]", "-----")
            dpg.configure_item(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", label="-----", user_data=None)
            dpg.bind_item_theme(f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", "newsong_theme")
        except Exception:
            # One unrenderable row shouldn't strand the whole library view.
            log_error(f"library row {r}")

    lastpage = fullcount // appstate.TABLE_ROWCOUNT
    dpg.set_value("librarypagelabel", f"{appstate.table_viewpage + 1}/{lastpage + 1}")

    dpg.configure_item("pageleftbutton", enabled=appstate.table_viewpage > 0)
    dpg.configure_item("pagerightbutton", enabled=appstate.table_viewpage < lastpage)

    refresh_batchbutton(fullcount)
        
    

def refresh_librarytitle():
    chartcount = appstate.librarysize    
    countstr = "(1 chart)" if chartcount == 1 else f"({chartcount} charts)"
    dpg.configure_item("librarytitle", label=f"Library {countstr}")
    
def refresh_songdetails():
    dpg.set_value("songdetails_songtitle", appstate.selected_scanitem.title)
    dpg.set_value("songdetails_songartist", appstate.selected_scanitem.artist)
    dpg.set_value("songdetails_songcharter", appstate.selected_scanitem.charter)
    dpg.set_value("songdetails_songhash", appstate.selected_scanitem.md5)
    
    dpg.configure_item("songdetails", label=f"Song Details\t\t\t\t{appstate.usettings.chartmode_key()}")
        
    viewed_record = appstate.get_selected_record()
    
    # Check the file that was scanned in the past
    if os.path.isfile(appstate.selected_scanitem.notespath):
        dpg.configure_item("runbutton", enabled=True, label="Analyze paths!")
    else:
        dpg.configure_item("runbutton", enabled=False, label="Song file not found.\nTry scanning again.")
    
    # Lower Panel - or quit if this row has no record yet
    
    if not viewed_record:
        dpg.hide_item("songdetails_pathpanel")
        dpg.hide_item("songdetails_pathdetails")
        dpg.hide_item("songdetails_pathdivider")
        dpg.show_item("songdetails_nopathsyet")
        dpg.hide_item("songdetails_plsupdaterecord")
        return
    
    if not viewed_record.is_version_compatible():
        dpg.hide_item("songdetails_pathpanel")
        dpg.hide_item("songdetails_pathdetails")
        dpg.hide_item("songdetails_pathdivider")
        dpg.hide_item("songdetails_nopathsyet")
        dpg.show_item("songdetails_plsupdaterecord")
        return
    
    dpg.show_item("songdetails_pathpanel")
    dpg.show_item("songdetails_pathdetails")
    dpg.show_item("songdetails_pathdivider")
    dpg.hide_item("songdetails_nopathsyet")
    dpg.hide_item("songdetails_plsupdaterecord")
    
    # Rebuild path list
    
    dpg.delete_item("songdetails_pathpanel", children_only=True)
    appstate.current_path_selectable = None
    appstate.current_path_copytext = None
    
    current_score = None 
    current_score_tier = 0
    current_treenode = None
    
    
    is_first_path = True
    for p in viewed_record.all_paths():
        if current_score != p.totalscore():
            current_score_tier += 1
            if current_score_tier == 1:
                dpg.add_separator(parent="songdetails_pathpanel", label="Optimal Path")
            elif current_score_tier == 2:
                ep_suffix = "" if viewed_record.ms_limit is None else f" (Limit timings: {viewed_record.ms_limit} ms)"
                dpg.add_separator(parent="songdetails_pathpanel", label=f"More Paths{ep_suffix}")
            current_score = p.totalscore()
            current_treenode = dpg.add_tree_node(label=f"{current_score:,}", parent="songdetails_pathpanel", default_open=True)
            dpg.bind_item_font(current_treenode, "MonoFont")
        
        with dpg.table(parent=current_treenode, header_row=False):
            dpg.add_table_column()
            dpg.add_table_column(width_fixed=True, init_width_or_weight=130)
            with dpg.table_row():
                pathselectable = dpg.add_selectable(label=p.pathstring(), callback=on_path_selected, user_data=p, default_value=is_first_path, indent=16, span_columns=True)
                
                if (diff := p.difficulty()) is not None:
                    dpg.add_selectable(label=f"{diff:9.1f} ms", callback=on_path_selected, user_data=p, default_value=False)
                    if p.is_difficult():
                        dpg.bind_item_theme(dpg.last_item(), "warning_theme")
        if is_first_path:
            autoselect = pathselectable
        is_first_path = False
    
    # Auto select the first path
    on_path_selected(autoselect, True, viewed_record.best_path())

    
""" Utility"""

def cache_librarysize():
    cxn = sqlite3.connect(hymisc.DBPATH)
    cur = cxn.cursor()
    try:
        cur.execute("SELECT COUNT(*) FROM charts")
        appstate.librarysize = cur.fetchone()[0]
    except sqlite3.OperationalError:
        appstate.librarysize = 0
    cxn.close()

def all_table_row_selectables():
    return [f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]" for r in range(appstate.TABLE_ROWCOUNT)]

""" Main """

def build_main_ui():
    def load_icon(path, tag):
        w, h, c, d = dpg.load_image(str(path))
        dpg.add_static_texture(width=w, height=h, default_value=d, tag=tag)
    
    # Textures
    with dpg.texture_registry():
        load_icon(hymisc.ICOPATH_RECORD, "icon_record")
        load_icon(hymisc.ICOPATH_STAR, "icon_star")
        load_icon(hymisc.ICOPATH_PENCIL, "icon_pencil")
        load_icon(hymisc.ICOPATH_HASH, "icon_hash")
        
    # Fonts
    with dpg.font_registry():
        with dpg.font(hymisc.FONTPATH_ANTQ, 18, tag="MainFont"):
            dpg.add_font_range_hint(dpg.mvFontRangeHint_Japanese)
        with dpg.font(hymisc.FONTPATH_ANTQ, 24, tag="MainFont24"):
            dpg.add_font_range_hint(dpg.mvFontRangeHint_Japanese)
        dpg.add_font(hymisc.FONTPATH_MONO, 18, tag="MonoFont")
        
    # Default font
    dpg.bind_font("MainFont")
    
    # Theme
    with dpg.theme(tag="default_theme") as standard_theme:
        with dpg.theme_component(dpg.mvButton, enabled_state=True):
            dpg.add_theme_color(dpg.mvThemeCol_Button, (0, 150, 150))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (0, 180, 180))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (0, 200, 200))
        
        with dpg.theme_component(dpg.mvButton, enabled_state=False):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (200, 200, 200))
            dpg.add_theme_color(dpg.mvThemeCol_Button, (100, 100, 100))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (100, 100, 100))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (100, 100, 100))
        
        with dpg.theme_component(dpg.mvInputInt, enabled_state=False):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (50, 50, 50))
            dpg.add_theme_color(dpg.mvThemeCol_FrameBg, (40, 40, 40))
            dpg.add_theme_color(dpg.mvThemeCol_Button, (40, 40, 40))            
        
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_TitleBgActive, (100, 0, 0))
            dpg.add_theme_color(dpg.mvThemeCol_CheckMark, (0, 180, 180))
            dpg.add_theme_color(dpg.mvThemeCol_FrameBgHovered, (0, 100, 100))
            dpg.add_theme_color(dpg.mvThemeCol_FrameBgActive, (0, 150, 150))
            dpg.add_theme_color(dpg.mvThemeCol_HeaderHovered, (0, 100, 100))
            dpg.add_theme_color(dpg.mvThemeCol_HeaderActive, (0, 180, 180))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (0, 180, 180))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (0, 200, 200))
            dpg.add_theme_color(dpg.mvThemeCol_PlotHistogram, (0, 200, 200))
            
            dpg.add_theme_color(dpg.mvThemeCol_Text, (250, 250, 250))
    
    with dpg.theme(tag="bestpath_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (250,210,0))
            
    with dpg.theme(tag="warning_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (255,127,0))
    
    with dpg.theme(tag="newsong_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (100,100,100))
            
    with dpg.theme(tag="songfolder_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_ChildBg, (50, 50, 50))
            
    with dpg.theme(tag="delete_theme"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Button, (180, 5, 5))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (250, 50, 50))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (250, 100, 100))

    with dpg.theme(tag="disabled_text"):
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, (50,50,50))

    dpg.bind_theme(standard_theme)
    
    # Add main content to main window
    with dpg.group(tag="mainwindowcontent", parent="mainwindow", show=False):
        dpg.add_separator(label="Settings")
        with dpg.tree_node(tag="songfolder_parent", label="Song folders:", default_open=False):
            with dpg.child_window(tag="songfolder_contents", height=50, border=False):
                dpg.bind_item_theme("songfolder_contents", "songfolder_theme")
                dpg.add_text("Dummy song folder slot")
        dpg.add_spacer(height=2)
        with dpg.group(horizontal=True):
            dpg.add_button(label="Add folder...", callback=on_add_chartfolder)
            dpg.add_button(tag="scanbutton", label="Scan charts", callback=on_scan)
            dpg.add_button(tag="batchbutton", label="Analyze library", callback=on_batch)
            dpg.add_checkbox(tag="batch_redo", label="redo existing")
        dpg.add_spacer(height=2)
        dpg.add_separator(label="Library", tag="librarytitle")
        
        with dpg.group(tag="librarypopulated"):
            dpg.add_spacer(height=2)
            with dpg.group(horizontal=True, tag="libraryviewcontrols"):
                dpg.add_text("View:")
                dpg.add_combo(("Expert",), tag="view_difficulty_combo", width=120, callback=on_viewdifficulty)
                #dpg.add_combo(("Expert", "Hard", "Medium", "Easy"), tag="view_difficulty_combo", width=120, callback=on_viewdifficulty)
                dpg.add_checkbox(label="Pro Drums", tag="view_prodrums_check", callback=on_viewprodrums)
                dpg.add_checkbox(label="2x Bass", tag="view_bass2x_check", callback=on_viewbass2x)
            
            dpg.add_spacer(height=2)
            with dpg.group(horizontal=True, tag="librarysearch"):
                dpg.add_text("Search:")
                dpg.add_input_text(callback=on_search_text, width=282)
            
            dpg.add_spacer(height=2)
            with dpg.table(tag="librarytable"):
                for i in range(appstate.TABLE_COLCOUNT):
                    dpg.add_table_column(label=f"table_header{i}", tag=f"table_header{i}")
                for r in range(appstate.TABLE_ROWCOUNT):
                    with dpg.table_row(tag=f"table_row{r}"):
                        for c in range(appstate.TABLE_COLCOUNT - 1):
                            dpg.add_text(f"table[{r}, {c}]", tag=f"table[{r}, {c}]")
                            dpg.bind_item_font(dpg.last_item(), "MainFont")
                        dpg.add_selectable(tag=f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", label=f"table[{r}, {appstate.TABLE_COLCOUNT - 1}]", span_columns=True, callback=on_library_rowclick)
                        dpg.bind_item_font(dpg.last_item(), "MonoFont")
                            
            dpg.configure_item("table_header0", label="Title", init_width_or_weight=1)
            dpg.configure_item("table_header1", label="Artist", init_width_or_weight=.75)
            dpg.configure_item("table_header2", label="Charter", init_width_or_weight=.5)
            dpg.configure_item("table_header3", label="Folder", init_width_or_weight=.75)                
            dpg.configure_item("table_header4", label="Best Path", init_width_or_weight=1)

            with dpg.group(horizontal=True, tag="librarytablecontrols"):
                dpg.add_button(tag="pageleftbutton", arrow=True, direction=dpg.mvDir_Left, callback=on_pageleft)
                dpg.add_text("0/0", tag= "librarypagelabel")
                dpg.add_button(tag="pagerightbutton", arrow=True, direction=dpg.mvDir_Right, callback=on_pageright)
                # dpg.add_text(f"Game version: {hymisc.ENGINE_LABEL}", tag="enginelabel", pos=(-100,-100))
        
        dpg.add_text("No songs scanned. Set a folder and scan songs to get started!", tag="libraryempty", show=False)
        
    # Showable
    dpg.add_file_dialog(
            directory_selector=True, show=False, callback=on_chartfolder_added,
            tag="select_chartfolder", width=700 ,height=400)

    with dpg.window(tag="scanprogress", show=False, modal=True, no_title_bar=True, no_close=True, no_resize=True, no_move=True):
        with dpg.group(indent=16):
            dpg.add_text("Discovering charts...", tag="scanprogress_discovering")
            dpg.add_text("0 charts found.", tag="scanprogress_chartsfound", show=False)
            dpg.add_text("Adding charts to library...", tag="scanprogress_bartext", show=False)
            dpg.add_progress_bar(tag="scanprogress_bar", show=False, width=-18)
            dpg.add_text("Done!", tag="scanprogress_done", show=False)
            dpg.add_text("Skipped failed songs:", tag="scanprogress_failedsongslabel", show=False)
            dpg.add_child_window(tag="scanprogress_failedcontainer", show=False, height=100, width=-18, horizontal_scrollbar=True)
            dpg.add_spacer(height=0)
            dpg.add_button(tag="scanprogress_dismiss", label="Continue", callback=on_scan_dismiss, show=False)

    with dpg.window(tag="batchprogress", show=False, modal=True, no_title_bar=True, no_close=True, no_resize=True, no_move=True):
        with dpg.group(indent=16):
            dpg.add_text("Analyzing library...", tag="batch_title")
            dpg.add_text("", tag="batch_current")
            dpg.add_progress_bar(tag="batch_bar", width=-18)
            dpg.add_text("", tag="batch_counts")
            dpg.add_text("Done!", tag="batch_done", show=False)
            dpg.add_text("Skipped failed songs:", tag="batch_failedlabel", show=False)
            dpg.add_child_window(tag="batch_failedcontainer", show=False, height=120, width=-18, horizontal_scrollbar=True)
            dpg.add_spacer(height=0)
            with dpg.group(horizontal=True):
                dpg.add_button(tag="batch_cancelbutton", label="Cancel", callback=on_batch_cancel)
                dpg.add_button(tag="batch_dismiss", label="Continue", callback=on_batch_dismiss, show=False)
    
    with dpg.window(label="Song Details", tag="songdetails", show=False, modal=True, no_title_bar=False, no_close=False, no_resize=True, no_move=True):
        with dpg.group(tag="songdetails_upperpanel", horizontal=True, height=170):
            with dpg.child_window(tag="songdetails_songinfo", width=390, horizontal_scrollbar=True):
                with dpg.group(horizontal=True):
                    dpg.add_image("icon_record")
                    dpg.add_text("Song Title", tag="songdetails_songtitle")
                    dpg.bind_item_font(dpg.last_item(), "MainFont24")
                with dpg.group(horizontal=True):
                    dpg.add_image("icon_star")
                    dpg.add_text("Song Artist", tag="songdetails_songartist")
                    dpg.bind_item_font(dpg.last_item(), "MainFont24")
                with dpg.group(horizontal=True):
                    dpg.add_image("icon_pencil")
                    dpg.add_text("Charter", tag="songdetails_songcharter")
                    dpg.bind_item_font(dpg.last_item(), "MainFont24")
                with dpg.group(horizontal=True):
                    dpg.add_image("icon_hash")
                    with dpg.group():
                        dpg.add_spacer(height=2)
                        dpg.add_text("Hash", tag="songdetails_songhash", color=(180,180,180,255))
                    dpg.bind_item_font(dpg.last_item(), "MonoFont")
            with dpg.child_window(tag="songdetails_songanalysis", width=400):
                dpg.add_text("/// Space for future stuff! ///") # Song Traits?
            with dpg.child_window(tag="songdetails_controls", width=-1):
                dpg.add_separator(label="More Paths settings")
                with dpg.group(horizontal=True):
                    dpg.add_text(" Score range:")
                    dpg.add_input_int(tag="inp_depthvalue", indent=100, min_value=0, min_clamped=True, width=140, default_value=1, callback=on_depth_value)
                    dpg.add_combo(("scores", "points"), tag="inp_depthmode", default_value="scores", width=80, callback=on_depth_mode)
                with dpg.group(horizontal=True):
                    dpg.add_text(" Limit timings:")
                    dpg.add_checkbox(tag="mslimit_check", indent=100, callback=on_mslimit_check)
                    dpg.add_input_int(tag="inp_mslimit", indent=140, min_value=-200, min_clamped=True, max_value=200, max_clamped=True, width=100, default_value=0, callback=on_mslimit_value)
                    dpg.add_text("ms", tag="mslimit_mstext")
                dpg.add_spacer(height=0)
                dpg.add_button(tag="runbutton", label="Analyze paths!", width=-1,height=-1, callback=on_run_chart)
                dpg.bind_item_font(dpg.last_item(), "MainFont24")
                    
                    #dpg.add_text("???")
        with dpg.group(tag="songdetails_lowerpanel", horizontal=True, height=-1):
            dpg.add_text("After analyzing this song, paths will show up here.", tag="songdetails_nopathsyet", show=False, indent=12)
            dpg.bind_item_font(dpg.last_item(), "MainFont24")
            dpg.add_text("This record is out of date. To make sure you have the latest results, please re-analyze.", tag="songdetails_plsupdaterecord", show=False, indent=12)
            dpg.bind_item_font(dpg.last_item(), "MainFont24")
            dpg.add_child_window(tag="songdetails_pathpanel", border=False, width=600)
            with dpg.child_window(tag="songdetails_pathdivider", border=False, width=5, frame_style=True):
                pass
                
            with dpg.child_window(tag="songdetails_pathdetails", border=False, width=-1):
                dpg.add_text("Filler - Path Details")
        #dpg.add_text("Song Name", tag="songdetails_name")
        with dpg.child_window(tag="songdetails_progresspanel", frame_style=True, show=False):
            #with dpg.child_window(tag="songdetails_progresscontent")
            with dpg.group(indent=16):
                dpg.add_text("Parsing chart...", tag="analyze_parse_label")
                dpg.add_text("Running paths...", tag="analyze_opt_label", show=False)
                dpg.add_progress_bar(tag="analyze_opt_bar", show=False, width=-18)
                dpg.bind_item_font("analyze_opt_bar", "MonoFont")
                dpg.add_text("Done!", tag="analyze_opt_done", show=False)
                dpg.add_text("An error occurred:", tag="analyze_errorlabel", show=False)
                dpg.add_text("", tag="analyze_errorcontent", show=False)
                dpg.bind_item_font(dpg.last_item(), "MonoFont")
                dpg.add_button(tag="analyze_dismissbutton", label="Continue", callback=on_analyze_dismiss, show=False)
    
    dpg.set_viewport_resize_callback(on_viewport_resize)
    on_viewport_resize()


if __name__ == '__main__':
    # appstate is visible to the top-level functions
    appstate = HyAppState()
            
    dpg.create_context()
    
    # Begin UI
    icopath = str(hymisc.ICOPATH_APP)
    verstr = f"Hydra v{'.'.join([str(n) for n in hymisc.HYDRA_VERSION])}"
    dpg.create_viewport(title=verstr, width=1280, height=720, small_icon=icopath, large_icon=icopath)

    #demo.show_demo()
    #dpg.show_font_manager()

    with dpg.handler_registry():
        dpg.add_key_press_handler(dpg.mvKey_C, callback=on_key_c_press)
        dpg.add_key_release_handler(dpg.mvKey_C, callback=on_key_c_release)

    dpg.setup_dearpygui()
    
    with dpg.window(label="Hydra", tag="mainwindow", show=True):
        with dpg.group(tag="preload"):
            with dpg.child_window(frame_style=True, width=280, height=100, pos=(500,280)):
                dpg.add_text("Loading...", pos=(110,38))
                
    dpg.set_primary_window("mainwindow", True)
    
    dpg.show_viewport()

    # Spread some setup across the first few frames
    setupframe = 0
    while dpg.is_dearpygui_running():
        dpg.render_dearpygui_frame()
        
        if setupframe == 1:
            # Loading window has rendered, so start some hitch-y setup
            build_main_ui()
        elif setupframe == 2:
            # Hitch over and everything's ready now
            view_main()
        
        if setupframe <= 2:
            setupframe += 1

        # Long-running library analysis advances one chart per frame so the
        # window keeps repainting and Cancel stays clickable.
        if appstate.batch is not None:
            batch_step()
            
    dpg.start_dearpygui()

    # End UI
    dpg.destroy_context()