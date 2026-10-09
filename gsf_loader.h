#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct viogsf_s viogsf_t;

// Resolves _lib filenames from PSF tags to file bytes.
typedef struct {
    // Returns 0 on success with malloc'd *out_data and *out_size set.
    // The buffer is released with free_file (or free() when free_file is NULL).
    int (*read_file)(const char *name, uint8_t **out_data, size_t *out_size, void *ctx);
    void (*free_file)(uint8_t *data, void *ctx);
    void *ctx;
} viogsf_reader_t;

// Opens a .gsf/.minigsf image; the _lib chain is resolved through reader
// (NULL reader means no _lib files). sample_rate is the output rate in Hz.
viogsf_t *viogsf_open(const uint8_t *data, size_t size,
                      const viogsf_reader_t *reader, long sample_rate);
// Renders interleaved signed 16-bit stereo; returns frames written.
int viogsf_render(viogsf_t *player, int16_t *out_stereo, int frames);
// Re-inits the CPU from the kept ROM image (cheap seek).
void viogsf_reset(viogsf_t *player);
void viogsf_close(viogsf_t *player);

// Channel-scope taps; see viogsf_scope.h.
typedef void (*viogsf_scope_cb_t)(int voice, const short *samples, int frames, void *user);
void viogsf_set_scope_callback(viogsf_t *player, viogsf_scope_cb_t callback, void *user);
void viogsf_set_voice_mute(viogsf_t *player, int voice, int muted);
void viogsf_clear_voice_mutes(viogsf_t *player);

// Audible per-voice mute in the emulated stereo mix. Unlike the scope
// mutes above, this changes what is rendered. Voices are numbered as in
// viogsf_scope.h.
void viogsf_set_voice_audible_mute(viogsf_t *player, int voice, int muted);
void viogsf_clear_voice_audible_mutes(viogsf_t *player);

// Toggles PCM interpolation (default on) and the PCM low-pass filter
// level 0..1 (default 0.5). Both apply live during rendering.
void viogsf_set_interpolation(viogsf_t *player, int enabled);
void viogsf_set_filtering(viogsf_t *player, float level);

// Renders the next frames of each voice into six mono buffers (Square 1,
// Square 2, Wave, Noise, PCM FIFO A, PCM FIFO B), post-volume 16-bit. The
// stereo mix advances identically and is discarded. Installed scope taps
// keep firing; scope mutes do not apply to stems. Returns frames written.
int viogsf_render_voices(viogsf_t *player, int16_t *out_voices[6], int frames);

#ifdef __cplusplus
}
#endif
