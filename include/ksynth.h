#ifndef KSYNTH_H
#define KSYNTH_H

typedef struct K {
    int r;      // refcount
    int n;      // length or -1 for functions
    double f[]; // flexible array member
} K;

typedef struct KSEngineModState {
    float lfo_rate_hz;
    float lfo_depth;
    float pd_amount;
    float detune_a_cents;
    float detune_b_cents;
    float filter_env_depth;
    float pitch_env_depth;
    float pd_env_depth;
    float filter_cutoff_hz;
    float filter_resonance;
    float filter_keytrack;
    float filter_drive;
    int filter_mode;
    float delay_time_ms;
    float delay_feedback;
    float delay_wet;
    float channel0_pan;
    float channel0_pan_spread;
    float channel0_pan_lfo_depth;
    float channel0_delay_send;
    float amp_attack_ms;
    float amp_decay_ms;
    float amp_sustain;
    float amp_release_ms;
    float pd_attack_ms;
    float pd_decay_ms;
    float pd_sustain;
    float pd_release_ms;
    float pitch_attack_ms;
    float pitch_decay_ms;
    float pitch_sustain;
    float pitch_release_ms;
} KSEngineModState;

typedef enum KSChannelMode {
    KS_CHANNEL_POLY = 0,
    KS_CHANNEL_MONO = 1
} KSChannelMode;

typedef struct KSChannelStateSnapshot {
    KSChannelMode mode;
    float glide_ms;
    int held_count;
    int stack_len;
    int top_note;
    int active_voice_count;
} KSChannelStateSnapshot;

K* k_new(int n);
void k_free(K* x);
K* k_get(char name);
K* k_func(char* body);
int k_is_func(K* x);
char* k_func_body(K* x);
K* k_call(K* fn, K* args[], int nargs);

K* mo(char c, K* b);
K* dy(char c, K* a, K* b);
K* e(char **s);
K* atom(char **s);
K* k_eval_script(const char *script);
void p(K* x);

void ksynth_engine_init(int sample_rate);
void ksynth_engine_render(float *out, int frames);
void ksynth_engine_render_stereo(float *out_lr, int frames);
void ksynth_engine_set_bpm(float bpm);
void ksynth_engine_set_gain(float gain);
void ksynth_engine_set_step(int index, int semitone);
void ksynth_engine_set_waveforms(int osc_a, int osc_b);
void ksynth_engine_set_lfo(float rate_hz, float depth);
void ksynth_engine_set_detune(float cents_a, float cents_b);
void ksynth_engine_set_pd(float amount);
void ksynth_engine_set_filter(float cutoff_hz, float resonance);
void ksynth_engine_set_filter_cutoff(float cutoff_hz);
void ksynth_engine_set_filter_resonance(float resonance);
void ksynth_engine_set_filter_keytrack(float keytrack);
void ksynth_engine_set_filter_mode(int mode);
void ksynth_engine_set_filter_drive(float drive);
void ksynth_engine_set_filter_env_depth(float amount);
void ksynth_engine_set_pitch_env_depth(float amount);
void ksynth_engine_set_pd_env_depth(float amount);
void ksynth_engine_set_amp_adsr(float attack_ms, float decay_ms, float sustain_level, float release_ms);
void ksynth_engine_set_pd_adsr(float attack_ms, float decay_ms, float sustain_level, float release_ms);
void ksynth_engine_set_pitch_adsr(float attack_ms, float decay_ms, float sustain_level, float release_ms);
void ksynth_engine_get_mod_state(KSEngineModState *out_state);
void ksynth_engine_set_channel_mode(int channel, KSChannelMode mode);
void ksynth_engine_set_channel_glide_ms(int channel, float glide_ms);
void ksynth_engine_set_channel_pan(int channel, float pan);
void ksynth_engine_set_channel_pan_spread(int channel, float spread);
void ksynth_engine_set_channel_pan_lfo_depth(int channel, float depth);
void ksynth_engine_set_channel_delay_send(int channel, float send);
void ksynth_engine_get_channel_state(int channel, KSChannelStateSnapshot *out_state);
void ksynth_engine_note_on_ch(int channel, int note, float velocity);
void ksynth_engine_note_off_ch(int channel, int note);
void ksynth_engine_note_on(int note, float velocity);
void ksynth_engine_all_notes_off(void);
void ksynth_engine_set_table(int slot, const double *data, int length);
void ksynth_engine_use_tables(int osc_a_slot, int osc_b_slot);
void ksynth_engine_set_sample(int slot, const double *data, int length);
void ksynth_engine_play_sample(int slot, float note, float velocity);
void ksynth_engine_set_sequence(const double *data, int length);
void ksynth_engine_start_transport(void);
void ksynth_engine_stop_transport(void);
void ksynth_engine_set_delay(float time_ms, float feedback, float wet);

// Example: produce one sample
K* ksynth_render_sample(void);

#endif
