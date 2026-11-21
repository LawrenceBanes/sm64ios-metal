#include "mod_audio_mp3.h"

#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "vendor/minimp3/minimp3_ex.h"

bool mod_audio_decode_mp3(const uint8_t *data, size_t size, size_t max_pcm_bytes,
                          unsigned max_seconds, int16_t **pcm_out, unsigned *frames_out,
                          unsigned *rate_out, unsigned *channels_out) {
    if (!data || size < 4 || size > 2u * 1024u * 1024u || max_pcm_bytes < 4096 ||
        !max_seconds || !pcm_out || !frames_out || !rate_out || !channels_out)
        return false;
    *pcm_out = NULL;
    *frames_out = *rate_out = *channels_out = 0;

    mp3dec_t decoder;
    mp3dec_init(&decoder);
    mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t *pcm = malloc(max_pcm_bytes);
    if (!pcm) return false;
    size_t written = 0;
    const uint8_t *cursor = data;
    size_t remaining = size;
    unsigned channels = 0, rate = 0;
    bool valid = true;
    while (remaining >= 4) {
        mp3dec_frame_info_t info = {0};
        int frame_count = mp3dec_decode_frame(&decoder, cursor, (int)remaining, frame, &info);
        if (info.frame_bytes <= 0 || (size_t)info.frame_bytes > remaining) {
            ++cursor;
            --remaining;
            continue;
        }
        cursor += info.frame_bytes;
        remaining -= (size_t)info.frame_bytes;
        if (frame_count <= 0) continue;
        if ((info.channels != 1 && info.channels != 2) || info.hz < 8000 || info.hz > 96000 ||
            (channels && (channels != (unsigned)info.channels || rate != (unsigned)info.hz))) {
            valid = false;
            break;
        }
        channels = (unsigned)info.channels;
        rate = (unsigned)info.hz;
        size_t bytes = (size_t)frame_count * channels * sizeof(*pcm);
        if (bytes > max_pcm_bytes - written ||
            written + bytes > (size_t)channels * rate * max_seconds * sizeof(*pcm)) {
            valid = false;
            break;
        }
        memcpy((uint8_t *)pcm + written, frame, bytes);
        written += bytes;
    }
    if (!written || !valid || !channels || !rate) {
        free(pcm);
        return false;
    }
    int16_t *smaller = realloc(pcm, written);
    *pcm_out = smaller ? smaller : pcm;
    *frames_out = (unsigned)(written / (channels * sizeof(*pcm)));
    *rate_out = rate;
    *channels_out = channels;
    return true;
}
