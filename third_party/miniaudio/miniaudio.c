/* Single implementation translation unit for miniaudio.
 * Hydra uses miniaudio for the output device (WASAPI only), sample-rate and
 * channel conversion, and its dr_libs decoders for WAV, MP3 and FLAC. OGG
 * goes to stb_vorbis and OPUS to libopus instead. The MA_NO_* and
 * MA_ENABLE_* switches are PUBLIC definitions on the miniaudio target in
 * CMakeLists.txt, so this file and every includer see the same set. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
