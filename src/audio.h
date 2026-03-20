#ifndef AUDIO_H
#define AUDIO_H

#include "ksynth.h"

int audio_init(int samplerate, int channels, int frames);
void audio_shutdown(void);
void audio_start(void);
void audio_stop(void);
void audio_set_volume(float vol);
float audio_get_volume(void);
void audio_render(float *buffer, int frames, int channels);

#endif
