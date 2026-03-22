#include "ksynth.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define KS_MAX_VOICES 16
#define KS_MAX_STAGES 8
#define KS_STEPS 16
#define KS_MAX_TABLES 4
#define KS_MAX_TABLE_LEN 2048
#define KS_MAX_SAMPLE_SLOTS 16
#define KS_MAX_SAMPLE_VOICES 8
#define KS_MAX_CHANNELS 16
#define KS_DELAY_MAX_SECONDS 4
#define KS_TWO_PI (2.0f * (float)M_PI)

typedef enum {
    KS_WAVE_SINE = 0,
    KS_WAVE_SAW,
    KS_WAVE_SQUARE,
    KS_WAVE_PULSE,
    KS_WAVE_TRIANGLE,
    KS_WAVE_TABLE
} KSWaveform;

typedef enum {
    KS_FILTER_LP = 0,
    KS_FILTER_BP = 1,
    KS_FILTER_HP = 2
} KSFilterMode;

typedef struct {
    float data[KS_MAX_TABLE_LEN];
    int length;
} WaveTable;

typedef struct {
    float *data;
    int length;
    int root_note;
} SampleSlot;

typedef struct {
    float level[KS_MAX_STAGES];
    float time[KS_MAX_STAGES];
    int stages;
    int sustain_stage;
    int release_stage;
} EnvelopeSpec;

typedef struct {
    EnvelopeSpec spec;
    int stage;
    int releasing;
    int finished;
    float value;
    float start_value;
    float target_value;
    float elapsed;
    float duration;
} Envelope;

typedef struct {
    float cutoff;
    float resonance;
    float low;
    float band;
} FilterState;

typedef struct {
    int active;
    int channel;
    int held;
    int note;
    int ch_idx;
    float base_freq;
    float target_freq;
    int glide_samples;
    float velocity;
    float phase[2];
    float detune[2];
    float mix[2];
    int table_slot[2];
    float width;
    float pan_offset;
    float pd_amount;
    float filter_env_depth;
    float pitch_env_depth;
    float pd_env_depth;
    Envelope amp_env;
    Envelope pd_env;
    Envelope pitch_env;
    FilterState filter;
    KSWaveform wave_a;
    KSWaveform wave_b;
} Voice;

typedef struct {
    int active;
    int slot;
    float position;
    float step;
    float gain;
} SampleVoice;

typedef struct {
    KSChannelMode mode;
    float glide_ms;
    float pan;
    float pan_spread;
    float pan_lfo_depth;
    float delay_send;
    unsigned char held[128];
    int stack[128];
    int stack_len;
} ChannelState;

typedef struct {
    int sample_rate;
    float master_gain;
    float bpm;
    int transport_running;
    float step_phase;
    int current_step;
    float lfo_phase;
    float lfo_rate;
    float lfo_depth;
    float default_detune[2];
    KSWaveform default_wave_a;
    KSWaveform default_wave_b;
    int default_table_slot_a;
    int default_table_slot_b;
    float default_pd_amount;
    float default_filter_cutoff_hz;
    float default_filter_resonance;
    float default_filter_keytrack;
    float default_filter_drive;
    KSFilterMode default_filter_mode;
    float default_filter_env_depth;
    float default_pitch_env_depth;
    float default_pd_env_depth;
    float amp_adsr[4];
    float pd_adsr[4];
    float pitch_adsr[4];
    WaveTable tables[KS_MAX_TABLES];
    SampleSlot samples[KS_MAX_SAMPLE_SLOTS];
    Voice voices[KS_MAX_VOICES];
    SampleVoice sample_voices[KS_MAX_SAMPLE_VOICES];
    ChannelState channels[KS_MAX_CHANNELS];
    unsigned char steps[KS_STEPS];
    float note_hz[KS_STEPS];
    EnvelopeSpec amp_spec;
    EnvelopeSpec pd_spec;
    EnvelopeSpec pitch_spec;
    float *delay_buf_l;
    float *delay_buf_r;
    int delay_len;
    int delay_write_pos;
    int delay_samples;
    float delay_time_ms;
    float delay_feedback;
    float delay_wet;
    unsigned int voice_serial;
} SynthEngine;

static SynthEngine g_engine;
static int g_engine_ready = 0;

static float clampf(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static float wrap_phase(float phase) {
    while (phase >= 1.0f) {
        phase -= 1.0f;
    }
    while (phase < 0.0f) {
        phase += 1.0f;
    }
    return phase;
}

static void pan_gains(float pan, float *gain_l, float *gain_r) {
    float p = clampf(pan, -1.0f, 1.0f);
    float angle = (p + 1.0f) * 0.25f * (float)M_PI;
    if (gain_l) {
        *gain_l = cosf(angle);
    }
    if (gain_r) {
        *gain_r = sinf(angle);
    }
}

static float midi_to_hz(int note) {
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

static float ratio_to_cents(float ratio) {
    if (ratio <= 0.0f) {
        return 0.0f;
    }
    return 1200.0f * (logf(ratio) / logf(2.0f));
}

static float ms_to_sec(float ms) {
    return clampf(ms / 1000.0f, 0.001f, 8.0f);
}

static void configure_adsr(EnvelopeSpec *spec, float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    float a;
    float d;
    float r;
    float s;

    if (!spec) {
        return;
    }

    a = ms_to_sec(attack_ms);
    d = ms_to_sec(decay_ms);
    r = ms_to_sec(release_ms);
    s = sustain_level;

    spec->stages = 8;
    spec->sustain_stage = 4;
    spec->release_stage = 5;
    spec->level[0] = 0.0f;
    spec->level[1] = 1.0f;
    spec->level[2] = s;
    spec->level[3] = s;
    spec->level[4] = s;
    spec->level[5] = s * 0.35f;
    spec->level[6] = s * 0.1f;
    spec->level[7] = 0.0f;
    spec->time[0] = 0.001f;
    spec->time[1] = a;
    spec->time[2] = d;
    spec->time[3] = 0.001f;
    spec->time[4] = 0.001f;
    spec->time[5] = clampf(r * 0.40f, 0.001f, 8.0f);
    spec->time[6] = clampf(r * 0.35f, 0.001f, 8.0f);
    spec->time[7] = clampf(r * 0.25f, 0.001f, 8.0f);
}

static void store_adsr(float out[4], float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    out[0] = attack_ms;
    out[1] = decay_ms;
    out[2] = sustain_level;
    out[3] = release_ms;
}

static void envelope_begin_stage(Envelope *env, int stage) {
    float duration;

    if (!env) {
        return;
    }

    if (stage < 0 || stage >= env->spec.stages) {
        env->finished = 1;
        env->stage = env->spec.stages;
        env->value = env->target_value;
        return;
    }

    env->stage = stage;
    env->elapsed = 0.0f;
    env->start_value = env->value;
    env->target_value = env->spec.level[stage];
    duration = env->spec.time[stage];
    env->duration = duration > 0.0001f ? duration : 0.0001f;

    if (env->spec.sustain_stage == stage && !env->releasing) {
        env->value = env->target_value;
        env->duration = 0.0f;
    }
}

static void envelope_init(Envelope *env, const EnvelopeSpec *spec) {
    memset(env, 0, sizeof(*env));
    env->spec = *spec;
    env->value = 0.0f;
    env->target_value = spec->level[0];
    env->duration = spec->time[0];
    env->finished = 0;
    envelope_begin_stage(env, 0);
}

static void envelope_release(Envelope *env) {
    if (!env || env->releasing || env->finished) {
        return;
    }
    env->releasing = 1;
    if (env->spec.release_stage >= 0 && env->spec.release_stage < env->spec.stages) {
        envelope_begin_stage(env, env->spec.release_stage);
    }
}

static float envelope_step(Envelope *env, float dt) {
    float t;

    if (!env || env->finished) {
        return 0.0f;
    }

    if (env->duration <= 0.0f) {
        return env->value;
    }

    env->elapsed += dt;
    t = clampf(env->elapsed / env->duration, 0.0f, 1.0f);
    env->value = env->start_value + (env->target_value - env->start_value) * t;

    if (t >= 1.0f) {
        if (env->spec.sustain_stage == env->stage && !env->releasing) {
            env->value = env->target_value;
            env->elapsed = 0.0f;
            return env->value;
        }

        if (env->stage + 1 >= env->spec.stages) {
            env->finished = 1;
            env->value = env->target_value;
        } else {
            envelope_begin_stage(env, env->stage + 1);
        }
    }

    return env->value;
}

static float apply_phase_distortion(float phase, float amount) {
    float split = clampf(0.5f + amount * 0.45f, 0.02f, 0.98f);
    if (phase < split) {
        return (phase / split) * 0.5f;
    }
    return 0.5f + ((phase - split) / (1.0f - split)) * 0.5f;
}

static float render_table(const WaveTable *table, float phase) {
    float pos;
    int i0;
    int i1;
    float frac;

    if (!table || table->length <= 0) {
        return 0.0f;
    }

    pos = wrap_phase(phase) * (float)table->length;
    i0 = (int)pos % table->length;
    i1 = (i0 + 1) % table->length;
    frac = pos - (float)i0;
    return table->data[i0] + (table->data[i1] - table->data[i0]) * frac;
}

static float render_waveform(KSWaveform waveform, int table_slot, float phase, float width) {
    float p = wrap_phase(phase);
    width = clampf(width, 0.05f, 0.95f);

    switch (waveform) {
        case KS_WAVE_SINE:
            return sinf(KS_TWO_PI * p);
        case KS_WAVE_SAW:
            return 2.0f * p - 1.0f;
        case KS_WAVE_SQUARE:
            return p < 0.5f ? 1.0f : -1.0f;
        case KS_WAVE_PULSE:
            return p < width ? 1.0f : -1.0f;
        case KS_WAVE_TRIANGLE:
            return 1.0f - 4.0f * fabsf(p - 0.5f);
        case KS_WAVE_TABLE:
            if (table_slot >= 0 && table_slot < KS_MAX_TABLES) {
                return render_table(&g_engine.tables[table_slot], p);
            }
            return 0.0f;
        default:
            return 0.0f;
    }
}

static float filter_process(FilterState *filter, float input, float cutoff_hz, float resonance, float sr) {
    float f;
    float q;
    float high;

    if (!filter) {
        return input;
    }

    cutoff_hz = clampf(cutoff_hz, 40.0f, sr * 0.45f);
    resonance = clampf(resonance, 0.05f, 1.2f);
    f = 2.0f * sinf((float)M_PI * cutoff_hz / sr);
    q = 2.0f - 1.9f * resonance;

    filter->low += f * filter->band;
    high = input - filter->low - q * filter->band;
    filter->band += f * high;
    return clampf(filter->low, -1.5f, 1.5f);
}

static float filter_process_mode(FilterState *filter, float input, float cutoff_hz, float resonance, float sr, KSFilterMode mode) {
    float f;
    float q;
    float high;

    if (!filter) {
        return input;
    }

    cutoff_hz = clampf(cutoff_hz, 40.0f, sr * 0.45f);
    resonance = clampf(resonance, 0.05f, 1.2f);
    f = 2.0f * sinf((float)M_PI * cutoff_hz / sr);
    q = 2.0f - 1.9f * resonance;

    filter->low += f * filter->band;
    high = input - filter->low - q * filter->band;
    filter->band += f * high;

    if (mode == KS_FILTER_HP) {
        return clampf(high, -1.5f, 1.5f);
    }
    if (mode == KS_FILTER_BP) {
        return clampf(filter->band, -1.5f, 1.5f);
    }
    return clampf(filter->low, -1.5f, 1.5f);
}

static Voice* allocate_voice(void) {
    int i;
    int quietest = 0;
    float quietest_level = 10.0f;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        if (!g_engine.voices[i].active) {
            return &g_engine.voices[i];
        }
        if (g_engine.voices[i].amp_env.value < quietest_level) {
            quietest = i;
            quietest_level = g_engine.voices[i].amp_env.value;
        }
    }

    return &g_engine.voices[quietest];
}

static void release_voice(Voice *voice) {
    if (!voice || !voice->active) {
        return;
    }
    voice->held = 0;
    envelope_release(&voice->amp_env);
    envelope_release(&voice->pd_env);
    envelope_release(&voice->pitch_env);
}

static Voice* find_mono_voice_for_channel(int channel) {
    int i;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        if (g_engine.voices[i].active && g_engine.voices[i].channel == channel) {
            return &g_engine.voices[i];
        }
    }
    return NULL;
}

static Voice* find_voice_for_channel_note(int channel, int note) {
    int i;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        if (g_engine.voices[i].active && g_engine.voices[i].channel == channel && g_engine.voices[i].note == note) {
            return &g_engine.voices[i];
        }
    }
    return NULL;
}

static void start_voice(Voice *voice, int channel, int note, float hz, float velocity) {
    ChannelState *ch;
    float spread;
    float rnd;
    int ch_idx;

    if (!voice) {
        return;
    }

    memset(voice, 0, sizeof(*voice));
    voice->active = 1;
    voice->channel = channel;
    voice->held = 1;
    voice->note = note;
    voice->base_freq = hz;
    voice->target_freq = hz;
    voice->glide_samples = 0;
    voice->velocity = velocity;
    voice->phase[0] = 0.0f;
    voice->phase[1] = 0.25f;
    voice->detune[0] = g_engine.default_detune[0];
    voice->detune[1] = g_engine.default_detune[1];
    voice->mix[0] = 0.55f;
    voice->mix[1] = 0.45f;
    voice->width = 0.32f;
    ch_idx = channel;
    if (ch_idx < 0) {
        ch_idx = 0;
    } else if (ch_idx >= KS_MAX_CHANNELS) {
        ch_idx = KS_MAX_CHANNELS - 1;
    }
    ch = &g_engine.channels[ch_idx];
    spread = clampf(ch->pan_spread, 0.0f, 1.0f);
    g_engine.voice_serial = g_engine.voice_serial * 1664525u + 1013904223u + (unsigned int)(note + channel * 17);
    rnd = (float)((g_engine.voice_serial >> 8) & 0x00FFFFFFu) / 16777215.0f;
    voice->pan_offset = (rnd * 2.0f - 1.0f) * spread;
    voice->pd_amount = g_engine.default_pd_amount;
    voice->filter.cutoff = g_engine.default_filter_cutoff_hz;
    voice->filter.resonance = g_engine.default_filter_resonance;
    voice->filter_env_depth = g_engine.default_filter_env_depth;
    voice->pitch_env_depth = g_engine.default_pitch_env_depth;
    voice->pd_env_depth = g_engine.default_pd_env_depth;
    voice->wave_a = g_engine.default_wave_a;
    voice->wave_b = g_engine.default_wave_b;
    voice->table_slot[0] = g_engine.default_table_slot_a;
    voice->table_slot[1] = g_engine.default_table_slot_b;

    envelope_init(&voice->amp_env, &g_engine.amp_spec);
    envelope_init(&voice->pd_env, &g_engine.pd_spec);
    envelope_init(&voice->pitch_env, &g_engine.pitch_spec);
}

static void set_voice_target_note(Voice *voice, int note, float velocity, float glide_ms) {
    float hz;
    int glide_samples;

    if (!voice) {
        return;
    }

    hz = midi_to_hz(note);
    voice->note = note;
    voice->velocity = velocity;
    voice->held = 1;
    voice->target_freq = hz;
    if (glide_ms <= 0.0f) {
        voice->base_freq = hz;
        voice->glide_samples = 0;
        return;
    }

    glide_samples = (int)((glide_ms / 1000.0f) * (float)g_engine.sample_rate);
    if (glide_samples < 1) {
        glide_samples = 1;
    }
    voice->glide_samples = glide_samples;
}

static void trigger_voice(int channel, float hz, float velocity, int note) {
    Voice *voice = allocate_voice();
    start_voice(voice, channel, note, hz, velocity);
}

static void release_finished_steps(void) {
    int i;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        if (g_engine.voices[i].active && !g_engine.voices[i].amp_env.releasing &&
            g_engine.voices[i].amp_env.stage >= g_engine.amp_spec.sustain_stage) {
            envelope_release(&g_engine.voices[i].amp_env);
            envelope_release(&g_engine.voices[i].pd_env);
            envelope_release(&g_engine.voices[i].pitch_env);
        }
    }
}

static void silence_all_voices(void) {
    int i;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        memset(&g_engine.voices[i], 0, sizeof(g_engine.voices[i]));
    }
    for (i = 0; i < KS_MAX_SAMPLE_VOICES; i++) {
        memset(&g_engine.sample_voices[i], 0, sizeof(g_engine.sample_voices[i]));
    }
}

static void advance_step_sequencer(float dt) {
    float steps_per_second = (g_engine.bpm / 60.0f) * 4.0f;
    float step_time = 1.0f / steps_per_second;

    g_engine.step_phase += dt;
    while (g_engine.step_phase >= step_time) {
        g_engine.step_phase -= step_time;
        g_engine.current_step = (g_engine.current_step + 1) % KS_STEPS;
        release_finished_steps();
        if (g_engine.steps[g_engine.current_step]) {
            int note = 48 + (int)g_engine.steps[g_engine.current_step];
            trigger_voice(0, g_engine.note_hz[g_engine.current_step], 0.8f, note);
        }
    }
}

static void render_voice(Voice *voice, float dt, float lfo_value, float *out_l, float *out_r, float *send_l, float *send_r) {
    float amp;
    float pd_env;
    float pitch_env;
    float freq_base;
    float pd_amount;
    float osc0;
    float osc1;
    float sample;
    float cutoff;
    float drive;
    float keyscale;
    int note;
    int ch_idx;
    float gl;
    float gr;
    float pan;
    float pan_lfo;
    float s;
    float send;
    ChannelState *ch;

    if (!voice || !voice->active) {
        return;
    }

    amp = envelope_step(&voice->amp_env, dt);
    pd_env = envelope_step(&voice->pd_env, dt);
    pitch_env = envelope_step(&voice->pitch_env, dt);

    if (voice->amp_env.finished) {
        voice->active = 0;
        return;
    }

    if (voice->glide_samples > 0) {
        voice->base_freq += (voice->target_freq - voice->base_freq) / (float)voice->glide_samples;
        voice->glide_samples--;
    } else {
        voice->base_freq = voice->target_freq;
    }

    freq_base = voice->base_freq * (1.0f + voice->pitch_env_depth * pitch_env + g_engine.lfo_depth * lfo_value);
    pd_amount = clampf(voice->pd_amount + pd_env * voice->pd_env_depth, -0.95f, 0.95f);

    voice->phase[0] = wrap_phase(voice->phase[0] + (freq_base * voice->detune[0]) / (float)g_engine.sample_rate);
    voice->phase[1] = wrap_phase(voice->phase[1] + (freq_base * voice->detune[1]) / (float)g_engine.sample_rate);

    osc0 = render_waveform(voice->wave_a, voice->table_slot[0], apply_phase_distortion(voice->phase[0], pd_amount), voice->width);
    osc1 = render_waveform(voice->wave_b, voice->table_slot[1], apply_phase_distortion(voice->phase[1], -pd_amount * 0.7f), voice->width);
    sample = osc0 * voice->mix[0] + osc1 * voice->mix[1];

    note = voice->note;
    if (note < 0) {
        note = 0;
    } else if (note > 127) {
        note = 127;
    }
    keyscale = powf(2.0f, ((float)(note - 60) / 12.0f) * g_engine.default_filter_keytrack);
    cutoff = (voice->filter.cutoff + pd_env * voice->filter_env_depth + lfo_value * 180.0f) * keyscale;

    drive = g_engine.default_filter_drive;
    if (drive > 1.0001f) {
        sample = tanhf(sample * drive) / tanhf(drive);
    }
    sample = filter_process_mode(&voice->filter, sample, cutoff, voice->filter.resonance,
                                 (float)g_engine.sample_rate, g_engine.default_filter_mode);

    s = sample * amp * voice->velocity;
    ch_idx = voice->channel;
    if (ch_idx < 0) {
        ch_idx = 0;
    } else if (ch_idx >= KS_MAX_CHANNELS) {
        ch_idx = KS_MAX_CHANNELS - 1;
    }
    ch = &g_engine.channels[ch_idx];
    pan_lfo = ch->pan_lfo_depth * lfo_value;
    pan = clampf(ch->pan + voice->pan_offset + pan_lfo, -1.0f, 1.0f);
    pan_gains(pan, &gl, &gr);

    if (out_l) {
        *out_l += s * gl;
    }
    if (out_r) {
        *out_r += s * gr;
    }

    send = clampf(ch->delay_send, 0.0f, 1.0f);
    if (send_l) {
        *send_l += s * gl * send;
    }
    if (send_r) {
        *send_r += s * gr * send;
    }
}

static SampleVoice* allocate_sample_voice(void) {
    int i;

    for (i = 0; i < KS_MAX_SAMPLE_VOICES; i++) {
        if (!g_engine.sample_voices[i].active) {
            return &g_engine.sample_voices[i];
        }
    }
    return &g_engine.sample_voices[0];
}

static float render_sample_voice(SampleVoice *voice) {
    SampleSlot *slot;
    int i0;
    int i1;
    float frac;
    float sample;

    if (!voice || !voice->active || voice->slot < 0 || voice->slot >= KS_MAX_SAMPLE_SLOTS) {
        return 0.0f;
    }

    slot = &g_engine.samples[voice->slot];
    if (!slot->data || slot->length <= 1) {
        voice->active = 0;
        return 0.0f;
    }

    if (voice->position >= (float)(slot->length - 1)) {
        voice->active = 0;
        return 0.0f;
    }

    i0 = (int)voice->position;
    i1 = i0 + 1;
    if (i1 >= slot->length) {
        i1 = slot->length - 1;
    }
    frac = voice->position - (float)i0;
    sample = slot->data[i0] + (slot->data[i1] - slot->data[i0]) * frac;
    voice->position += voice->step;

    if (voice->position >= (float)(slot->length - 1)) {
        voice->active = 0;
    }

    return sample * voice->gain;
}

static void load_default_patch(void) {
    int i;
    int ch;
    static const unsigned char pattern[KS_STEPS] = {
        12, 0, 19, 0, 15, 0, 22, 0,
        12, 0, 24, 0, 19, 0, 15, 0
    };

    memset(&g_engine, 0, sizeof(g_engine));
    g_engine.sample_rate = 44100;
    g_engine.master_gain = 0.18f;
    g_engine.bpm = 118.0f;
    g_engine.transport_running = 0;
    g_engine.current_step = KS_STEPS - 1;
    g_engine.lfo_rate = 5.2f;
    g_engine.lfo_depth = 0.012f;
    g_engine.default_detune[0] = 0.997f;
    g_engine.default_detune[1] = 1.005f;
    g_engine.default_wave_a = KS_WAVE_SAW;
    g_engine.default_wave_b = KS_WAVE_PULSE;
    g_engine.default_table_slot_a = 0;
    g_engine.default_table_slot_b = 1;
    g_engine.default_pd_amount = 0.35f;
    g_engine.default_filter_cutoff_hz = 1400.0f;
    g_engine.default_filter_resonance = 0.42f;
    g_engine.default_filter_keytrack = 0.0f;
    g_engine.default_filter_drive = 1.0f;
    g_engine.default_filter_mode = KS_FILTER_LP;
    g_engine.default_filter_env_depth = 1800.0f;
    g_engine.delay_time_ms = 320.0f;
    g_engine.delay_feedback = 0.35f;
    g_engine.delay_wet = 0.20f;
    g_engine.default_pitch_env_depth = 0.08f;
    g_engine.default_pd_env_depth = 0.60f;
    store_adsr(g_engine.amp_adsr, 10.0f, 60.0f, 0.72f, 300.0f);
    store_adsr(g_engine.pd_adsr, 20.0f, 40.0f, 0.20f, 160.0f);
    store_adsr(g_engine.pitch_adsr, 15.0f, 80.0f, -0.04f, 140.0f);
    g_engine.tables[0].length = 256;
    g_engine.tables[1].length = 256;
    for (i = 0; i < 256; i++) {
        float phase = (float)i / 256.0f;
        g_engine.tables[0].data[i] = sinf(KS_TWO_PI * phase);
        g_engine.tables[1].data[i] = 2.0f * phase - 1.0f;
    }

    g_engine.amp_spec.stages = 8;
    g_engine.amp_spec.sustain_stage = 4;
    g_engine.amp_spec.release_stage = 5;
    g_engine.amp_spec.level[0] = 0.0f;
    g_engine.amp_spec.level[1] = 1.0f;
    g_engine.amp_spec.level[2] = 0.65f;
    g_engine.amp_spec.level[3] = 0.8f;
    g_engine.amp_spec.level[4] = 0.72f;
    g_engine.amp_spec.level[5] = 0.45f;
    g_engine.amp_spec.level[6] = 0.18f;
    g_engine.amp_spec.level[7] = 0.0f;
    g_engine.amp_spec.time[0] = 0.001f;
    g_engine.amp_spec.time[1] = 0.01f;
    g_engine.amp_spec.time[2] = 0.06f;
    g_engine.amp_spec.time[3] = 0.05f;
    g_engine.amp_spec.time[4] = 0.2f;
    g_engine.amp_spec.time[5] = 0.12f;
    g_engine.amp_spec.time[6] = 0.18f;
    g_engine.amp_spec.time[7] = 0.25f;

    g_engine.pd_spec.stages = 8;
    g_engine.pd_spec.sustain_stage = 3;
    g_engine.pd_spec.release_stage = 4;
    g_engine.pd_spec.level[0] = 0.0f;
    g_engine.pd_spec.level[1] = 0.9f;
    g_engine.pd_spec.level[2] = 0.4f;
    g_engine.pd_spec.level[3] = 0.2f;
    g_engine.pd_spec.level[4] = 0.15f;
    g_engine.pd_spec.level[5] = 0.08f;
    g_engine.pd_spec.level[6] = 0.02f;
    g_engine.pd_spec.level[7] = 0.0f;
    g_engine.pd_spec.time[0] = 0.001f;
    g_engine.pd_spec.time[1] = 0.02f;
    g_engine.pd_spec.time[2] = 0.04f;
    g_engine.pd_spec.time[3] = 0.1f;
    g_engine.pd_spec.time[4] = 0.12f;
    g_engine.pd_spec.time[5] = 0.15f;
    g_engine.pd_spec.time[6] = 0.16f;
    g_engine.pd_spec.time[7] = 0.18f;

    g_engine.pitch_spec.stages = 8;
    g_engine.pitch_spec.sustain_stage = 2;
    g_engine.pitch_spec.release_stage = 3;
    g_engine.pitch_spec.level[0] = 0.35f;
    g_engine.pitch_spec.level[1] = 0.0f;
    g_engine.pitch_spec.level[2] = -0.04f;
    g_engine.pitch_spec.level[3] = -0.08f;
    g_engine.pitch_spec.level[4] = -0.03f;
    g_engine.pitch_spec.level[5] = 0.0f;
    g_engine.pitch_spec.level[6] = 0.0f;
    g_engine.pitch_spec.level[7] = 0.0f;
    g_engine.pitch_spec.time[0] = 0.001f;
    g_engine.pitch_spec.time[1] = 0.015f;
    g_engine.pitch_spec.time[2] = 0.08f;
    g_engine.pitch_spec.time[3] = 0.09f;
    g_engine.pitch_spec.time[4] = 0.08f;
    g_engine.pitch_spec.time[5] = 0.05f;
    g_engine.pitch_spec.time[6] = 0.05f;
    g_engine.pitch_spec.time[7] = 0.05f;

    for (ch = 0; ch < KS_MAX_CHANNELS; ch++) {
        g_engine.channels[ch].mode = KS_CHANNEL_POLY;
        g_engine.channels[ch].glide_ms = 0.0f;
        g_engine.channels[ch].pan = 0.0f;
        g_engine.channels[ch].pan_spread = 0.0f;
        g_engine.channels[ch].pan_lfo_depth = 0.0f;
        g_engine.channels[ch].delay_send = 0.0f;
        g_engine.channels[ch].stack_len = 0;
        memset(g_engine.channels[ch].held, 0, sizeof(g_engine.channels[ch].held));
    }

    for (i = 0; i < KS_STEPS; i++) {
        int note = 48 + pattern[i];
        g_engine.steps[i] = pattern[i];
        g_engine.note_hz[i] = midi_to_hz(note);
    }
}

void ksynth_engine_init(int sample_rate) {
    float *old_delay_l = g_engine.delay_buf_l;
    float *old_delay_r = g_engine.delay_buf_r;
    int delay_len;
    int delay_samples;

    if (old_delay_l) {
        free(old_delay_l);
    }
    if (old_delay_r) {
        free(old_delay_r);
    }
    load_default_patch();
    if (sample_rate > 0) {
        g_engine.sample_rate = sample_rate;
    }
    delay_len = g_engine.sample_rate * KS_DELAY_MAX_SECONDS;
    if (delay_len < 1) {
        delay_len = 1;
    }
    g_engine.delay_buf_l = calloc((size_t)delay_len, sizeof(float));
    g_engine.delay_buf_r = calloc((size_t)delay_len, sizeof(float));
    g_engine.delay_len = delay_len;
    g_engine.delay_write_pos = 0;
    delay_samples = (int)((g_engine.delay_time_ms / 1000.0f) * (float)g_engine.sample_rate);
    if (delay_samples < 1) {
        delay_samples = 1;
    } else if (delay_samples >= g_engine.delay_len) {
        delay_samples = g_engine.delay_len - 1;
    }
    g_engine.delay_samples = delay_samples;
    g_engine_ready = 1;
}

void ksynth_engine_render_stereo(float *out_lr, int frames) {
    int i;
    int v;
    float dt;

    if (!out_lr || frames <= 0) {
        return;
    }

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    memset(out_lr, 0, (size_t)frames * 2u * sizeof(float));
    dt = 1.0f / (float)g_engine.sample_rate;

    for (i = 0; i < frames; i++) {
        float dry_l = 0.0f;
        float dry_r = 0.0f;
        float send_l = 0.0f;
        float send_r = 0.0f;
        float wet_l = 0.0f;
        float wet_r = 0.0f;
        float lfo_value;

        if (g_engine.transport_running) {
            advance_step_sequencer(dt);
        }
        g_engine.lfo_phase = wrap_phase(g_engine.lfo_phase + g_engine.lfo_rate * dt);
        lfo_value = sinf(g_engine.lfo_phase * KS_TWO_PI);

        for (v = 0; v < KS_MAX_VOICES; v++) {
            render_voice(&g_engine.voices[v], dt, lfo_value, &dry_l, &dry_r, &send_l, &send_r);
        }
        for (v = 0; v < KS_MAX_SAMPLE_VOICES; v++) {
            float s = render_sample_voice(&g_engine.sample_voices[v]);
            dry_l += s;
            dry_r += s;
            send_l += s * g_engine.channels[0].delay_send;
            send_r += s * g_engine.channels[0].delay_send;
        }

        if (g_engine.delay_buf_l && g_engine.delay_buf_r && g_engine.delay_len > 0) {
            int read_pos = g_engine.delay_write_pos - g_engine.delay_samples;
            if (read_pos < 0) {
                read_pos += g_engine.delay_len;
            }
            wet_l = g_engine.delay_buf_l[read_pos];
            wet_r = g_engine.delay_buf_r[read_pos];
            g_engine.delay_buf_l[g_engine.delay_write_pos] = send_l + wet_l * g_engine.delay_feedback;
            g_engine.delay_buf_r[g_engine.delay_write_pos] = send_r + wet_r * g_engine.delay_feedback;
            g_engine.delay_write_pos++;
            if (g_engine.delay_write_pos >= g_engine.delay_len) {
                g_engine.delay_write_pos = 0;
            }
        }

        dry_l += wet_l * g_engine.delay_wet;
        dry_r += wet_r * g_engine.delay_wet;
        out_lr[i * 2 + 0] = clampf(dry_l * g_engine.master_gain, -0.95f, 0.95f);
        out_lr[i * 2 + 1] = clampf(dry_r * g_engine.master_gain, -0.95f, 0.95f);
    }
}

void ksynth_engine_render(float *out, int frames) {
    int i;
    float *tmp;

    if (!out || frames <= 0) {
        return;
    }

    tmp = malloc((size_t)frames * 2u * sizeof(float));
    if (!tmp) {
        memset(out, 0, (size_t)frames * sizeof(float));
        return;
    }
    ksynth_engine_render_stereo(tmp, frames);
    for (i = 0; i < frames; i++) {
        out[i] = 0.5f * (tmp[i * 2 + 0] + tmp[i * 2 + 1]);
    }
    free(tmp);
}

void ksynth_engine_set_bpm(float bpm) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.bpm = clampf(bpm, 24.0f, 320.0f);
}

void ksynth_engine_set_gain(float gain) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.master_gain = clampf(gain, 0.0f, 16.0f);
}

void ksynth_engine_set_step(int index, int semitone) {
    int note;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (index < 0 || index >= KS_STEPS) {
        return;
    }

    if (semitone <= 0) {
        g_engine.steps[index] = 0;
        g_engine.note_hz[index] = 0.0f;
        return;
    }

    if (semitone > 48) {
        semitone = 48;
    }
    note = 48 + semitone;
    g_engine.steps[index] = (unsigned char)semitone;
    g_engine.note_hz[index] = midi_to_hz(note);
}

void ksynth_engine_set_waveforms(int osc_a, int osc_b) {
    int i;
    KSWaveform wave_a;
    KSWaveform wave_b;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    wave_a = (KSWaveform)((osc_a < 0 ? 0 : osc_a) % 5);
    wave_b = (KSWaveform)((osc_b < 0 ? 0 : osc_b) % 5);
    g_engine.default_wave_a = wave_a;
    g_engine.default_wave_b = wave_b;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].wave_a = wave_a;
        g_engine.voices[i].wave_b = wave_b;
    }
}

void ksynth_engine_set_lfo(float rate_hz, float depth) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.lfo_rate = clampf(rate_hz, 0.0f, 1760.0f);
    g_engine.lfo_depth = clampf(depth, 0.0f, 10.0f);
}

void ksynth_engine_set_detune(float cents_a, float cents_b) {
    int i;
    float ca;
    float cb;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

#define DETUNE_LIMIT (4800.f)
    ca = clampf(cents_a, -(DETUNE_LIMIT), (DETUNE_LIMIT));
    cb = clampf(cents_b, -(DETUNE_LIMIT), (DETUNE_LIMIT));
    g_engine.default_detune[0] = powf(2.0f, ca / 1200.0f);
    g_engine.default_detune[1] = powf(2.0f, cb / 1200.0f);
    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].detune[0] = g_engine.default_detune[0];
        g_engine.voices[i].detune[1] = g_engine.default_detune[1];
    }
}

void ksynth_engine_set_pd(float amount) {
    int i;
    float clamped;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    clamped = clampf(amount, -0.95f, 0.95f);
    g_engine.default_pd_amount = clamped;
    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].pd_amount = clamped;
    }
}

void ksynth_engine_set_filter(float cutoff_hz, float resonance) {
    int i;
    float c;
    float r;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    c = clampf(cutoff_hz, 40.0f, (float)g_engine.sample_rate * 0.45f);
    r = clampf(resonance, 0.05f, 1.2f);
    g_engine.default_filter_cutoff_hz = c;
    g_engine.default_filter_resonance = r;
    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].filter.cutoff = c;
        g_engine.voices[i].filter.resonance = r;
    }
}

void ksynth_engine_set_filter_cutoff(float cutoff_hz) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    ksynth_engine_set_filter(cutoff_hz, g_engine.default_filter_resonance);
}

void ksynth_engine_set_filter_resonance(float resonance) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    ksynth_engine_set_filter(g_engine.default_filter_cutoff_hz, resonance);
}

void ksynth_engine_set_filter_keytrack(float keytrack) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.default_filter_keytrack = clampf(keytrack, 0.0f, 1.5f);
}

void ksynth_engine_set_filter_mode(int mode) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (mode < 0 || mode > 2) {
        mode = 0;
    }
    g_engine.default_filter_mode = (KSFilterMode)mode;
}

void ksynth_engine_set_filter_drive(float drive) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.default_filter_drive = clampf(drive, 0.1f, 12.0f);
}

void ksynth_engine_set_filter_env_depth(float amount) {
    int i;
    float v;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    v = clampf(amount, -8000.0f, 8000.0f);
    g_engine.default_filter_env_depth = v;
    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].filter_env_depth = v;
    }
}

void ksynth_engine_set_pitch_env_depth(float amount) {
    int i;
    float v;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    v = clampf(amount, -1.0f, 1.0f);
    g_engine.default_pitch_env_depth = v;
    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].pitch_env_depth = v;
    }
}

void ksynth_engine_set_pd_env_depth(float amount) {
    int i;
    float v;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    v = clampf(amount, -2.0f, 2.0f);
    g_engine.default_pd_env_depth = v;
    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].pd_env_depth = v;
    }
}

void ksynth_engine_set_amp_adsr(float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    store_adsr(g_engine.amp_adsr, attack_ms, decay_ms, clampf(sustain_level, 0.0f, 1.2f), release_ms);
    configure_adsr(&g_engine.amp_spec, attack_ms, decay_ms, clampf(sustain_level, 0.0f, 1.2f), release_ms);
}

void ksynth_engine_set_pd_adsr(float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    store_adsr(g_engine.pd_adsr, attack_ms, decay_ms, clampf(sustain_level, 0.0f, 1.2f), release_ms);
    configure_adsr(&g_engine.pd_spec, attack_ms, decay_ms, clampf(sustain_level, 0.0f, 1.2f), release_ms);
}

void ksynth_engine_set_pitch_adsr(float attack_ms, float decay_ms, float sustain_level, float release_ms) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    store_adsr(g_engine.pitch_adsr, attack_ms, decay_ms, clampf(sustain_level, -1.0f, 1.0f), release_ms);
    configure_adsr(&g_engine.pitch_spec, attack_ms, decay_ms, clampf(sustain_level, -1.0f, 1.0f), release_ms);
}

void ksynth_engine_get_mod_state(KSEngineModState *out_state) {
    if (!out_state) {
        return;
    }
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    out_state->lfo_rate_hz = g_engine.lfo_rate;
    out_state->lfo_depth = g_engine.lfo_depth;
    out_state->pd_amount = g_engine.default_pd_amount;
    out_state->detune_a_cents = ratio_to_cents(g_engine.default_detune[0]);
    out_state->detune_b_cents = ratio_to_cents(g_engine.default_detune[1]);
    out_state->filter_cutoff_hz = g_engine.default_filter_cutoff_hz;
    out_state->filter_resonance = g_engine.default_filter_resonance;
    out_state->filter_keytrack = g_engine.default_filter_keytrack;
    out_state->filter_drive = g_engine.default_filter_drive;
    out_state->filter_mode = (int)g_engine.default_filter_mode;
    out_state->delay_time_ms = g_engine.delay_time_ms;
    out_state->delay_feedback = g_engine.delay_feedback;
    out_state->delay_wet = g_engine.delay_wet;
    out_state->channel0_pan = g_engine.channels[0].pan;
    out_state->channel0_pan_spread = g_engine.channels[0].pan_spread;
    out_state->channel0_pan_lfo_depth = g_engine.channels[0].pan_lfo_depth;
    out_state->channel0_delay_send = g_engine.channels[0].delay_send;
    out_state->filter_env_depth = g_engine.default_filter_env_depth;
    out_state->pitch_env_depth = g_engine.default_pitch_env_depth;
    out_state->pd_env_depth = g_engine.default_pd_env_depth;
    out_state->amp_attack_ms = g_engine.amp_adsr[0];
    out_state->amp_decay_ms = g_engine.amp_adsr[1];
    out_state->amp_sustain = g_engine.amp_adsr[2];
    out_state->amp_release_ms = g_engine.amp_adsr[3];
    out_state->pd_attack_ms = g_engine.pd_adsr[0];
    out_state->pd_decay_ms = g_engine.pd_adsr[1];
    out_state->pd_sustain = g_engine.pd_adsr[2];
    out_state->pd_release_ms = g_engine.pd_adsr[3];
    out_state->pitch_attack_ms = g_engine.pitch_adsr[0];
    out_state->pitch_decay_ms = g_engine.pitch_adsr[1];
    out_state->pitch_sustain = g_engine.pitch_adsr[2];
    out_state->pitch_release_ms = g_engine.pitch_adsr[3];
}

static void channel_stack_remove(ChannelState *ch, int note) {
    int i;
    int j;

    if (!ch) {
        return;
    }
    for (i = 0; i < ch->stack_len; i++) {
        if (ch->stack[i] == note) {
            for (j = i; j < ch->stack_len - 1; j++) {
                ch->stack[j] = ch->stack[j + 1];
            }
            ch->stack_len--;
            return;
        }
    }
}

static void channel_stack_push(ChannelState *ch, int note) {
    if (!ch) {
        return;
    }
    channel_stack_remove(ch, note);
    if (ch->stack_len < (int)(sizeof(ch->stack) / sizeof(ch->stack[0]))) {
        ch->stack[ch->stack_len++] = note;
    }
}

static int normalize_channel(int channel) {
    if (channel < 0) {
        return 0;
    }
    if (channel >= KS_MAX_CHANNELS) {
        return KS_MAX_CHANNELS - 1;
    }
    return channel;
}

void ksynth_engine_get_channel_state(int channel, KSChannelStateSnapshot *out_state) {
    ChannelState *ch;
    int i;

    if (!out_state) {
        return;
    }
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    channel = normalize_channel(channel);
    ch = &g_engine.channels[channel];
    out_state->mode = ch->mode;
    out_state->glide_ms = ch->glide_ms;
    out_state->held_count = 0;
    out_state->stack_len = ch->stack_len;
    out_state->top_note = ch->stack_len > 0 ? ch->stack[ch->stack_len - 1] : -1;
    out_state->active_voice_count = 0;

    for (i = 0; i < 128; i++) {
        if (ch->held[i]) {
            out_state->held_count++;
        }
    }
    for (i = 0; i < KS_MAX_VOICES; i++) {
        if (g_engine.voices[i].active && g_engine.voices[i].channel == channel) {
            out_state->active_voice_count++;
        }
    }
}

void ksynth_engine_set_channel_mode(int channel, KSChannelMode mode) {
    channel = normalize_channel(channel);
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.channels[channel].mode = mode == KS_CHANNEL_MONO ? KS_CHANNEL_MONO : KS_CHANNEL_POLY;
}

void ksynth_engine_set_channel_glide_ms(int channel, float glide_ms) {
    channel = normalize_channel(channel);
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.channels[channel].glide_ms = clampf(glide_ms, 0.0f, 5000.0f);
}

void ksynth_engine_set_channel_pan(int channel, float pan) {
    channel = normalize_channel(channel);
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.channels[channel].pan = clampf(pan, -1.0f, 1.0f);
}

void ksynth_engine_set_channel_pan_spread(int channel, float spread) {
    channel = normalize_channel(channel);
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.channels[channel].pan_spread = clampf(spread, 0.0f, 1.0f);
}

void ksynth_engine_set_channel_pan_lfo_depth(int channel, float depth) {
    channel = normalize_channel(channel);
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.channels[channel].pan_lfo_depth = clampf(depth, 0.0f, 10.0f);
}

void ksynth_engine_set_channel_delay_send(int channel, float send) {
    channel = normalize_channel(channel);
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.channels[channel].delay_send = clampf(send, 0.0f, 1.0f);
}

void ksynth_engine_set_delay(float time_ms, float feedback, float wet) {
    int delay_samples;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (g_engine.delay_len < 2) {
        return;
    }
    g_engine.delay_time_ms = clampf(time_ms, 1.0f, (float)(KS_DELAY_MAX_SECONDS * 1000));
    g_engine.delay_feedback = clampf(feedback, 0.0f, 0.98f);
    g_engine.delay_wet = clampf(wet, 0.0f, 1.0f);

    delay_samples = (int)((g_engine.delay_time_ms / 1000.0f) * (float)g_engine.sample_rate);
    if (delay_samples < 1) {
        delay_samples = 1;
    } else if (delay_samples >= g_engine.delay_len) {
        delay_samples = g_engine.delay_len - 1;
    }
    g_engine.delay_samples = delay_samples;
}

void ksynth_engine_note_on_ch(int channel, int note, float velocity) {
    ChannelState *ch;
    float hz;
    float vel;
    Voice *voice;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    channel = normalize_channel(channel);
    if (note < 0) {
        note = 0;
    }
    if (note > 127) {
        note = 127;
    }
    hz = midi_to_hz(note);
    vel = clampf(velocity, 0.0f, 10.0f);
    ch = &g_engine.channels[channel];

    if (ch->mode == KS_CHANNEL_MONO) {
        channel_stack_push(ch, note);
        ch->held[note] = 1;
        voice = find_mono_voice_for_channel(channel);
        if (!voice) {
            voice = allocate_voice();
            start_voice(voice, channel, note, hz, vel);
        } else {
            set_voice_target_note(voice, note, vel, ch->glide_ms);
        }
        return;
    }

    ch->held[note] = 1;
    voice = allocate_voice();
    start_voice(voice, channel, note, hz, vel);
}

void ksynth_engine_note_off_ch(int channel, int note) {
    ChannelState *ch;
    Voice *voice;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }

    channel = normalize_channel(channel);
    if (note < 0) {
        note = 0;
    }
    if (note > 127) {
        note = 127;
    }
    ch = &g_engine.channels[channel];
    ch->held[note] = 0;

    if (ch->mode == KS_CHANNEL_MONO) {
        voice = find_mono_voice_for_channel(channel);
        channel_stack_remove(ch, note);
        if (!voice) {
            return;
        }
        if (ch->stack_len > 0) {
            int next_note = ch->stack[ch->stack_len - 1];
            set_voice_target_note(voice, next_note, voice->velocity, ch->glide_ms);
            return;
        }
        release_voice(voice);
        return;
    }

    voice = find_voice_for_channel_note(channel, note);
    release_voice(voice);
}

void ksynth_engine_note_on(int note, float velocity) {
    ksynth_engine_note_on_ch(0, note, velocity);
}

void ksynth_engine_all_notes_off(void) {
    int i;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    for (i = 0; i < KS_MAX_VOICES; i++) {
        if (g_engine.voices[i].active) {
            release_voice(&g_engine.voices[i]);
        }
    }
}

void ksynth_engine_set_table(int slot, const double *data, int length) {
    int i;
    int n;
    float peak = 0.0f;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (!data || slot < 0 || slot >= KS_MAX_TABLES) {
        return;
    }

    n = length;
    if (n < 2) {
        return;
    }
    if (n > KS_MAX_TABLE_LEN) {
        n = KS_MAX_TABLE_LEN;
    }

    for (i = 0; i < n; i++) {
        float value = (float)data[i];
        g_engine.tables[slot].data[i] = value;
        if (fabsf(value) > peak) {
            peak = fabsf(value);
        }
    }
    if (peak > 0.00001f) {
        for (i = 0; i < n; i++) {
            g_engine.tables[slot].data[i] /= peak;
        }
    }
    g_engine.tables[slot].length = n;
}

void ksynth_engine_use_tables(int osc_a_slot, int osc_b_slot) {
    int i;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (osc_a_slot < 0 || osc_a_slot >= KS_MAX_TABLES || osc_b_slot < 0 || osc_b_slot >= KS_MAX_TABLES) {
        return;
    }

    g_engine.default_table_slot_a = osc_a_slot;
    g_engine.default_table_slot_b = osc_b_slot;
    g_engine.default_wave_a = KS_WAVE_TABLE;
    g_engine.default_wave_b = KS_WAVE_TABLE;

    for (i = 0; i < KS_MAX_VOICES; i++) {
        g_engine.voices[i].wave_a = KS_WAVE_TABLE;
        g_engine.voices[i].wave_b = KS_WAVE_TABLE;
        g_engine.voices[i].table_slot[0] = osc_a_slot;
        g_engine.voices[i].table_slot[1] = osc_b_slot;
    }
}

void ksynth_engine_set_sequence(const double *data, int length) {
    int i;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (!data || length <= 0) {
        return;
    }

    for (i = 0; i < KS_STEPS; i++) {
        int value = i < length ? (int)data[i] : 0;
        ksynth_engine_set_step(i, value);
    }
}

void ksynth_engine_set_sample(int slot, const double *data, int length) {
    float *copy;
    float peak = 0.0f;
    int i;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (slot < 0 || slot >= KS_MAX_SAMPLE_SLOTS || !data || length < 2) {
        return;
    }

    copy = malloc((size_t)length * sizeof(float));
    if (!copy) {
        return;
    }

    for (i = 0; i < length; i++) {
        copy[i] = (float)data[i];
        if (fabsf(copy[i]) > peak) {
            peak = fabsf(copy[i]);
        }
    }
    if (peak > 0.00001f) {
        for (i = 0; i < length; i++) {
            copy[i] /= peak;
        }
    }

    free(g_engine.samples[slot].data);
    g_engine.samples[slot].data = copy;
    g_engine.samples[slot].length = length;
    g_engine.samples[slot].root_note = 60;
}

void ksynth_engine_play_sample(int slot, float note, float velocity) {
    SampleVoice *voice;
    SampleSlot *sample_slot;
    float step;

    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    if (slot < 0 || slot >= KS_MAX_SAMPLE_SLOTS) {
        return;
    }

    sample_slot = &g_engine.samples[slot];
    if (!sample_slot->data || sample_slot->length < 2) {
        return;
    }

    step = powf(2.0f, (note - (float)sample_slot->root_note) / 12.0f);
    voice = allocate_sample_voice();
    memset(voice, 0, sizeof(*voice));
    voice->active = 1;
    voice->slot = slot;
    voice->position = 0.0f;
    voice->step = clampf(step, 0.0625f, 16.0f);
    voice->gain = clampf(velocity, 0.0f, 1.0f);
}

void ksynth_engine_start_transport(void) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.transport_running = 1;
}

void ksynth_engine_stop_transport(void) {
    if (!g_engine_ready) {
        ksynth_engine_init(44100);
    }
    g_engine.transport_running = 0;
    g_engine.step_phase = 0.0f;
    silence_all_voices();
}
