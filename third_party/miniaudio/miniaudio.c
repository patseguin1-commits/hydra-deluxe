/* Single implementation translation unit for miniaudio.
 * Hydra uses miniaudio only for the output device, resampling, and mixing;
 * each audio format is decoded to PCM by our own code (stb_vorbis for OGG,
 * libopus for OPUS, dr_libs for MP3), so miniaudio's own decoders and
 * encoders are compiled out to keep this unit small. */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
