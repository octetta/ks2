#ifndef KSYNTH_H
#define KSYNTH_H

typedef struct K {
    int r;      // refcount
    int n;      // length or -1 for functions
    double f[]; // flexible array member
} K;

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
void ksynth_engine_set_bpm(float bpm);
void ksynth_engine_set_gain(float gain);
void ksynth_engine_set_step(int index, int semitone);
void ksynth_engine_set_waveforms(int osc_a, int osc_b);
void ksynth_engine_set_pd(float amount);
void ksynth_engine_note_on(int note, float velocity);
void ksynth_engine_all_notes_off(void);
void ksynth_engine_set_table(int slot, const double *data, int length);
void ksynth_engine_use_tables(int osc_a_slot, int osc_b_slot);
void ksynth_engine_set_sample(int slot, const double *data, int length);
void ksynth_engine_play_sample(int slot, int note, float velocity);
void ksynth_engine_set_sequence(const double *data, int length);
void ksynth_engine_start_transport(void);
void ksynth_engine_stop_transport(void);

// Example: produce one sample
K* ksynth_render_sample(void);

#endif
