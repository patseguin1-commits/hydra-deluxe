// Shared image decode — the single stb_image translation unit for the whole
// binary.
//
// stb_image's implementation may be compiled in exactly one TU (it defines
// non-static symbols), yet both the GUI's icons (src/ui/icons.cpp) and the
// Preview renderer's authored textures (src/render) need PNG/JPEG decoding. So
// the STB_IMAGE_IMPLEMENTATION lives here, behind a small decode function, and
// both link this tiny library instead of each defining their own copy.

#ifndef HYDRA_IMAGE_DECODE_H
#define HYDRA_IMAGE_DECODE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hydra::image {

// Decoded pixels: 8-bit RGBA, `width` x `height`, row-major from the top-left,
// tightly packed (row pitch = width * 4). Empty (width/height 0) on failure.
struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    bool empty() const { return rgba.empty(); }
};

// Decode PNG or JPEG bytes to RGBA (always 4 channels). Returns an empty image
// on any failure — callers fall back rather than handle an exception.
DecodedImage decode_image(const uint8_t* data, std::size_t size);
inline DecodedImage decode_image(const std::vector<uint8_t>& bytes) {
    return decode_image(bytes.data(), bytes.size());
}

}  // namespace hydra::image

#endif  // HYDRA_IMAGE_DECODE_H
