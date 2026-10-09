#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct viogsf_s viogsf_t;

// Per-voice channel-scope tap: the GBA core reports each voice's
// post-volume mono contribution through the callback. Silence arrives as
// zeroes, never as a null pointer; frames is always positive. Voices are
// 0 Square 1, 1 Square 2, 2 Wave, 3 Noise, 4 PCM FIFO A, 5 PCM FIFO B.
enum { VIOGSF_SCOPE_VOICES = 6 };
typedef void (*viogsf_scope_cb_t)(int voice, const short *samples, int frames, void *user);
// Installs the scope callback (NULL disables the taps entirely).
void viogsf_set_scope_callback(viogsf_t *player, viogsf_scope_cb_t callback, void *user);
// Channel mutes for scope isolation; muted voices tap silence.
void viogsf_set_voice_mute(viogsf_t *player, int voice, int muted);
void viogsf_clear_voice_mutes(viogsf_t *player);

#ifdef __cplusplus
}
#endif
