#include "audio.h"

#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ma_device device;
static float global_volume = 1.0f;
static int audio_ready = 0;

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    int channels = (int)pDevice->playback.channels;

    (void)pInput;
    if (!pOutput) {
        return;
    }

    audio_render((float*)pOutput, (int)frameCount, channels);
}

int audio_init(int samplerate, int channels, int frames) {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);

    ksynth_engine_init(samplerate);
    config.playback.format = ma_format_f32;
    config.playback.channels = (ma_uint32)channels;
    config.sampleRate = (ma_uint32)samplerate;
    config.periodSizeInFrames = (ma_uint32)frames;
    config.dataCallback = data_callback;

    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return -1;
    }

    audio_ready = 1;
    return 0;
}

void audio_shutdown(void) {
    if (audio_ready) {
        ma_device_uninit(&device);
        audio_ready = 0;
    }
}

void audio_start(void) {
    if (audio_ready) {
        ma_device_start(&device);
    }
}

void audio_stop(void)  {
    if (audio_ready) {
        ma_device_stop(&device);
    }
}

void audio_set_volume(float vol) { global_volume = vol; }
float audio_get_volume(void) { return global_volume; }

void audio_render(float *buffer, int frames, int channels) {
    int i;
    float *tmp_lr;

    if (!buffer || frames <= 0 || channels <= 0) {
        return;
    }

    tmp_lr = malloc((size_t)frames * 2u * sizeof(float));
    if (!tmp_lr) {
        memset(buffer, 0, (size_t)frames * (size_t)channels * sizeof(float));
        return;
    }

    ksynth_engine_render_stereo(tmp_lr, frames);
    for (i = 0; i < frames; i++) {
        float l = tmp_lr[i * 2 + 0] * global_volume;
        float r = tmp_lr[i * 2 + 1] * global_volume;
        if (channels == 1) {
            buffer[i] = 0.5f * (l + r);
        } else {
            buffer[i * channels + 0] = l;
            buffer[i * channels + 1] = r;
            for (int ch = 2; ch < channels; ch++) {
                buffer[i * channels + ch] = 0.5f * (l + r);
            }
        }
    }
    free(tmp_lr);
}
