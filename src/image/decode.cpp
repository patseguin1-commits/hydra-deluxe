#include "image/decode.h"

// The one and only STB_IMAGE_IMPLEMENTATION in the binary. PNG + JPEG cover the
// authored-art contract (docs/adr/0007); decoding is always from memory.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace hydra::image {

DecodedImage decode_image(const uint8_t* data, std::size_t size) {
    DecodedImage out;
    if (data == nullptr || size == 0) return out;

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width,
                                            &height, &channels, 4);
    if (pixels == nullptr) return out;

    out.width = width;
    out.height = height;
    out.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return out;
}

}  // namespace hydra::image
