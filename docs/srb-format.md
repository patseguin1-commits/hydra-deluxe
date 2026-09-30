# Clone Hero's .srb bundles, and how Hydra reads them

A `.srb` file is how Clone Hero packs its built-in setlist. Each one holds a
whole song in one file: the song's info, its chart, two images, and the audio.
Nobody has published the format. What follows was worked out by inspecting the
files directly, and every fact below was checked against the real ones.

Hydra reads `.srb` files the same way it reads a song folder or a `.sng`. It
pulls out the chart to analyze it, the name, artist and charter for the library,
and the audio for the Preview.

## Where they live

The built-in songs are in the game's install folder, under
`Clone Hero_Data\StreamingAssets\songs`. The Clone Hero 1.1 install on this
machine has 30 of them. Ten carry a `notes.mid` chart and twenty carry a
`notes.chart`.

To get them into Hydra, add that folder under `Manage folders...` and scan. The
User Guide says the same thing for players.

The files are Clone Hero's copyrighted content, so none are committed to this
repo. The tests build fake ones instead (see "How it's tested" below).

## The layout at a glance

Think of the file as a short label followed by a row of sealed bags, then a
locked box. The bags are compressed; the box is encrypted.

```
offset 0    header, 16 bytes
offset 16   stream 1   compressed   song info (name, artist, charter, ...)
            stream 2   compressed   the chart: a notes.mid or notes.chart
            stream 3   compressed   a JPEG image
            stream 4   compressed   a JPEG image
            audio section           encrypted Ogg Opus audio, 2 to 9 pieces
end of file
```

Nothing sits between one part and the next. The audio section runs exactly to
the end of the file in all 30 real bundles.

## Each part in turn

### The header

The first 16 bytes are 12 bytes that differ per file, then a 4-byte number.
The number ranges from 1 to 29 across the real files, and its meaning is
unknown. Hydra never needs either part; it just skips to offset 16.

### Why you have to unpack a stream to find the next one

The compressed streams are "raw DEFLATE". DEFLATE is the compression inside a
.zip file. "Raw" means there is no wrapper around it: no length, no checksum,
no marker saying where it ends.

That's like a row of vacuum-packed bags with no labels. You can't tell where
bag one ends by looking at it. You have to open it; the end of its contents
tells you where bag two begins.

So Hydra finds stream 2 by decompressing stream 1 and noting how many
compressed bytes that used up. The reader function `srb_inflate_stream` returns
that end position for exactly this reason. This is also why Hydra bundles the
miniz library: the decompressor it already had could not report how much input
it had consumed.

### Stream 1: the song info

Decompressed, this block is a few hundred bytes (353 to 974 in the real files).
It starts with four fixed bytes, `4b4` followed by a byte of value 1, in every
file.

Then come eight text fields. Each field is a 4-byte length followed by that many
bytes of UTF-8 text. They always appear in this order:

| # | Field | Example (Biology.srb) |
|---|-------|-----------------------|
| 1 | notes filename | `notes.mid` |
| 2 | song name | `Biology` |
| 3 | artist | `Fox Vibes` |
| 4 | album | `Mantra` |
| 5 | genre | `Math Rock` |
| 6 | charter | `Inventor, 3-UP, TheGuitarHeroNerd` |
| 7 | year | `2017` |
| 8 | description | a paragraph about the song and band |

Text can carry Clone Hero's colour and italic tags, such as `<color=#FFB300>` or
`<i>`. Several charter fields are coloured letter by letter.

After the eight fields comes 123 to 235 bytes of binary data. It probably holds
difficulty ratings, the preview start time and the sizes of the other parts.
Hydra does not decode it.

### Stream 2: the chart

This is an ordinary `notes.mid` or `notes.chart`, byte for byte. Field 1 of the
song info says which. Nineteen of the twenty `.chart` files begin with a UTF-8
byte-order mark (three invisible bytes some editors add); one doesn't. Hydra's
chart reader handles both.

### Streams 3 and 4: images

Both are JPEG files, most likely album art and a background. Hydra skips them.

### The audio section

After stream 4, the audio sits in a section encrypted with AES-128. AES is the
standard modern cipher; 128 is the key length in bits. The mode is CFB, which
turns the cipher into a stream you can decrypt byte by byte.

The section is a run of audio pieces, which Hydra calls blobs, back to back.
Each blob is laid out like this:

```
8 bytes    type tag (meaning unknown; see below)
16 bytes   blob header, which also supplies the decryption starting value (IV)
8 bytes    size of the encrypted data
N bytes    encrypted data
```

The key is the same for every file and is written into Hydra as `kSrbAesKey`.
The IV (the per-blob starting value AES needs) is the blob header with its two
halves swapped: bytes 8 to 15 first, then bytes 0 to 7.

Decrypted, every one of the 152 blobs in the 30 real files is an Ogg Opus audio
stream. Each file has between 2 and 9 blobs. They look like the song's
separate stems (individual instrument tracks). Most are a few megabytes, but a
handful are under 150 KB, which suggests a stem that is silent for most of the
song.

**The type tags.** The values seen across the 30 files are 0, 1, 2, 3, 6
through 12, and one file with all bits set. Within a file they almost never
repeat, which fits "one tag per instrument". Which number means which
instrument is not known, so Hydra skips the tags and names the blobs by
position instead.

Hydra's audio code first read the tag on the first blob as a separate
"purpose unclear" number at the start of the section. It had ruled out a blob
count, but never compared that number with the tags on the later blobs. The
two match in size and in the range of values, so the code comment now
describes every blob as starting with a tag. The reading loop was already
correct, because it skips that number either way.

## How Hydra uses a .srb

### Scanning the library

The scan treats every `.srb` it finds as one chart, the same as a `.sng`. There
is no `song.ini` to read.

Hydra computes the chart's identity (an MD5 fingerprint of the whole file) in
one pass. While hashing, it keeps the first megabyte. Stream 1 always fits in
that, so the name, artist and charter come from memory without a second read.

If stream 1 is damaged, the song still appears. It shows as an unknown title,
unknown artist and unknown charter. A blank name also shows as unknown.

A rescan skips any `.srb` whose size and timestamp haven't changed, the same as
every other chart.

### Analysing the chart

`load_songpath_srb` decompresses stream 1, then stream 2, then hands the chart
bytes to the normal `.mid` or `.chart` reader. From there a bundled song is
treated exactly like a loose chart; the same path search runs on it.

It picks the reader from field 1 of the song info. If that name ends in neither
`.mid` nor `.chart`, it checks the chart bytes instead: a MIDI file starts with
the letters `MThd`, and anything else is read as a `.chart`.

### The Preview

`extract_srb_audio` walks past streams 1 and 2. It keeps any later compressed
stream that looks like audio, in case a future bundle stores audio that way.
The real files never do; their streams 3 and 4 are images, so they are skipped.

Then it reads the encrypted section and decrypts each blob. The first blob is
labelled `song` and the rest `stem1`, `stem2`, and so on. The Preview plays
them all together.

If no audio comes out, the Preview falls back to any loose audio files in the
same folder as the `.srb`, as it would for a song folder.

A `.srb` has no delay setting that Hydra reads, so only the chart's own
`Offset` lines the audio up with the notes. A delay may hide in the undecoded
binary data after the eight text fields; nobody has checked.

### When a file is broken

Every read has a size cap. The song info may inflate to at most 1 MB. Any
other stream may inflate to at most 1 GB. Real charts are far smaller; the caps
only exist to stop a hostile file from filling memory.

A damaged, cut-off or oversized stream raises an error inside the reader. The
player sees the standard message: "Hydra couldn't read this chart file. It may
be damaged or in a format Hydra doesn't support; try downloading the song
again."

## How it's tested

`tests/test_srb.cpp` builds fake `.srb` files at test time. Each one is a
16-byte header, a compressed song-info block, a compressed chart taken from the
test corpus, and a compressed filler stream standing in for the rest. The tests
check four things. A wrapped chart parses exactly like the same chart loose.
An odd notes filename falls back to sniffing the bytes. Broken files throw an
error instead of crashing. The scan picks up the name, artist and charter,
including the blank-name fallback.

The encrypted audio section has no automated test yet. One would need a fake
audio blob encrypted with the same key. The facts above about it were checked
by decrypting the start of every blob in the 30 real files.

## Where the code is

The container reader is `src/parse/srb.h` and `src/parse/srb.cpp`. The header
file repeats the layout in a comment. The chart loader is `load_songpath_srb`
in `src/parse/song.cpp`. The library scan reads the song info in
`parse_srb_metadata` in `src/app/analysis.cpp`. The audio extraction and
decryption live in `src/app/preview_source.cpp`.

If a change to `srb.cpp` alters which notes a chart ends up with, two stamps in
`src/store/stored_versions.h` need bumping. The Dynamics count stamp goes up by
one so stored note counts get redone; its comment names `srb.cpp` directly. The
results stamp moves to the release version too, because the paths computed
from those notes change as well.
