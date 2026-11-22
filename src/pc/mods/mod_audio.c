#include "mod_audio.h"
#include "mod_audio_mp3.h"
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
    MAX_PCM_BYTES = 8 * 1024 * 1024, MAX_OWNER_PCM_BYTES = 4 * 1024 * 1024,
    MAX_SECONDS = 15, OUTPUT_RATE = 32000,
    MAX_SEQUENCE_OVERRIDES = 32, MAX_SEQUENCE_OVERRIDE_BYTES = 1024 * 1024,
    MAX_SEQUENCE_OVERRIDE_TOTAL_BYTES = 4 * 1024 * 1024,
};

typedef struct {
    const void *owner;
    uint8_t sequence_id, bank_id, default_volume;
    uint8_t *data;
    size_t size;
    uint64_t order;
    bool active;
} SequenceOverride;

typedef struct {
    const void *owner;
    char path[241];
    int16_t *pcm;
    unsigned frames, rate, channels;
    uint32_t id;
    bool stream;
    bool looping;
} Sample;

typedef struct {
    const Sample *sample;
    uint64_t position; /* Source position measured in 1/32000 of a sample frame. */
    float left, right;
    bool looping;
} Voice;

static Sample sSamples[MAX_SAMPLES];
static Voice sVoices[MAX_VOICES];
static uint32_t sNextId = 1;
static SequenceOverride sSequenceOverrides[MAX_SEQUENCE_OVERRIDES];
static uint64_t sSequenceOverrideOrder;

/* Implemented by the audio engine. It swaps any currently playing borrowed
 * sequence data to the next active override or the built-in sequence. */
void audio_mod_sequence_owner_unload(const void *owner);
void audio_mod_sequence_set_default_volume(uint32_t sequence_id, uint8_t volume);
void audio_mod_sequence_reset_default_volume(uint32_t sequence_id);

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

/* Decode bounded MP3 frames into the same native mixer representation as Ogg
 * samples. This is intentionally not a streaming decoder: encoded size,
 * decoded duration, per-mod PCM, and global PCM budgets remain bounded. */
static bool decode_mp3(Sample *sample, const uint8_t *data, size_t size, size_t budget) {
    return mod_audio_decode_mp3(data, size, budget, MAX_SECONDS, &sample->pcm,
                                &sample->frames, &sample->rate, &sample->channels);
}

static uint32_t mod_audio_load_internal(const void *owner, const char *root, const char *path,
                                        bool stream, const char **error) {
    size_t length = strlen(path);
    bool is_ogg = length >= 4 && !strcasecmp(path + length - 4, ".ogg");
    bool is_mp3 = length >= 4 && !strcasecmp(path + length - 4, ".mp3");
    if (length > 240 || (stream ? (!is_ogg && !is_mp3) : !is_ogg)) {
        *error = stream ? "only mod-local Ogg Vorbis or MP3 streams (.ogg/.mp3) are supported"
                        : "only mod-local Ogg Vorbis samples (.ogg) are supported";
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
            if (!strcmp(sample->path, path) && sample->stream == stream) return sample->id;
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
    bool decoded = is_mp3 ? decode_mp3(slot, data, size, budget)
                          : decode_sample(slot, data, size, budget);
    free(data);
    if (!decoded) {
        *error = is_mp3 ? "invalid MP3 stream or decoded channel, rate, duration or memory limit exceeded"
                        : "invalid Ogg Vorbis sample or decoded channel, rate, duration or memory limit exceeded";
        return 0;
    }
    memcpy(slot->path, path, length + 1);
    slot->owner = owner;
    slot->id = sNextId++;
    slot->stream = stream;
    if (!sNextId) sNextId = 1;
    return slot->id;
}

uint32_t mod_audio_load(const void *owner, const char *root, const char *path, const char **error) {
    return mod_audio_load_internal(owner, root, path, false, error);
}

uint32_t mod_audio_load_stream(const void *owner, const char *root, const char *path, const char **error) {
    return mod_audio_load_internal(owner, root, path, true, error);
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

bool mod_audio_stream_play(const void *owner, uint32_t id, bool restart, float volume, const char **error) {
    Sample *sample = find_sample(owner, id);
    if (!sample || !sample->stream) {
        *error = "stream is stale or belongs to another mod";
        return false;
    }
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (sVoices[i].sample != sample) continue;
        /* CoopDX's second argument is restart, independent of loop mode. */
        if (restart) sVoices[i].position = 0;
        sVoices[i].left = sVoices[i].right = volume;
        return true;
    }
    for (size_t i = 0; i < MAX_VOICES; ++i) {
        if (sVoices[i].sample) continue;
        sVoices[i] = (Voice){ sample, 0, volume, volume, sample->looping };
        return true;
    }
    *error = "mod audio voice capacity exhausted";
    return false;
}

bool mod_audio_stream_set_looping(const void *owner, uint32_t id, bool looping) {
    Sample *sample = find_sample(owner, id);
    if (!sample || !sample->stream) return false;
    sample->looping = looping;
    for (size_t i = 0; i < MAX_VOICES; ++i)
        if (sVoices[i].sample == sample) sVoices[i].looping = looping;
    return true;
}

bool mod_audio_stream_stop(const void *owner, uint32_t id) {
    Sample *sample = find_sample(owner, id);
    if (!sample || !sample->stream) return false;
    for (size_t i = 0; i < MAX_VOICES; ++i)
        if (sVoices[i].sample == sample) sVoices[i] = (Voice){0};
    return true;
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

bool mod_audio_sequence_override_load(const void *owner, uint8_t sequence_id, uint8_t bank_id,
                                      uint8_t default_volume, const char *root,
                                      const char *path, const char **error) {
    if (!owner || !root || !path || !error || bank_id >= 64 || strlen(path) > 240) {
        if (error) *error = "invalid sequence override owner, bank, or path";
        return false;
    }
    SequenceOverride *slot = NULL;
    size_t total = 0;
    for (size_t i = 0; i < MAX_SEQUENCE_OVERRIDES; ++i) {
        SequenceOverride *entry = &sSequenceOverrides[i];
        if (entry->data) total += entry->size;
        if (entry->owner == owner && entry->sequence_id == sequence_id) {
            slot = entry;
            break;
        }
        if (!slot && !entry->owner) slot = entry;
    }
    if (!slot || (slot->owner != owner && total >= MAX_SEQUENCE_OVERRIDE_TOTAL_BYTES)) {
        *error = "sequence override capacity exhausted";
        return false;
    }
    size_t size = 0;
    uint8_t *data = mod_resource_read(root, path, MAX_SEQUENCE_OVERRIDE_BYTES, &size);
    if (!data || !size) {
        free(data);
        *error = "M64 sequence is missing, empty, or exceeds the 1 MiB limit";
        return false;
    }
    size_t prior_size = slot->data ? slot->size : 0;
    if (total - prior_size > MAX_SEQUENCE_OVERRIDE_TOTAL_BYTES - size) {
        free(data);
        *error = "global M64 sequence memory limit exceeded";
        return false;
    }
    if (slot->owner == owner && slot->active) {
        /* A playing sequence borrows the old buffer. Redirect it before a
         * repeated Lua registration replaces that buffer. */
        slot->active = false;
        audio_mod_sequence_owner_unload(owner);
    }
    free(slot->data);
    *slot = (SequenceOverride){ owner, sequence_id, bank_id, default_volume,
                                data, size, ++sSequenceOverrideOrder, true };
    if (!sSequenceOverrideOrder) sSequenceOverrideOrder = 1;
    return true;
}

bool mod_audio_sequence_override_lookup(uint32_t sequence_id, uint8_t *bank_id,
                                        uint8_t *default_volume, const uint8_t **data,
                                        size_t *size, const void **owner) {
    if (sequence_id > UINT8_MAX || !bank_id || !default_volume || !data || !size || !owner)
        return false;
    SequenceOverride *best = NULL;
    for (size_t i = 0; i < MAX_SEQUENCE_OVERRIDES; ++i) {
        SequenceOverride *entry = &sSequenceOverrides[i];
        if (!entry->active || !entry->data || entry->sequence_id != sequence_id) continue;
        if (!best || entry->order > best->order) best = entry;
    }
    if (!best) return false;
    *bank_id = best->bank_id;
    *default_volume = best->default_volume;
    *data = best->data;
    *size = best->size;
    *owner = best->owner;
    return true;
}

void mod_audio_sequence_override_unload(const void *owner) {
    if (!owner) return;
    bool found = false;
    for (size_t i = 0; i < MAX_SEQUENCE_OVERRIDES; ++i) {
        if (sSequenceOverrides[i].owner != owner) continue;
        found = true;
        sSequenceOverrides[i].active = false;
    }
    if (!found) return;
    for (size_t i = 0; i < MAX_SEQUENCE_OVERRIDES; ++i) {
        if (sSequenceOverrides[i].owner != owner) continue;
        uint8_t sequence_id = sSequenceOverrides[i].sequence_id;
        bool already_updated = false;
        for (size_t earlier = 0; earlier < i; ++earlier)
            if (sSequenceOverrides[earlier].owner == owner &&
                sSequenceOverrides[earlier].sequence_id == sequence_id) {
                already_updated = true;
                break;
            }
        if (already_updated) continue;
        uint8_t bank_id, volume;
        const uint8_t *data;
        size_t size;
        const void *fallback_owner;
        if (mod_audio_sequence_override_lookup(sequence_id, &bank_id, &volume,
                &data, &size, &fallback_owner))
            audio_mod_sequence_set_default_volume(sequence_id, volume);
        else
            audio_mod_sequence_reset_default_volume(sequence_id);
    }
    /* The audio thread's sequence VM borrows this memory. First redirect any
     * player using it, then release buffers and owner slots. */
    audio_mod_sequence_owner_unload(owner);
    for (size_t i = 0; i < MAX_SEQUENCE_OVERRIDES; ++i) {
        if (sSequenceOverrides[i].owner != owner) continue;
        free(sSequenceOverrides[i].data);
        sSequenceOverrides[i] = (SequenceOverride){0};
    }
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

void mod_audio_mix(int16_t *stereo, size_t frames, float sfx_gain, float music_gain) {
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
            float gain = sample->stream ? music_gain : sfx_gain;
            left += gain * voice->left * (a[0] + (b[0] - a[0]) * t);
            right += gain * voice->right * (a[right_channel] + (b[right_channel] - a[right_channel]) * t);
            voice->position += sample->rate;
            if (voice->position >= (uint64_t)sample->frames * OUTPUT_RATE) {
                if (voice->looping) voice->position = 0;
                else voice->sample = NULL;
            }
        }
        stereo[2 * f] = saturated_sample(stereo[2 * f] + left);
        stereo[2 * f + 1] = saturated_sample(stereo[2 * f + 1] + right);
    }
}
