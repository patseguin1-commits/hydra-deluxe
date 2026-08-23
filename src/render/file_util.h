// Small file readers shared by the Preview renderer's asset loaders (models,
// textures, shaders, config). Both return empty on any failure so a missing
// asset is reported by the caller with its own message, not by an exception
// from deep inside a loader.

#ifndef HYDRA_RENDER_FILE_UTIL_H
#define HYDRA_RENDER_FILE_UTIL_H

#include <cstdint>
#include <string>
#include <vector>

namespace hydra::render {

// Whole file as bytes; empty on a missing, unreadable, or empty file.
std::vector<uint8_t> read_file_bytes(const std::string& path);

// Whole file as text; empty on a missing, unreadable, or empty file.
std::string read_file_text(const std::string& path);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_FILE_UTIL_H
