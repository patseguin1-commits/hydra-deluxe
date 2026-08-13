# Claude Prompt: PhD-Level Hydra Search Optimization Study

## Instructions for Claude

You are a PhD-level computer scientist performing an exhaustive optimization study of the Hydra Clone Hero drum path optimizer. Your goal: make a full blink-182 discography scan (1,732 SP phrases) at **128-bar SP meter cap** run in **under 10 seconds**, down from the current ~87.5 seconds.

Use subagents to assist your exploration. Search the internet for solutions. Think creatively.

> **STATUS UPDATE (2026-08-13): the headline target is already met.** Enabling
> the native C++ search by default (it was opt-in) takes the *real app* path for
> the blink-182 discography from **~65.8s to ~9.6s** end-to-end, depth 4,
> uncapped, settling at cap 128, with an **identical score (71,786,175)**. The
> search *stage* alone went ~59s → ~3.3s (~16x). Measured on this machine; see
> the answered "Specific Questions" below. The remaining sections are kept as
> the original research brief, but note two corrections folded in: (a) the old
> ~87.5s figure was the Python-search path, and (b) with native search on the
> bottleneck has moved off the search entirely and onto `ScoreGraph.__init__`.

---

## Context

### What Hydra Does

Hydra finds optimal Star Power activation paths for Clone Hero drum charts. Given a song's note chart, it determines when to activate Star Power to maximize score. The search is a **breadth-first exploration of a two-track score graph** where millions of simultaneous paths are maintained and pruned each iteration.

### The Uncapped Problem

Normal Hydra caps the SP meter at 4 bars. **Hydra Uncapped** removes this cap, running the SP meter ceiling up a ladder (16, 32, 64, 128, 256, 512 bars) until two consecutive rungs produce the same score ("settled"). Cost grows ~2.5x per doubling because a path holding a different number of bars is a different path and nothing merges them.

### Current Performance (Expert Pro Drums 2x, search depth 4)

These are the **Python-search** numbers (native search off), as originally recorded:

| Chart | Capped | Uncapped | Settled At |
|---|---|---|---|
| Hail The Sun — Discography (961 phrases) | 3.7s | 36.4s | 64 bars |
| Rise Against — Discography | 6.1s | 38.1s | 64 bars |
| **blink-182 — Discography (1,732 phrases)** | **7.3s** | **87.5s** | **128 bars** |
| Endless Setlist I (4.4 hours of music) | 7.7s | 98.8s | 128 bars |

**Target: blink-182 uncapped in < 10 seconds.** Originally a ~9x ask.

**Measured with native search on (2026-08-13, real app path, parse once):**
blink-182 uncapped **~65.8s → ~9.6s (~6.8x end-to-end)**, identical score. The
9.6s splits as parse 1.76s + 4× `ScoreGraph.__init__` ~4.6s + native search
~3.3s. **Target met.** (A benchmark harness that re-parses per rung will report
~19s for the same run — that is an artifact, not the app; see Question 1.)

---

## Architecture (Read These Files)

### Core Files

| File | Lines | What It Does |
|---|---|---|
| `hydra/hypath.py` | 1331 | **THE HOT PATH.** ScoreGraph construction, GraphPather search loop, path reduction, bound pruning. Start here. |
| `hydra/hydata.py` | 1242 | Path/Activation/Squeeze data classes, serialization |
| `hydra/hyflat.py` | 365 | Flattens ScoreGraph to arrays for C++ native search |
| `hydra/hynative.py` | 663 | C++ library loader, ctypes interface, ABI |
| `hydra/hymisc.py` | 501 | Constants, Timecode, TempoMap, SP_CAP_LADDER |
| `hydra/hyutil.py` | 445 | Analysis entry points, uncapped ladder logic |
| `hydra/hysong.py` | 1018 | Song/Chord/Note parsing |
| `native/` | C++ | Native search engine (hydra_score.dll) |

### Algorithm Architecture

```
hyutil._analyze_uncapped()
  for sp_cap in (16, 32, 64, 128, ...):     # SP_CAP_LADDER
    hyutil._analyze_at_cap(song, sp_cap)
      graph = ScoreGraph(song, sp_meter_cap=sp_cap)   # Build two-track graph
      pather = GraphPather()
      pather.read(graph, depth_mode, depth_value)      # THE SEARCH
        if hynative.SEARCH_ENABLED:
          flat = hyflat.flatten(graph)
          paths = hynative.search(flat, ...)            # C++ path: crosses boundary ONCE
        else:
          # Python path: main loop
          while any path incomplete:
            for each path:
              path.advance(sp_cap)         # ~1.9M calls/chart
              path.branch_activate()       # or branch_deactivate()
            _reduce_iteration_paths()      # ~4M calls/discography
    if score == previous_score: break      # Settled - done
```

### Data Structures

**ScoreGraph** (two-track):
- Base track: paths without SP active
- SP track: paths with SP active
- Branch edges connect tracks (activation/deactivation only)
- Advance edges move forward within same track
- Each edge carries: basescore, comboscore, spscore, soloscore, accentscore, ghostscore, sp_times, backends

**GraphPath** (one live search path):
```python
data: hydata.Path        # accumulating result
currentnode: ScoreGraphNode
sp: int                  # SP meter (0 to sp_cap bars)
score: int               # running total (6 categories summed)
sp_end_time: Timecode    # when current SP activation ends
sp_ready_time: Timecode  # when path collected 2 bars (can activate)
buffered_sqinout_sp: int # SP to not double-count in squeezes
```

### Why Uncapped Is Expensive

At cap=4, there are only 5 possible SP meter values (0-4), so paths with identical SP merge aggressively. At cap=128, there are 129 possible values. Two paths holding different bar counts are never compared in `_reduce_group` - they're in different comparison groups. So the number of live paths explodes with the cap, and nothing eliminates them across groups.

---

## Existing Optimizations Already In Place

### 1. Native C++ Search (hypath.py:644, hynative.py)
- Entire search runs in C++ via ctypes, crossing the boundary **once per chart**
- **Now ON by default** when the DLL is present. Opt out with `HYDRA_NO_NATIVE_SEARCH=1` (was previously opt-in via `HYDRA_NATIVE_SEARCH=1`).
- **This was the single biggest lever, and it is now spent.** It took the blink-182 uncapped run ~65.8s → ~9.6s (real app, measured). The per-chord native function (`category_scores`) remains 1.6% slower due to ctypes overhead and stays off; only the per-chart search amortizes the boundary cost.
- **Consequence:** the search is no longer the hot path. Profiling the native run now shows `ScoreGraph.__init__` (graph construction, rebuilt once per ladder rung) as the largest single cost (~4.6s of the 9.6s), ahead of the search (~3.3s) and parse (~1.8s).

### 2. Running Score (hypath.py:999-1022)
- `path.score` maintained incrementally rather than re-summing 6 categories per path per iteration
- Eliminates O(paths * 6) work per iteration

### 3. Optimized Path Reduction (hypath.py:868-960)
- Groups compared only in identical SP situations
- Bisect search for score ranking (O(log n))
- Early exit for small groups (≤ depth_value + 1 paths)
- Hoisted attribute lookups

### 4. Deactivation Detection (hypath.py:588-617)
- Returns small ints not strings
- Compares `.ticks` directly (int64)

### 5. Bound Pruning (hypath.py:808-866) — **DISABLED**
- `ENABLE_BOUND_PRUNE = False`
- Currently uses `max_suffix` (assumes entire rest of song played in SP)
- Only prunes 0.13% of paths because the bound is too loose
- **THE AUTHOR IDENTIFIED THE FIX** (lines 836-843): bound the SP *time* using `2*(b+r)` measures where b=bars held, r=phrases remaining, and charge it at the densest spscore rate. This would make the ceiling drop below the bar late in a chart.

---

## The Key Optimization Opportunity (Author's Own Design)

From `hypath.py` lines 836-843:

> What would make it bite is a tighter ceiling. max_suffix assumes the entire rest of the song is played in SP, and no path can do that: a path holding b bars with r phrases left can be in SP for at most 2 * (b + r) measures. Bounding the SP *time* that way, and charging it at the densest spscore rate left in the song, would give a ceiling that actually falls below the bar late in a chart. That needs a per-measure spscore index over the suffix.

This is the **single most impactful algorithmic improvement** for uncapped runs. At high caps, paths in different SP situations (different bar counts) cannot eliminate each other through `_reduce_group` - they're in different comparison groups. Tighter bound pruning is the only way to cut across those groups.

**Implementation sketch:**
1. During `compute_bounds()`, precompute a per-measure spscore density index over the suffix
2. For each path, compute: `tight_max_suffix = base_suffix + min(2*(sp + remaining_phrases), remaining_measures) * max_spscore_density + remaining_frontends + remaining_backends`
3. This bound falls as a path progresses through the song (fewer phrases and measures remain)
4. Late in the chart, `2*(b+r)` shrinks enough that `score + tight_max_suffix < bar` catches paths the current bound misses

---

## What to Investigate

### 1. Profile the Native C++ Search
The C++ search (`native/hydra_score.dll`) already exists. Questions:
- What's the native search speedup on the 87.5s uncapped blink-182 run?
- Where does the C++ search spend its time? Is it the same `_reduce_group` logic?
- Does the C++ search implement the bound pruning? Can we add tighter bounds there?
- Read `native/hydra_search.h` and `native/hydra_search.cpp` to understand the C++ algorithm

### 2. Implement Tighter Bound Pruning
The author's design (above) is the highest-ROI algorithmic change:
- Compute per-measure spscore density during `compute_bounds()`
- Track remaining SP phrases per graph position
- Use `2*(bars_held + phrases_remaining)` to bound maximum SP time
- Apply this tighter bound in `_prune_hopeless_paths()`
- **Critical:** This must also be implemented in the C++ search if that's the hot path

### 3. Parallelize the Uncapped Ladder
`_analyze_uncapped()` runs caps sequentially (16 → 32 → 64 → 128). Each rung is independent except for the convergence check. Could we:
- Run multiple rungs in parallel?
- Use the previous rung's score as a pruning incumbent for the next (already plumbed but unused because `ENABLE_BOUND_PRUNE` is off)?
- Skip rungs by detecting settling earlier?

### 4. Reduce Path Explosion Across SP Groups
The core scalability problem: at cap=128, paths in 129 different SP meter states can't eliminate each other. Ideas:
- **Dominance across groups:** Path A with 50 SP bars and score X can never beat Path B with 50 bars and score X+100, regardless of future. Can we formalize cross-group dominance?
- **State compression:** Are some bar counts equivalent late in the song? If remaining_phrases < bar_difference, two paths will finish identically.
- **Lazy expansion:** Don't track all 129 SP values; only expand when a phrase is encountered

### 5. Numba JIT on Python Hot Loops

Numba is installed. The Python search path (`GraphPather.read` when native C++ is off) has three hot loops that are candidates for `@numba.jit(nopython=True)`:

**Candidates:**
- **`GraphPath.advance()`** (~1.9M calls/chart): Increments 6 score fields, iterates `sp_times`, updates SP meter. Currently a method on a Python class — must be extracted to a standalone function taking primitives/arrays to work with nopython mode.
- **`_reduce_iteration_paths()`** grouping loop (~4M calls/discography): Builds comparison groups by SP situation, then delegates to `_reduce_group`. The grouping loop itself is a dict-building pass over all paths — could be replaced with sorted arrays + Numba.
- **`_reduce_group()` inner loop**: Bisect-based score ranking over sorted arrays of integers. Natural fit for Numba if the data is pulled out of Python objects into contiguous arrays.

**Challenges:**
- Numba nopython mode cannot access Python objects, class instances, or dicts. The path state (`GraphPath`) is a class with object references (`currentnode`, `data`). JIT-ing the hot loop requires **restructuring to struct-of-arrays**: pull `score`, `sp`, `sp_end_time`, `currentnode_index` into parallel NumPy arrays and process them in bulk.
- This is a significant refactor but could yield 10-100x on the Python path, making it competitive with the C++ search.
- The Numba approach is **complementary** to tighter bound pruning — pruning reduces the number of paths, Numba makes processing each path faster.

**Investigation:**
- Profile the Python search to confirm which function dominates wall time
- Determine whether struct-of-arrays conversion is feasible without changing the algorithm
- Estimate Numba speedup on a toy version of the advance loop
- Consider `@numba.jit(parallel=True)` with `prange` for the per-path loop inside each iteration

### 6. Search the C++ Source
Read `native/hydra_search.cpp` and `native/hydra_search.h`:
- Does it implement the same `_reduce_group` logic?
- Does it have bound pruning? (The Python says no - line 714)
- Can we add SIMD/parallel processing within the C++ search?
- Is there low-hanging fruit in memory layout (struct-of-arrays vs array-of-structs)?

### 7. External Solutions
- **CHOpt** (https://github.com/GenericMadScientist/CHOpt): Another Clone Hero SP optimizer. Study its algorithm - does it solve the same exponential scaling problem?
- **SAT/SMT solvers:** Could the path optimization be encoded as a constraint satisfaction problem?
- **Integer Linear Programming:** The score is a sum of integer components with constraints - is this a natural ILP?

---

## Specific Questions to Answer

1. **What is the actual speedup from native search on the blink-182 uncapped run?** **ANSWERED (2026-08-13).** Real app path, parse once, depth 4, uncapped, settles at cap 128: **~65.8s → ~9.6s, ~6.8x end-to-end**, identical score (71,786,175). The search *stage* alone is ~16x (~59.2s → ~3.3s); it is diluted to 6.8x by parse (~1.76s, once) and four `ScoreGraph.__init__` builds (~4.6s total) that the native search does not touch. **The <10s target is met** by the default flip alone — no algorithmic work required. Caveat: `benchmark_depth128.py` reports ~19.15s for the same run because it re-parses the 3.26 MB MIDI once per rung and totals wall-clock over the ladder; the real `_analyze_uncapped` receives a pre-parsed song and parses once, so believe ~9.6s, not ~19s. **Next bottleneck is `ScoreGraph.__init__`, not the search** — see Question 7 territory and the note under "Existing Optimizations #1".

2. **How many live paths exist at each iteration during the cap=128 rung?** This tells us the width of the search and whether pruning can reduce it.

3. **What fraction of paths are in SP groups that can never reach the result?** This is what tighter bound pruning would eliminate.

4. **Can rungs of the uncapped ladder be parallelized?** The convergence check needs sequential scores, but could we speculatively run the next rung?

5. **Is the problem structure amenable to GPU acceleration?** The advance loop processes millions of paths identically - is the memory access pattern regular enough for GPU?

6. **What does CHOpt do differently?** Does it face the same exponential scaling, or does it use a fundamentally different algorithm?

7. **Can Numba JIT close the gap with native C++?** If `advance()` and `_reduce_group()` are extracted to Numba nopython functions operating on NumPy arrays, does the Python path become competitive with the C++ search — and is that refactor cheaper than improving the C++ side?

---

## Constraints

- **Language:** Python 3.14 + C++ (via ctypes). **Numba is installed** — use `@numba.jit(nopython=True)` on hot numeric loops that can be extracted from class methods into standalone functions.
- **Dependencies:** Minimize new dependencies. Pure Python or C/C++ preferred.
- **Correctness:** Paths must be identical to the unoptimized version. Use `test/test_search_parity.py` and the existing test suite.
- **Recurring workflow:** This is interactive, not one-shot. Solutions must be maintainable.
- **Hardware:** Windows 11, NVIDIA GPU available for potential CUDA work.

---

## How to Verify

```bash
# Run the existing parity test
py -m pytest test/test_search_parity.py -v

# Benchmark a single chart
py benchmark_depth128.py --chart "test/input/common/IB24/T5/Meshuggah - Nostrum/notes.mid" --max-cap 128

# Compare native vs Python
py benchmark_depth128.py --chart <chart> --native --max-cap 128
py benchmark_depth128.py --chart <chart> --no-native --max-cap 128
```

---

## Deliverables

For each optimization you propose:
1. **Expected speedup** (with reasoning, not just a guess)
2. **Implementation difficulty** (Trivial / Easy / Medium / Hard / Research)
3. **Specific code changes** (which files, which functions, what logic)
4. **Risk of regression** (what could break, how to test)
5. **Priority** (do this first / second / last / skip)

Rank all proposals by **impact-to-effort ratio** for the specific goal of blink-182 uncapped at 128 bars in < 10 seconds.
