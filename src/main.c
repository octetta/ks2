#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/select.h>
#endif

#include "ksynth.h"
#include "audio.h"

#if defined(_WIN32)
#include "uedit.h"
#else
#include "bestline.h"
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef KS2_VERSION
#define KS2_VERSION "0.0.0-dev"
#endif

typedef enum {
    HOST_SLOT_EMPTY = 0,
    HOST_SLOT_WAVETABLE,
    HOST_SLOT_SAMPLE
} HostSlotKind;

typedef struct {
    K* slots[16];
    HostSlotKind kinds[16];
} HostState;

static HostState g_host;
static char g_history_path[PATH_MAX];
static volatile sig_atomic_t g_sigint_hits = 0;
static volatile sig_atomic_t g_sigint_notice = 0;
static volatile sig_atomic_t g_sigint_exit = 0;
static int g_script_depth = 0;

#if defined(_WIN32)
static FILE *g_history_file = NULL;
#endif

#if defined(_WIN32)
#define KS_REPL_MAX_LINE UEDIT_MAX_LINE
#else
#define KS_REPL_MAX_LINE 4096
#endif

static int ks_readline(const char *prompt, char *buf, int max_line) {
#if defined(_WIN32)
    return uedit(prompt, buf, max_line);
#else
    char *line;
    size_t len;

    line = bestline(prompt);
    if (!line) {
        return -1;
    }
    len = strlen(line);
    if (len >= (size_t)max_line) {
        len = (size_t)max_line - 1;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    if (line[0] != '\0') {
        bestlineHistoryAdd(line);
    }
    bestlineFree(line);
    return (int)len;
#endif
}

static void ks_handle_sigint(int signo) {
    (void)signo;
    if (g_sigint_hits == 0) {
        g_sigint_hits = 1;
        g_sigint_notice = 1;
        return;
    }
    g_sigint_exit = 1;
}

static void ks_sleep_seconds(double seconds) {
    if (seconds <= 0.0) {
        return;
    }
#if defined(_WIN32)
    Sleep((DWORD)(seconds * 1000.0));
#else
    struct timeval tv;
    tv.tv_sec = (long)seconds;
    tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1000000.0);
    if (tv.tv_usec < 0) {
        tv.tv_usec = 0;
    }
    select(0, NULL, NULL, NULL, &tv);
#endif
}

static void trim_newline(char *s) {
    size_t n;
    if (!s) {
        return;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void ks_history_record_line(const char *line) {
#if defined(_WIN32)
    if (g_history_file && line && line[0] != '\0') {
        fprintf(g_history_file, "%s\n", line);
        fflush(g_history_file);
    }
#else
    (void)line;
#endif
}

static void ks_history_shutdown(void) {
#if defined(_WIN32)
    if (g_history_file) {
        fclose(g_history_file);
        g_history_file = NULL;
    }
#else
    if (g_history_path[0] != '\0') {
        bestlineHistorySave(g_history_path);
    }
#endif
}

static void ks_history_init(void) {
    const char *home;

    g_history_path[0] = '\0';

#if defined(_WIN32)
    home = getenv("USERPROFILE");
    if (home && home[0] != '\0') {
        snprintf(g_history_path, sizeof(g_history_path), "%s\\.ks2", home);
    } else {
        const char *drive = getenv("HOMEDRIVE");
        const char *path = getenv("HOMEPATH");
        if (drive && path) {
            snprintf(g_history_path, sizeof(g_history_path), "%s%s\\.ks2", drive, path);
        }
    }
#else
    home = getenv("HOME");
    if (home && home[0] != '\0') {
        snprintf(g_history_path, sizeof(g_history_path), "%s/.ks2", home);
    }
#endif

    if (g_history_path[0] == '\0') {
        snprintf(g_history_path, sizeof(g_history_path), ".ks2");
    }

#if defined(_WIN32)
    {
        FILE *fp = fopen(g_history_path, "rb");
        if (fp) {
            char last_line[KS_REPL_MAX_LINE];
            char line[KS_REPL_MAX_LINE];

            last_line[0] = '\0';
            while (fgets(line, sizeof(line), fp)) {
                trim_newline(line);
                if (line[0] != '\0') {
                    strncpy(last_line, line, sizeof(last_line) - 1);
                    last_line[sizeof(last_line) - 1] = '\0';
                }
            }
            fclose(fp);
            if (last_line[0] != '\0') {
                uedit_history_set_last(last_line);
            }
        }
    }
    g_history_file = fopen(g_history_path, "a");
#else
    bestlineHistoryLoad(g_history_path);
#endif
}

static void print_repl_help(void) {
    printf("KSynth REPL v%s\n", KS2_VERSION);
    puts("  Enter DSL scripts such as:");
    puts("    N: 44100");
    puts("    T: !N");
    puts("    W: w s +\\(N#(440*(p 2)%44100))");
    puts("    P: ~1024");
    puts("    T: s P");
    puts("    W: w T t 440,N");
    puts("  Commands:");
    puts("    :help   show this help");
    puts("    :version show app version");
    puts("    :quit   exit");
    puts("    :load <file.ks>   evaluate a ksynth patch file");
    puts("    :script <file.txt> run REPL commands from a text file");
    puts("    :play <var>       legacy alias for :playsample <var>");
    puts("    :playwt <var>     stop the sequence and audition a vector as a wavetable");
    puts("    :playsample <var> stop the sequence and audition a vector as a sample");
    puts("    :stop             stop the sequencer and silence active voices");
    puts("    :start            resume the sequencer transport");
    puts("    :wt <0-3> <var>   bank a vector variable into an engine wavetable slot");
    puts("    :usewt <0-3> <0-3> route osc A/B to wavetable slots");
    puts("    :sample <hex> <var> bank a vector variable into a sample slot");
    puts("    :slot <hex> <var> legacy alias for :sample");
    puts("    :trigsample <hex> <note> <db> trigger a banked sample slot at fractional midi note and dB gain");
    puts("    :lfo <rate_hz> <depth> set pitch LFO rate/depth");
    puts("    :pd <amount> set phase distortion amount (-0.95..0.95)");
    puts("    :detune <cents_a> <cents_b> set osc detune in cents");
    puts("    :envamp <a_ms> <d_ms> <s> <r_ms> set amp envelope");
    puts("    :envpd <a_ms> <d_ms> <s> <r_ms> set pd envelope");
    puts("    :envpitch <a_ms> <d_ms> <s> <r_ms> set pitch envelope");
    puts("    :envdepth <pitch|pd|filter> <amount> set envelope depth");
    puts("    :modstate print current modulation/envelope settings");
    puts("    :chmode <hex> <mono|poly> set per-channel voice mode");
    puts("    :glide <hex> <ms> set mono glide time per channel");
    puts("    :noteon <hex> <note> <vel127> trigger channel note-on");
    puts("    :noteoff <hex> <note> trigger channel note-off");
    puts("    :sleep <seconds|ms> pause command stream (example: 0.25 or 250ms)");
    puts("    :playwtraw <var> play wavetable with modulation minimized");
    puts("    :slots            list filled slots");
}

static char* read_text_file(const char *path) {
    FILE *fp;
    long len;
    char *buf;

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    return buf;
}

static int var_index_from_name(char name) {
    if (name >= 'a' && name <= 'z') {
        name = (char)(name - 'a' + 'A');
    }
    if (name < 'A' || name > 'Z') {
        return -1;
    }
    return name - 'A';
}

static K* get_var_value(char name) {
    return k_get((char)toupper((unsigned char)name));
}

static int host_store_slot(int slot, K* value, HostSlotKind kind) {
    K* copy;

    if (!value || slot < 0 || slot >= 16 || value->n <= 0 || value->n == -1) {
        return -1;
    }
    copy = k_new(value->n);
    if (!copy) {
        return -1;
    }
    memcpy(copy->f, value->f, (size_t)value->n * sizeof(double));
    if (g_host.slots[slot]) {
        k_free(g_host.slots[slot]);
    }
    g_host.slots[slot] = copy;
    g_host.kinds[slot] = kind;
    return 0;
}

static void host_list_slots(void) {
    int i;
    int any = 0;

    for (i = 0; i < 16; i++) {
        if (g_host.slots[i]) {
            const char *kind = "data";
            if (g_host.kinds[i] == HOST_SLOT_WAVETABLE) {
                kind = "wavetable";
            } else if (g_host.kinds[i] == HOST_SLOT_SAMPLE) {
                kind = "sample";
            }
            printf("%X: %s len=%d\n", i, kind, g_host.slots[i]->n);
            any = 1;
        }
    }
    if (!any) {
        puts("(no slots)");
    }
}

static int host_play_wavetable(char name) {
    K* value = get_var_value(name);

    if (!value) {
        puts("(missing var)");
        return -1;
    }
    if (value->n <= 1 || value->n == -1) {
        puts("(not a playable vector)");
        k_free(value);
        return -1;
    }

    ksynth_engine_stop_transport();
    ksynth_engine_set_table(0, value->f, value->n);
    ksynth_engine_use_tables(0, 0);
    ksynth_engine_note_on(60, 0.9f);
    printf("playing %c as wavetable through table[0]\n", (char)toupper((unsigned char)name));
    k_free(value);
    return 0;
}

static int host_play_wavetable_raw(char name) {
    K* value = get_var_value(name);

    if (!value) {
        puts("(missing var)");
        return -1;
    }
    if (value->n <= 1 || value->n == -1) {
        puts("(not a playable vector)");
        k_free(value);
        return -1;
    }

    ksynth_engine_stop_transport();
    ksynth_engine_set_table(0, value->f, value->n);
    ksynth_engine_use_tables(0, 0);
    ksynth_engine_set_lfo(0.0f, 0.0f);
    ksynth_engine_set_pd(0.0f);
    ksynth_engine_set_detune(0.0f, 0.0f);
    ksynth_engine_set_filter_env_depth(0.0f);
    ksynth_engine_set_pitch_env_depth(0.0f);
    ksynth_engine_set_pd_env_depth(0.0f);
    ksynth_engine_note_on(60, 0.9f);
    printf("playing %c as raw wavetable through table[0]\n", (char)toupper((unsigned char)name));
    k_free(value);
    return 0;
}

static int host_play_sample(char name) {
    K* value = get_var_value(name);

    if (!value) {
        puts("(missing var)");
        return -1;
    }
    if (value->n <= 1 || value->n == -1) {
        puts("(not a playable vector)");
        k_free(value);
        return -1;
    }

    ksynth_engine_stop_transport();
    ksynth_engine_set_sample(0, value->f, value->n);
    ksynth_engine_play_sample(0, 60, 0.9f);
    printf("playing %c as sample through sample[0]\n", (char)toupper((unsigned char)name));
    k_free(value);
    return 0;
}

static int host_bank_wavetable(int slot, char name) {
    K* value = get_var_value(name);

    if (slot < 0 || slot >= 4) {
        puts("(wavetable slot must be 0-3)");
        return -1;
    }
    if (!value) {
        puts("(missing var)");
        return -1;
    }
    if (value->n <= 1 || value->n == -1) {
        puts("(not a bankable vector)");
        k_free(value);
        return -1;
    }

    ksynth_engine_set_table(slot, value->f, value->n);
    if (host_store_slot(slot, value, HOST_SLOT_WAVETABLE) == 0) {
        printf("wavetable[%X] loaded from %c len=%d\n", slot, (char)toupper((unsigned char)name), value->n);
    } else {
        puts("(failed to store wavetable)");
    }
    k_free(value);
    return 0;
}

static int host_bank_sample(int slot, char name) {
    K* value = get_var_value(name);

    if (!value) {
        puts("(missing var)");
        return -1;
    }
    if (value->n <= 1 || value->n == -1) {
        puts("(not a bankable vector)");
        k_free(value);
        return -1;
    }

    ksynth_engine_set_sample(slot, value->f, value->n);
    if (host_store_slot(slot, value, HOST_SLOT_SAMPLE) == 0) {
        printf("sample[%X] loaded from %c len=%d\n", slot, (char)toupper((unsigned char)name), value->n);
    } else {
        puts("(failed to store sample)");
    }
    k_free(value);
    return 0;
}

static int host_use_wavetables(int osc_a_slot, int osc_b_slot) {
    if (osc_a_slot < 0 || osc_a_slot >= 4 || osc_b_slot < 0 || osc_b_slot >= 4) {
        puts("(wavetable slots must be 0-3)");
        return -1;
    }

    ksynth_engine_use_tables(osc_a_slot, osc_b_slot);
    printf("osc A->wt[%X], osc B->wt[%X]\n", osc_a_slot, osc_b_slot);
    return 0;
}

static int host_trigger_sample(int slot, float note, float gain_db) {
    float gain_linear;

    if (slot < 0 || slot >= 16) {
        puts("(sample slot must be 0-F)");
        return -1;
    }
    if (!g_host.slots[slot] || g_host.kinds[slot] != HOST_SLOT_SAMPLE) {
        puts("(slot is empty or not a sample)");
        return -1;
    }
    if (note < 0.0f || note > 127.0f) {
        puts("(note must be 0-127)");
        return -1;
    }
    if (gain_db < -96.0f || gain_db > 0.0f) {
        puts("(db must be in range -96.0 to 0.0)");
        return -1;
    }

    gain_linear = powf(10.0f, gain_db / 20.0f);
    ksynth_engine_play_sample(slot, note, gain_linear);
    printf("triggered sample[%X] note=%.3f db=%.2f (lin=%.3f)\n", slot, note, gain_db, gain_linear);
    return 0;
}

static int host_set_lfo(float rate_hz, float depth) {
    if (rate_hz < 0.0f || rate_hz > 40.0f) {
        puts("(lfo rate must be 0.0-40.0 Hz)");
        return -1;
    }
    if (depth < 0.0f || depth > 0.25f) {
        puts("(lfo depth must be 0.0-0.25)");
        return -1;
    }
    ksynth_engine_set_lfo(rate_hz, depth);
    printf("lfo set rate=%.3fHz depth=%.4f\n", rate_hz, depth);
    return 0;
}

static int host_set_pd(float amount) {
    if (amount < -0.95f || amount > 0.95f) {
        puts("(pd amount must be -0.95 to 0.95)");
        return -1;
    }
    ksynth_engine_set_pd(amount);
    printf("pd set amount=%.3f\n", amount);
    return 0;
}

static int host_set_detune(float cents_a, float cents_b) {
    if (cents_a < -100.0f || cents_a > 100.0f || cents_b < -100.0f || cents_b > 100.0f) {
        puts("(detune cents must be -100.0 to 100.0)");
        return -1;
    }
    ksynth_engine_set_detune(cents_a, cents_b);
    printf("detune set a=%.2fc b=%.2fc\n", cents_a, cents_b);
    return 0;
}

static int host_set_envamp(float a_ms, float d_ms, float s, float r_ms) {
    if (a_ms < 0.0f || d_ms < 0.0f || r_ms < 0.0f || s < 0.0f || s > 1.2f) {
        puts("(envamp expects a,d,r >= 0 and sustain 0.0-1.2)");
        return -1;
    }
    ksynth_engine_set_amp_adsr(a_ms, d_ms, s, r_ms);
    printf("envamp set a=%.1fms d=%.1fms s=%.3f r=%.1fms\n", a_ms, d_ms, s, r_ms);
    return 0;
}

static int host_set_envpd(float a_ms, float d_ms, float s, float r_ms) {
    if (a_ms < 0.0f || d_ms < 0.0f || r_ms < 0.0f || s < 0.0f || s > 1.2f) {
        puts("(envpd expects a,d,r >= 0 and sustain 0.0-1.2)");
        return -1;
    }
    ksynth_engine_set_pd_adsr(a_ms, d_ms, s, r_ms);
    printf("envpd set a=%.1fms d=%.1fms s=%.3f r=%.1fms\n", a_ms, d_ms, s, r_ms);
    return 0;
}

static int host_set_envpitch(float a_ms, float d_ms, float s, float r_ms) {
    if (a_ms < 0.0f || d_ms < 0.0f || r_ms < 0.0f || s < -1.0f || s > 1.0f) {
        puts("(envpitch expects a,d,r >= 0 and sustain -1.0 to 1.0)");
        return -1;
    }
    ksynth_engine_set_pitch_adsr(a_ms, d_ms, s, r_ms);
    printf("envpitch set a=%.1fms d=%.1fms s=%.3f r=%.1fms\n", a_ms, d_ms, s, r_ms);
    return 0;
}

static int host_set_envdepth(const char *target, float amount) {
    if (strcmp(target, "pitch") == 0) {
        if (amount < -1.0f || amount > 1.0f) {
            puts("(pitch depth must be -1.0 to 1.0)");
            return -1;
        }
        ksynth_engine_set_pitch_env_depth(amount);
        printf("envdepth pitch=%.3f\n", amount);
        return 0;
    }
    if (strcmp(target, "pd") == 0) {
        if (amount < -2.0f || amount > 2.0f) {
            puts("(pd depth must be -2.0 to 2.0)");
            return -1;
        }
        ksynth_engine_set_pd_env_depth(amount);
        printf("envdepth pd=%.3f\n", amount);
        return 0;
    }
    if (strcmp(target, "filter") == 0) {
        if (amount < -8000.0f || amount > 8000.0f) {
            puts("(filter depth must be -8000 to 8000)");
            return -1;
        }
        ksynth_engine_set_filter_env_depth(amount);
        printf("envdepth filter=%.1f\n", amount);
        return 0;
    }

    puts("(envdepth target must be pitch, pd, or filter)");
    return -1;
}

static int host_print_mod_state(void) {
    KSEngineModState s;

    ksynth_engine_get_mod_state(&s);
    puts("modstate:");
    printf("  lfo: rate=%.3fHz depth=%.4f\n", s.lfo_rate_hz, s.lfo_depth);
    printf("  pd: amount=%.3f\n", s.pd_amount);
    printf("  detune: a=%.2fc b=%.2fc\n", s.detune_a_cents, s.detune_b_cents);
    printf("  envdepth: pitch=%.3f pd=%.3f filter=%.1f\n",
           s.pitch_env_depth, s.pd_env_depth, s.filter_env_depth);
    printf("  envamp: a=%.1fms d=%.1fms s=%.3f r=%.1fms\n",
           s.amp_attack_ms, s.amp_decay_ms, s.amp_sustain, s.amp_release_ms);
    printf("  envpd: a=%.1fms d=%.1fms s=%.3f r=%.1fms\n",
           s.pd_attack_ms, s.pd_decay_ms, s.pd_sustain, s.pd_release_ms);
    printf("  envpitch: a=%.1fms d=%.1fms s=%.3f r=%.1fms\n",
           s.pitch_attack_ms, s.pitch_decay_ms, s.pitch_sustain, s.pitch_release_ms);
    return 0;
}

static int host_set_channel_mode(int channel, const char *mode) {
    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (strcmp(mode, "mono") == 0) {
        ksynth_engine_set_channel_mode(channel, KS_CHANNEL_MONO);
        printf("channel %X mode=mono\n", channel);
        return 0;
    }
    if (strcmp(mode, "poly") == 0) {
        ksynth_engine_set_channel_mode(channel, KS_CHANNEL_POLY);
        printf("channel %X mode=poly\n", channel);
        return 0;
    }
    puts("(mode must be mono or poly)");
    return -1;
}

static int host_set_channel_glide(int channel, float glide_ms) {
    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (glide_ms < 0.0f || glide_ms > 5000.0f) {
        puts("(glide must be 0.0-5000.0 ms)");
        return -1;
    }
    ksynth_engine_set_channel_glide_ms(channel, glide_ms);
    printf("channel %X glide=%.1fms\n", channel, glide_ms);
    return 0;
}

static int host_note_on_ch(int channel, int note, int vel127) {
    float velocity;

    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (note < 0 || note > 127) {
        puts("(note must be 0-127)");
        return -1;
    }
    if (vel127 < 0 || vel127 > 127) {
        puts("(vel127 must be 0-127)");
        return -1;
    }
    velocity = (float)vel127 / 127.0f;
    ksynth_engine_note_on_ch(channel, note, velocity);
    printf("noteon ch=%X note=%d vel=%d\n", channel, note, vel127);
    return 0;
}

static int host_note_off_ch(int channel, int note) {
    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (note < 0 || note > 127) {
        puts("(note must be 0-127)");
        return -1;
    }
    ksynth_engine_note_off_ch(channel, note);
    printf("noteoff ch=%X note=%d\n", channel, note);
    return 0;
}

static int handle_repl_command(const char *line);

static int process_input_line(const char *line) {
    K* result;
    int rc;
    const char *line_ptr;

    if (!line || line[0] == '\0') {
        return 1;
    }

    line_ptr = line;
    while (*line_ptr && isspace((unsigned char)*line_ptr)) {
        line_ptr++;
    }
    if (*line_ptr == '\0') {
        return 1;
    }
    if (*line_ptr == '#') {
        return 1;
    }
    if (line_ptr[0] == '/' && line_ptr[1] == '/') {
        return 1;
    }
    if (line_ptr[0] == '`' && line_ptr[1] == '`' && line_ptr[2] == '`') {
        return 1;
    }

    if (line_ptr[0] == ':') {
        rc = handle_repl_command(line_ptr);
        if (rc >= 0) {
            g_sigint_hits = 0;
        }
        return rc;
    }

    result = k_eval_script(line_ptr);
    if (result) {
        p(result);
        k_free(result);
    } else {
        puts("(error)");
    }
    g_sigint_hits = 0;
    return 1;
}

static int host_run_script(const char *path) {
    FILE *fp;
    char line[KS_REPL_MAX_LINE];
    int lineno = 0;

    if (!path || path[0] == '\0') {
        puts("(script path missing)");
        return -1;
    }
    if (g_script_depth >= 8) {
        puts("(script nesting too deep)");
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        puts("(could not read script file)");
        return -1;
    }

    g_script_depth++;
    while (fgets(line, (int)sizeof(line), fp)) {
        char *p = line;
        int rc;

        lineno++;
        trim_newline(p);
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            continue;
        }

        ks_history_record_line(p);
        rc = process_input_line(p);
        if (rc < 0) {
            fclose(fp);
            g_script_depth--;
            return -1;
        }
    }

    fclose(fp);
    g_script_depth--;
    printf("script %s done (%d lines)\n", path, lineno);
    return 0;
}

static int host_sleep(const char *spec) {
    char *end;
    double value;
    double seconds;

    if (!spec || spec[0] == '\0') {
        puts("(sleep expects a value, e.g. 0.25 or 250ms)");
        return -1;
    }

    errno = 0;
    value = strtod(spec, &end);
    if (errno != 0 || end == spec) {
        puts("(invalid sleep value)");
        return -1;
    }

    while (*end && isspace((unsigned char)*end)) {
        end++;
    }

    if (*end == '\0' || strcmp(end, "s") == 0) {
        seconds = value;
    } else if (strcmp(end, "ms") == 0) {
        seconds = value / 1000.0;
    } else {
        puts("(sleep suffix must be empty, s, or ms)");
        return -1;
    }

    if (seconds < 0.0 || seconds > 600.0) {
        puts("(sleep must be between 0 and 600 seconds)");
        return -1;
    }

    ks_sleep_seconds(seconds);
    return 0;
}

static int host_load_patch(const char *path) {
    char *text;
    K* result;
    K* w;

    text = read_text_file(path);
    if (!text) {
        puts("(could not read file)");
        return -1;
    }

    result = k_eval_script(text);
    free(text);
    if (!result) {
        puts("(evaluation failed)");
        return -1;
    }
    k_free(result);

    w = get_var_value('W');
    if (w && w->n > 1 && w->n != -1) {
        printf("loaded %s, W len=%d\n", path, w->n);
        k_free(w);
    } else {
        puts("(loaded, but W is missing or not a vector)");
        if (w) {
            k_free(w);
        }
    }
    return 0;
}

static int handle_repl_command(const char *line) {
    int slot;
    int slot_b;
    float note;
    int note_i;
    int vel127;
    int channel;
    char var_name;
    float gain_db;
    float v1;
    float v2;
    float v3;
    float v4;
    char word[32];
    char path[512];

    if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) {
        return -1;
    }
    if (strcmp(line, ":help") == 0 || strcmp(line, ":h") == 0) {
        print_repl_help();
        return 1;
    }
    if (strcmp(line, ":version") == 0) {
        printf("KSynth v%s\n", KS2_VERSION);
        return 1;
    }
    if (strcmp(line, ":slots") == 0) {
        host_list_slots();
        return 1;
    }
    if (strcmp(line, ":modstate") == 0) {
        host_print_mod_state();
        return 1;
    }
    if (strcmp(line, ":stop") == 0) {
        ksynth_engine_stop_transport();
        puts("transport stopped");
        return 1;
    }
    if (strcmp(line, ":start") == 0) {
        ksynth_engine_start_transport();
        puts("transport started");
        return 1;
    }
    if (sscanf(line, ":load %511s", path) == 1) {
        host_load_patch(path);
        return 1;
    }
    if (sscanf(line, ":script %511s", path) == 1) {
        if (host_run_script(path) < 0) {
            return -1;
        }
        return 1;
    }
    if (sscanf(line, ":playwtraw %c", &var_name) == 1) {
        host_play_wavetable_raw(var_name);
        return 1;
    }
    if (sscanf(line, ":playwt %c", &var_name) == 1) {
        host_play_wavetable(var_name);
        return 1;
    }
    if (sscanf(line, ":playsample %c", &var_name) == 1) {
        host_play_sample(var_name);
        return 1;
    }
    if (sscanf(line, ":play %c", &var_name) == 1) {
        host_play_sample(var_name);
        return 1;
    }
    if (sscanf(line, ":wt %x %c", &slot, &var_name) == 2) {
        host_bank_wavetable(slot, var_name);
        return 1;
    }
    if (sscanf(line, ":usewt %x %x", &slot, &slot_b) == 2) {
        host_use_wavetables(slot, slot_b);
        return 1;
    }
    if (sscanf(line, ":sample %x %c", &slot, &var_name) == 2) {
        host_bank_sample(slot, var_name);
        return 1;
    }
    if (sscanf(line, ":slot %x %c", &slot, &var_name) == 2) {
        host_bank_sample(slot, var_name);
        return 1;
    }
    if (sscanf(line, ":trigsample %x %f %f", &slot, &note, &gain_db) == 3) {
        host_trigger_sample(slot, note, gain_db);
        return 1;
    }
    if (sscanf(line, ":lfo %f %f", &v1, &v2) == 2) {
        host_set_lfo(v1, v2);
        return 1;
    }
    if (sscanf(line, ":pd %f", &v1) == 1) {
        host_set_pd(v1);
        return 1;
    }
    if (sscanf(line, ":detune %f %f", &v1, &v2) == 2) {
        host_set_detune(v1, v2);
        return 1;
    }
    if (sscanf(line, ":envamp %f %f %f %f", &v1, &v2, &v3, &v4) == 4) {
        host_set_envamp(v1, v2, v3, v4);
        return 1;
    }
    if (sscanf(line, ":envpd %f %f %f %f", &v1, &v2, &v3, &v4) == 4) {
        host_set_envpd(v1, v2, v3, v4);
        return 1;
    }
    if (sscanf(line, ":envpitch %f %f %f %f", &v1, &v2, &v3, &v4) == 4) {
        host_set_envpitch(v1, v2, v3, v4);
        return 1;
    }
    if (sscanf(line, ":envdepth %31s %f", word, &v1) == 2) {
        host_set_envdepth(word, v1);
        return 1;
    }
    if (sscanf(line, ":chmode %x %31s", &channel, word) == 2) {
        host_set_channel_mode(channel, word);
        return 1;
    }
    if (sscanf(line, ":glide %x %f", &channel, &v1) == 2) {
        host_set_channel_glide(channel, v1);
        return 1;
    }
    if (sscanf(line, ":noteon %x %d %d", &channel, &note_i, &vel127) == 3) {
        host_note_on_ch(channel, note_i, vel127);
        return 1;
    }
    if (sscanf(line, ":noteoff %x %d", &channel, &note_i) == 2) {
        host_note_off_ch(channel, note_i);
        return 1;
    }
    if (sscanf(line, ":sleep %31s", word) == 1) {
        host_sleep(word);
        return 1;
    }

    puts("(unknown command)");
    return 1;
}

int main(void) {
    char line[KS_REPL_MAX_LINE];
    int i;

    signal(SIGINT, ks_handle_sigint);
    ks_history_init();
    if (audio_init(44100, 2, 256) != 0) {
        ks_history_shutdown();
        return 1;
    }

    audio_start();
    print_repl_help();
    puts("");

    while (1) {
        K* result;
        int nread;

        signal(SIGINT, ks_handle_sigint);
        nread = ks_readline("ks> ", line, (int)sizeof(line));
        if (g_sigint_exit) {
            break;
        }
        if (g_sigint_notice) {
            puts("^C (press Ctrl-C again to quit)");
            ksynth_engine_all_notes_off();
            g_sigint_notice = 0;
            continue;
        }
        if (nread < 0) {
            break;
        }

        if (line[0] == '\0') {
            continue;
        }
        ks_history_record_line(line);
        nread = process_input_line(line);
        if (nread < 0) {
            break;
        }
    }

    audio_stop();
    audio_shutdown();
    ks_history_shutdown();
    for (i = 0; i < 16; i++) {
        if (g_host.slots[i]) {
            k_free(g_host.slots[i]);
        }
    }
    return 0;
}
