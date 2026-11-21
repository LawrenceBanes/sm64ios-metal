#ifndef SM64IOS_MOD_AUDIO_MP3_H
#define SM64IOS_MOD_AUDIO_MP3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mod_audio_decode_mp3(const uint8_t *data, size_t size, size_t max_pcm_bytes,
                          unsigned max_seconds, int16_t **pcm, unsigned *frames,
                          unsigned *sample_rate, unsigned *channels);

#endif
