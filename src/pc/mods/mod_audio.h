#ifndef SM64IOS_MOD_AUDIO_H
#define SM64IOS_MOD_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All operations run on the game thread, before SDL copies the mixed buffer. */
uint32_t mod_audio_load(const void *owner, const char *root, const char *path, const char **error);
uint32_t mod_audio_load_stream(const void *owner, const char *root, const char *path, const char **error);
bool mod_audio_play(const void *owner, uint32_t id, float volume, float pan, const char **error);
bool mod_audio_stream_play(const void *owner, uint32_t id, bool restart, float volume, const char **error);
bool mod_audio_stream_set_looping(const void *owner, uint32_t id, bool looping);
bool mod_audio_stream_stop(const void *owner, uint32_t id);
bool mod_audio_stop(const void *owner, uint32_t id);
bool mod_audio_destroy(const void *owner, uint32_t id);
bool mod_audio_sequence_override_load(const void *owner, uint8_t sequence_id, uint8_t bank_id,
                                      uint8_t default_volume, const char *root,
                                      const char *path, const char **error);
bool mod_audio_sequence_override_lookup(uint32_t sequence_id, uint8_t *bank_id,
                                        uint8_t *default_volume, const uint8_t **data,
                                        size_t *size, const void **owner);
/* Disables and safely retires sequence data before any mod-owned bytes are freed. */
void mod_audio_sequence_override_unload(const void *owner);
void mod_audio_unload(const void *owner);
void mod_audio_mix(int16_t *stereo, size_t frames, float sfx_gain, float music_gain);

#endif
