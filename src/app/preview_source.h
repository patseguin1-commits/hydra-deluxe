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
//   * a .srb container — audio is in the trailing DEFLATE streams past the
//     notes stream, identified by magic bytes since the streams are unnamed.
//
// The audio bytes/paths produced here are decoded and mixed by the audio engine
// (Phase 3); this module does no decoding.

#ifndef HYDRA_APP_PREVIEW_SOURCE_H
#define HYDRA_APP_PREVIEW_SOURCE_H

#include <cstdint>
#include <string>
#include <vector>

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
};

// Parse `notespath` (any supported chart kind) and gather its audio. pro/bass2x
// mirror the analysis toggles so the previewed notes match the analyzed ones.
PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x);

// ---- pieces, exposed for testing and reuse -------------------------------

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

// Audio blobs embedded in a .srb container: the trailing DEFLATE streams past
// the notes stream, inflated and kept when they look like audio. Labels tag the
// stream index.
std::vector<PreviewAudioStem> extract_srb_audio(const std::string& path);

}  // namespace hydra::app

#endif  // HYDRA_APP_PREVIEW_SOURCE_H
