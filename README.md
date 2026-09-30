# Hydra Deluxe
Score optimizer / path viewer for Clone Hero drums!

Featuring a song browsing UI to make information convenient to access even for large song libraries.

<br><p align="center"><img src="/resource/icon_app.png" width="200"></p>

Hydra Deluxe grew out of [DragonDelgar's Hydra](https://github.com/DragonDelgar/hydra). It started from the public v1.3.1 in August 2026, was rewritten as a native Windows app, and has grown a lot since. [How Hydra Deluxe differs from the public Hydra](docs/differences-from-public-hydra.md) covers every change, and why scores can differ between the two.

## What Hydra Deluxe does

Hydra Deluxe reads your Clone Hero drum charts and finds the Star Power paths that give the best score. It keeps a few paths just below optimal too, in case the best one is awkward to play.

For each path it tells you how to play it. Each activation says where it is, which chord to activate on, and which squeezes it needs. Each squeeze is a sentence saying which note to hit early or late, by how much, and what it's worth. A score breakdown matches the categories on Clone Hero's results screen.

The **Preview** plays the chart as a 3D note highway with the song's audio. It draws the path on it, with a running score and a Star Power meter. The **Dynamics** tab counts ghost and accent notes. The **Stars** tab shows the score each star needs.

It can analyze your whole library in the background and build a sortable HTML report of every song's paths. It can also compare a player's [dmleaderboards](https://dmleaderboards.com) scores with your optimals.

It reads `.mid`, `.chart`, `.sng` and `.srb` charts, on any difficulty, with or without Pro Drums and 2x Bass.

## Getting started

1. Download the installer (the `-setup.exe` file) from the [latest release](https://github.com/patseguin1-commits/hydra-deluxe/releases/latest) and run it. It installs to `C:\Program Files\Hydra`, adds a Start Menu shortcut, and installs the Microsoft VC++ runtime if your PC doesn't have it. The installer isn't code-signed, so Windows SmartScreen may warn — choose "More info" → "Run anyway".
2. Run Hydra Deluxe from the Start Menu.
3. Click `Manage folders...`, then `Add folder...`, and pick your Clone Hero songs folder (or whichever folder contains the songs you want to add).
4. Click `Scan library`.
5. Once it's done, songs appear in a table. Type in the search box to find the song you want to get the path for, then click on the song. Its panel opens beside the library.
6. Click the `Analyze this song` button.
7. Once it's done, paths appear. The first path is optimal. There may be other paths tied for optimal, listed under the same score. Below that are some of the next-highest scores and their paths, which could come in handy if the optimal path is too annoying or difficult.
8. Click a path on the left side of the panel to show its activations on the right side.
9. Use `<` and `>` to step to the previous or next song, or close the panel with its `X` (or `Escape`) to go back to browsing.

The [User Guide](docs/UserGuide.md) explains every button and tab, in the order you meet them.

## Learning the mechanics

The public Hydra wiki explains the playing mechanics. They apply to Hydra Deluxe too:

* [Path Notation Quick Reference](https://github.com/DragonDelgar/hydra/wiki/Path-Notation-Quick-Reference)
* [Optimal Checklist](https://github.com/DragonDelgar/hydra/wiki/Optimal-Checklist)
* [Squeezes and early fills](https://github.com/DragonDelgar/hydra/wiki): the wiki's "Detailed Mechanics" pages.

The wiki's own user guide describes the public app's older screen, not this one.

## Good to know

**Paths at an SP cap other than 4 can't be played.** Clone Hero's Star Power meter holds 4 bars. The **SP cap** setting lets you raise that to see what the cap costs a chart. Scores at any other cap are a what-if, not paths to play.

**Your data stays put.** Your library, results and settings live next to Hydra.exe in `C:\Program Files\Hydra`. Reports are saved in your `Documents\Hydra` folder. Upgrading keeps everything. Uninstalling keeps your data too; delete the folder yourself if you really want it gone.

**Had this app installed back when it was called Hydra?** Hydra Deluxe is the same program under a new name. It installs over that copy and keeps your library and results. Coming from DragonDelgar's public Hydra instead? See [moving from the public version](docs/differences-from-public-hydra.md#moving-from-the-public-version).

## Acknowledgements
- DragonDelgar, who wrote Hydra and whose public version this one grew from
- Onyx, whose drum previewer the Preview's highway is ported from
- Boddy, Beud, and Nick (BongOfDestiny) for active beta testing
- Boddy for reference footage / images shown in the Hydra Wiki
- Drummer's Monthly for being an awesome CH drums community, and for testing some of the first paths ever made by Hydra
- Smidge for all the love and support
