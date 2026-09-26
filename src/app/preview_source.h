// Preview source resolution — find a chart's notes and its audio so the Preview
// can draw the highway and play the song.
//
// A stored record keeps only timing and paths, not the note stream (see
// docs/adr and record_store), so the Preview re-parses the chart file here.
// Audio is never stored either: this module locates it. Three sources, matching
// the chart kinds Hydra scans:
//   * a loose folder — audio sits beside the notes file (song.ogg, drums.opus,
//     stems, ...);
//   * a .sng container — audio lives in the same XOR-masked file table the
//     notes come from;
//   * a .srb container — art streams follow the notes stream in the DEFLATE
//     chain; the song audio lives past the chain in a section encrypted with
//     AES-128-CFB (a hardcoded key and a per-blob IV derived from the blob
//     header). extract_srb_audio decrypts these blobs in-memory.
//
// The audio bytes/paths produced here are decoded and mixed by the audio engine
// (Phase 3); this module does no decoding.

#ifndef HYDRA_APP_PREVIEW_SOURCE_H
#define HYDRA_APP_PREVIEW_SOURCE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/rules.h"
#include "parse/song.h"

namespace hydra::app {

// One audio track for the Preview. A loose track names a file on disk (`path`
// set, `bytes` empty); a container track carries its extracted, decompressed
// bytes (`bytes` set, `path` empty). `label` is a short stem name for display
// and mixing ("song", "drums", or a container stream tag).
struct PreviewAudioStem {
    std::string label;
    std::string path;
    std::vector<uint8_t> bytes;

    bool from_file() const { return !path.empty(); }
};

// A chart resolved for preview: its re-parsed notes and every audio stem found.
// `stems` may be empty — a chart with no locatable audio previews silently.
struct PreviewSource {
    Song song;
    std::vector<PreviewAudioStem> stems;
    // Where chart time 0 sits in the audio: audio_ms = chart_ms +
    // audio_offset_ms. From the delay (ms) and the .chart Offset (s), as
    // Clone Hero applies them. A folder chart's delay comes from its song.ini
    // and a .sng's from its metadata block. A .srb has no delay field, so
    // only its chart's Offset counts.
    double audio_offset_ms = 0.0;
};

// Parse `notespath` (any supported chart kind) and gather its audio.
// pro/bass2x/difficulty mirror the analysis toggles so the previewed notes
// match the analyzed ones. rules places the fills the same way analysis does.
PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x,
                                     Difficulty difficulty = Difficulty::Expert,
                                     const core::Rules& rules = core::default_rules());

// ---- pieces, exposed for testing and reuse -------------------------------

// song.ini's `delay` in milliseconds, or nullopt when the file or key is
// missing or the value is not a number.
std::optional<double> read_ini_delay_ms(const std::string& ini_path);

// A .sng container's `delay` metadata in milliseconds, the key matched in any
// case (the last one wins, like the library scan's metadata read), or nullopt
// when it is missing or not a number.
std::optional<double> sng_delay_ms(const std::vector<uint8_t>& sng_bytes);

// The Preview's audio offset in ms from the two values Clone Hero reads.
double preview_audio_offset_ms(std::optional<double> ini_delay_ms,
                               std::optional<double> chart_offset_s);

// True if `filename` ends in an audio extension Hydra can decode
// (.ogg/.opus/.mp3/.wav/.flac), case-insensitive.
bool is_audio_filename(const std::string& filename);

// True if `bytes` begins with a recognized audio container's magic (OggS, RIFF,
// fLaC, an ID3 tag, or an MP3 frame sync). Used to pick audio streams out of a
// .srb's unnamed trailing streams and skip album art.
bool looks_like_audio(const std::vector<uint8_t>& bytes);

// Audio files sitting beside a loose notes file: every decodable audio file in
// `folder` except a standalone "preview" clip. Labels are the base filename.
std::vector<PreviewAudioStem> find_loose_audio(const std::string& folder);

// Audio blobs embedded in a .sng container: its file table's audio entries,
// XOR-demasked to their original bytes. Labels are the entries' base filenames.
std::vector<PreviewAudioStem> extract_sng_audio(const std::string& path);

// Audio from a .srb container's encrypted section.  Walks past the DEFLATE
// chain (metadata, notes, art), then parses and AES-128-CFB-decrypts the
// audio blob chain that follows.  Each decrypted Ogg blob becomes a stem.
std::vector<PreviewAudioStem> extract_srb_audio(const std::string& path);

}  // namespace hydra::app

#endif  // HYDRA_APP_PREVIEW_SOURCE_H
