// SPDX-License-Identifier: GPL-3.0-only
#include "silt_assets.h"
#include "Stream.h"

extern const cc_uint8 textures_start[] asm("_binary_default_zip_start");
extern const cc_uint8 textures_end[] asm("_binary_default_zip_end");
extern const cc_uint8 effects_start[] asm("_binary_classicube_zip_start");
extern const cc_uint8 effects_end[] asm("_binary_classicube_zip_end");
extern const cc_uint8 music_start[] asm("_binary_bgm_wav_start");
extern const cc_uint8 music_end[] asm("_binary_bgm_wav_end");

static void read_only(struct Stream *stream, const cc_uint8 *first, const cc_uint8 *last)
{
    Stream_ReadonlyMemory(stream, (void *)first, (cc_uint32)(last - first));
}

void silt_texture_stream(struct Stream *stream)
{
    read_only(stream, textures_start, textures_end);
}

void silt_effects_stream(struct Stream *stream)
{
    read_only(stream, effects_start, effects_end);
}

void silt_music_stream(struct Stream *stream)
{
    read_only(stream, music_start, music_end);
}
