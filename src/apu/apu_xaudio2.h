/**
 * XAudio2 Audio Output Backend
 */
#ifndef APU_XAUDIO2_H
#define APU_XAUDIO2_H

#include <stdint.h>

/* Initialize XAudio2. Returns 1 on success, 0 on failure. */
int xa2_init(void);

/* Shut down XAudio2 and release all resources. */
void xa2_shutdown(void);

/* Returns 1 if XAudio2 is active. */
int xa2_is_active(void);

/* Submit interleaved stereo 16-bit samples. Returns 1 if accepted. */
int xa2_submit_samples(const int16_t *samples, int num_samples);

/* Get the preferred buffer size in samples. */
int xa2_get_buffer_size(void);

/* Frames waiting in the output ring, or -1 if this backend has no ring the
 * APU can pace itself on. */
int xa2_ring_level(void);

/* Milliseconds since the device last pulled from the ring, or -1. */
int xa2_ms_since_pull(void);

/* The ring as the device spends it: the frames waiting, plus what is left
 * of the device's last pull at 48 kHz since it was made -- a level that
 * falls a frame at a time instead of five hundred at once at each pull, for
 * the APU to keep topped up a frame at a time (apu_core.c, throttle). -1 as
 * xa2_ring_level. */
int xa2_ring_level_smooth(void);

#endif /* APU_XAUDIO2_H */
