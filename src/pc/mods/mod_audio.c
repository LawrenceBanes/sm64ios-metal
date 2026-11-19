#include "mod_audio.h"
#include "mod_resource.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "vendor/stb/stb_vorbis.c"

enum {
    MAX_SAMPLES = 48, MAX_VOICES = 24, MAX_OWNER_SAMPLES = 16,
    MAX_ENCODED = 2 * 1024 * 1024, DECODER_MEMORY = 4 * 1024 * 1024,
    MAX_PCM_BYTES = 8 * 1024 * 1024, MAX_OWNER_PCM_BYTES = 2 * 1024 * 1024,
    MAX_SECONDS = 15, OUTPUT_RATE = 32000,
};

typedef struct {
    const void *owner;
    char path[241];
    int16_t *pcm;
    unsigned frames, rate, channels;
    uint32_t id;
} Sample;

typedef struct {
    const Sample *sample;
    uint64_t position; /* Source position measured in 1/32000 of a sample frame. */
    float left, right;
} Voice;

static Sample sSamples[MAX_SAMPLES];
static Voice sVoices[MAX_VOICES];
static uint32_t sNextId = 1;

static size_t sample_bytes(const Sample *sample) {
    return (size_t)sample->frames * sample->channels * sizeof(int16_t);
}

static Sample *find_sample(const void *owner, uint32_t id) {
    for (size_t i = 0; i < MAX_SAMPLES; ++i)
        if (sSamples[i].owner == owner && sSamples[i].id == id && id)
            return &sSamples[i];
    return NULL;
}

static bool decode_sample(Sample *sample, const uint8_t *data, size_t size, size_t budget) {
    void *memory = malloc(DECODER_MEMORY);
    if (!memory) return false;
    stb_vorbis_alloc arena = { memory, DECODER_MEMORY };
    int error;
    stb_vorbis *decoder = stb_vorbis_open_memory(data, (int)size, &error, &arena);
    bool ok = false;
    if (decoder) {
        stb_vorbis_info info = stb_vorbis_get_info(decoder);
        uint64_t frames = stb_vorbis_stream_length_in_samples(decoder);
        if ((info.channels == 1 || info.channels == 2) &&
            info.sample_rate >= 8000 && info.sample_rate <= 96000 &&
            frames > 0 && frames <= (uint64_t)info.sample_rate * MAX_SECONDS &&
            frames * info.channels * sizeof(int16_t) <= budget &&
            stb_vorbis_get_error(decoder) == VORBIS__no_error) {
            sample->pcm = malloc((size_t)frames * info.channels * sizeof(int16_t));
            if (sample->pcm) {
                int decoded = stb_vorbis_get_samples_short_interleaved(
                    decoder, info.channels, sample->pcm, (int)(frames * info.channels));
                if ((uint64_t)decoded == frames && stb_vorbis_get_error(decoder) == VORBIS__no_error) {
                    sample->frames = (unsigned)frames;
                    sample->rate = info.sample_rate;
                    sample->channels = (unsigned)info.channels;
                    ok = true;
                } else {
                    free(sample->pcm);
                    sample->pcm = NULL;
                }
            }
        }
        stb_vorbis_close(decoder);
    }
    free(memory);
    return ok;
}

uint32_t mod_audio_load(const void *owner, const char *root, const char *path, const char **error) {
    size_t length = strlen(path);
    if (length < 5 || length > 240 || strcasecmp(path + length - 4, ".ogg")) {
        *error = "only mod-local Ogg Vorbis samples (.ogg) are supported";
        return 0;
    }
    Sample *slot = NULL;
    size_t total = 0, owned = 0, owner_count = 0;
    for (size_t i = 0; i < MAX_SAMPLES; ++i) {
        Sample *sample = &sSamples[i];
        if (!sample->owner) {
            if (!slot) slot = sample;
            continue;
        }
        size_t bytes = sample_bytes(sample);
        total += bytes;
        if (sample->owner == owner) {
            if (!strcmp(sample->path, path)) return sample->id;
            owned += bytes;
            ++owner_count;
        }
    }
    if (!slot || owner_count >= MAX_OWNER_SAMPLES || owned >= MAX_OWNER_PCM_BYTES || total >= MAX_PCM_BYTES) {
        *error = "decoded sample capacity exhausted";
        return 0;
    }
    size_t size = 0;
    uint8_t *data = mod_resource_read(root, path, MAX_ENCODED, &size);
    if (!data) {
        *error = "could not read sample inside the mod directory, or encoded size exceeds 2 MiB";
        return 0;
    }
    size_t budget = MAX_OWNER_PCM_BYTES - owned;
    if (budget > MAX_PCM_BYTES - total) budget = MAX_PCM_BYTES - total;
    bool decoded = decode_sample(slot, data, size, budget);
    free(data);
    if (!decoded) {
        *error = "invalid Ogg Vorbis sample or decoded channel, rate, duration or memory limit exceeded";
        return 0;
    }
    memcpy(slot->path, path, length + 1);
    slot->owner = owner;
    slot->id = sNextId++;
    if (!sNextId) sNextId = 1;
    return slot->id;
}

bool mod_audio_play(const void *owner, uint32_t id, float volume, float pan, const char **error) {
    Sample *sample = find_sample(owner, id);
    if (!sample) {
        *error = "sample is stale or belongs to another mod";
        return false;
    }
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (sVoices[i].sample) continue;
        sVoices[i] = (Voice){ sample, 0, volume * (pan > 0 ? 1 - pan : 1),
                                           volume * (pan < 0 ? 1 + pan : 1) };
        return true;
    }
    *error = "mod sample voice capacity exhausted";
    return false;
}

bool mod_audio_stop(const void *owner, uint32_t id) {
    Sample *sample = find_sample(owner, id);
    if (!sample) return false;
    for (size_t i = 0; i < MAX_VOICES; ++i)
        if (sVoices[i].sample == sample) sVoices[i] = (Voice){0};
    return true;
}

bool mod_audio_destroy(const void *owner, uint32_t id) {
    if (!mod_audio_stop(owner, id)) return false;
    Sample *sample = find_sample(owner, id);
    free(sample->pcm);
    *sample = (Sample){0};
    return true;
}

void mod_audio_unload(const void *owner) {
    for (size_t i = 0; i < MAX_SAMPLES; ++i)
        if (sSamples[i].owner == owner && sSamples[i].id)
            mod_audio_destroy(owner, sSamples[i].id);
}

static int16_t saturated_sample(float value) {
    if (value >= INT16_MAX) return INT16_MAX;
    if (value <= INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

void mod_audio_mix(int16_t *stereo, size_t frames, float sfx_gain) {
    bool playing = false;
    for (size_t i = 0; i < MAX_VOICES; ++i) playing |= sVoices[i].sample != NULL;
    if (!playing) return;
    for (size_t f = 0; f < frames; ++f) {
        float left = 0, right = 0;
        for (size_t i = 0; i < MAX_VOICES; ++i) {
            Voice *voice = &sVoices[i];
            const Sample *sample = voice->sample;
            if (!sample) continue;
            size_t frame = (size_t)(voice->position / OUTPUT_RATE);
            size_t next = frame + 1 < sample->frames ? frame + 1 : frame;
            float t = (float)(voice->position % OUTPUT_RATE) / OUTPUT_RATE;
            const int16_t *a = sample->pcm + frame * sample->channels;
            const int16_t *b = sample->pcm + next * sample->channels;
            unsigned right_channel = sample->channels - 1;
            left += voice->left * (a[0] + (b[0] - a[0]) * t);
            right += voice->right * (a[right_channel] + (b[right_channel] - a[right_channel]) * t);
            voice->position += sample->rate;
            if (voice->position >= (uint64_t)sample->frames * OUTPUT_RATE) voice->sample = NULL;
        }
        stereo[2 * f] = saturated_sample(stereo[2 * f] + left * sfx_gain);
        stereo[2 * f + 1] = saturated_sample(stereo[2 * f + 1] + right * sfx_gain);
    }
}
