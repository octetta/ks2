#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <glob.h>
#else
#include <direct.h>
#include <windows.h>
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
static char g_kspath[PATH_MAX];
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

static const char *g_repl_commands[] = {
    ":help", ":h",
    ":version",
    ":quit", ":q",
    ":load",
    ":script",
    ":play",
    ":playwt",
    ":playwtraw",
    ":playsample",
    ":stop",
    ":start",
    ":wt",
    ":usewt",
    ":sample",
    ":slot",
    ":trigsample",
    ":lfo",
    ":pd",
    ":filter",
    ":cutoff",
    ":res",
    ":keytrack",
    ":filtermode",
    ":fdrive",
    ":gain",
    ":detune",
    ":envamp",
    ":envpd",
    ":envpitch",
    ":envdepth",
    ":modstate",
    ":chmode",
    ":glide",
    ":pan",
    ":panspread",
    ":panlfo",
    ":chsenddelay",
    ":delay",
    ":noteon",
    ":noteoff",
    ":trigwt",
    ":chstate",
    ":kspath",
    ":sleep",
    ":ls",
    ":cd",
    ":slots",
    NULL
};

static void ks_repl_completion_callback(const char *line, int pos, bestlineCompletions *lc) {
    int i;
    size_t input_len;

    (void)pos;
    if (!line || line[0] != ':') {
        return;
    }

    input_len = strlen(line);
    for (i = 0; g_repl_commands[i]; i++) {
        if (strncmp(g_repl_commands[i], line, input_len) == 0) {
            bestlineAddCompletion(lc, g_repl_commands[i]);
        }
    }
}
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

static int ks_terminal_width(void) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h;
    int width;

    h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        return 80;
    }
    if (!GetConsoleScreenBufferInfo(h, &csbi)) {
        return 80;
    }
    width = (int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    return width > 20 ? width : 80;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) {
        return (int)ws.ws_col;
    }
    return 80;
#endif
}

static char *ks_strdup(const char *s) {
    size_t n;
    char *copy;

    if (!s) {
        return NULL;
    }
    n = strlen(s) + 1;
    copy = malloc(n);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, n);
    return copy;
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
    bestlineSetCompletionCallback(ks_repl_completion_callback);
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
    puts("    :load <file[.ks]> evaluate a ksynth patch file (.ks guessed if omitted)");
    puts("    :script <file>    run REPL commands from a text file (.ks2.txt/.txt guessed)");
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
    puts("    :filter <cutoff_hz> <res> set filter cutoff/resonance");
    puts("    :cutoff <hz> set filter cutoff (40Hz..Nyquist*0.45)");
    puts("    :res <value> set filter resonance (0.05..1.20)");
    puts("    :keytrack <0..1.5> set filter keyboard tracking amount");
    puts("    :filtermode <lp|bp|hp> set filter mode");
    puts("    :fdrive <0.1..12> set pre-filter drive amount");
    puts("    :gain <db> set synth master gain in dB (-96.0..+24.0, >0 can overdrive)");
    puts("    :detune <cents_a> <cents_b> set osc detune in cents");
    puts("    :envamp <a_ms> <d_ms> <s> <r_ms> set amp envelope");
    puts("    :envpd <a_ms> <d_ms> <s> <r_ms> set pd envelope");
    puts("    :envpitch <a_ms> <d_ms> <s> <r_ms> set pitch envelope");
    puts("    :envdepth <pitch|pd|filter> <amount> set envelope depth");
    puts("    :modstate print current modulation/envelope settings");
    puts("    :chmode <hex> <mono|poly> set per-channel voice mode");
    puts("    :glide <hex> <ms> set mono glide time per channel");
    puts("    :pan <hex> <-1..1> set per-channel pan (left..right)");
    puts("    :panspread <hex> <0..1> set per-voice random pan spread around channel pan");
    puts("    :panlfo <hex> <0..1> set per-channel pan LFO depth");
    puts("    :chsenddelay <hex> <db> set per-channel delay send in dB");
    puts("    :delay <ms> <feedback> <wet> set global delay parameters");
    puts("    :noteon <hex> <note> <vel127> trigger channel note-on");
    puts("    :noteoff <hex> <note> trigger channel note-off");
    puts("    :trigwt <hex> <note> <vel127> trigger wavetable voice on channel");
    puts("    :chstate <hex> print channel mono/poly/glide/note state");
    puts("    :kspath [path] show or set KS asset base path for :load/:script fallback");
    puts("    :sleep <seconds|ms> pause command stream (example: 0.25 or 250ms)");
    puts("    :ls [path|pattern] list directory entries; supports simple wildcards (*, ?)");
    puts("    :cd [path] change directory (with no path, print current directory)");
    puts("    :playwtraw <var> play wavetable with modulation minimized");
    puts("    :slots            list filled slots");
}

static int ks_chdir(const char *path) {
#if defined(_WIN32)
    return _chdir(path);
#else
    return chdir(path);
#endif
}

static char *ks_getcwd(char *buf, size_t size) {
#if defined(_WIN32)
    return _getcwd(buf, (int)size);
#else
    return getcwd(buf, size);
#endif
}

static int ks_is_abs_path(const char *path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
#if defined(_WIN32)
    if ((isalpha((unsigned char)path[0]) && path[1] == ':') ||
        (path[0] == '\\' && path[1] == '\\')) {
        return 1;
    }
    return 0;
#else
    return path[0] == '/';
#endif
}

static int ks_file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int ks_try_compose_path(char *out, size_t out_size, const char *base, const char *tail) {
    int n;
    if (!out || out_size == 0 || !base || !tail) {
        return -1;
    }
    n = snprintf(out, out_size, "%s/%s", base, tail);
    if (n < 0 || (size_t)n >= out_size) {
        return -1;
    }
    return 0;
}

static int ks_has_extension(const char *path) {
    const char *dot;
    const char *slash1;
    const char *slash2;
    const char *sep;

    if (!path || path[0] == '\0') {
        return 0;
    }
    dot = strrchr(path, '.');
    if (!dot || dot == path) {
        return 0;
    }
    slash1 = strrchr(path, '/');
    slash2 = strrchr(path, '\\');
    sep = slash1;
    if (slash2 && (!sep || slash2 > sep)) {
        sep = slash2;
    }
    return sep ? dot > sep : 1;
}

static int ks_try_path_candidates(const char *candidate, char *resolved, size_t resolved_size) {
    char pathbuf[PATH_MAX];

    if (!candidate || !resolved || resolved_size == 0) {
        return -1;
    }
    if (strlen(candidate) < resolved_size) {
        strcpy(resolved, candidate);
    } else {
        return -1;
    }
    if (ks_file_exists(resolved)) {
        return 0;
    }
    if (g_kspath[0] == '\0' || ks_is_abs_path(candidate)) {
        return -1;
    }

    if ((strncmp(candidate, "ks/", 3) == 0 || strncmp(candidate, "ks\\", 3) == 0) &&
        ks_try_compose_path(pathbuf, sizeof(pathbuf), g_kspath, candidate + 3) == 0 &&
        ks_file_exists(pathbuf)) {
        if (strlen(pathbuf) < resolved_size) {
            strcpy(resolved, pathbuf);
            return 0;
        }
        return -1;
    }

    if (ks_try_compose_path(pathbuf, sizeof(pathbuf), g_kspath, candidate) == 0 &&
        ks_file_exists(pathbuf)) {
        if (strlen(pathbuf) < resolved_size) {
            strcpy(resolved, pathbuf);
            return 0;
        }
        return -1;
    }

    return -1;
}

static int ks_resolve_input_path(const char *input, char *resolved, size_t resolved_size,
                                 const char *exts[], int ext_count) {
    const char *trimmed = input;
    char candidate[PATH_MAX];
    int i;

    if (!input || !resolved || resolved_size == 0) {
        return -1;
    }
    while (*trimmed && isspace((unsigned char)*trimmed)) {
        trimmed++;
    }
    if (*trimmed == '\0') {
        return -1;
    }

    if (ks_try_path_candidates(trimmed, resolved, resolved_size) == 0) {
        return 0;
    }

    if (ks_has_extension(trimmed)) {
        return -1;
    }

    for (i = 0; i < ext_count; i++) {
        int n;
        if (!exts[i] || exts[i][0] == '\0') {
            continue;
        }
        n = snprintf(candidate, sizeof(candidate), "%s%s", trimmed, exts[i]);
        if (n < 0 || (size_t)n >= sizeof(candidate)) {
            continue;
        }
        if (ks_try_path_candidates(candidate, resolved, resolved_size) == 0) {
            return 0;
        }
    }

    return -1;
}

static void ks_print_path_context(const char *kind, const char *requested, const char *suffix_hint) {
    char cwd[PATH_MAX];

    printf("(could not read %s: %s)\n", kind, requested ? requested : "");
    if (suffix_hint && suffix_hint[0] != '\0') {
        printf("(tip: try %s)\n", suffix_hint);
    }
    if (ks_getcwd(cwd, sizeof(cwd))) {
        printf("(cwd: %s)\n", cwd);
    } else {
        puts("(cwd: unavailable)");
    }
    if (g_kspath[0] != '\0') {
        printf("(kspath: %s)\n", g_kspath);
    } else {
        puts("(kspath: unset)");
    }
}

static void ks_init_kspath(void) {
    const char *env_path = getenv("KS2_KSPATH");
    char cwd[PATH_MAX];
    char candidate[PATH_MAX];
    DIR *d;

    g_kspath[0] = '\0';
    if (env_path && env_path[0] != '\0') {
        if (strlen(env_path) < sizeof(g_kspath)) {
            strcpy(g_kspath, env_path);
            return;
        }
    }

    if (ks_getcwd(cwd, sizeof(cwd)) && ks_try_compose_path(candidate, sizeof(candidate), cwd, "ks") == 0) {
        d = opendir(candidate);
        if (d) {
            closedir(d);
            if (strlen(candidate) < sizeof(g_kspath)) {
                strcpy(g_kspath, candidate);
            }
        }
    }
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

static int host_set_filter(float cutoff_hz, float resonance) {
    if (cutoff_hz < 40.0f || cutoff_hz > 20000.0f) {
        puts("(cutoff must be 40.0-20000.0 Hz)");
        return -1;
    }
    if (resonance < 0.05f || resonance > 1.20f) {
        puts("(resonance must be 0.05-1.20)");
        return -1;
    }
    ksynth_engine_set_filter(cutoff_hz, resonance);
    printf("filter set cutoff=%.1fHz res=%.3f\n", cutoff_hz, resonance);
    return 0;
}

static int host_set_filter_cutoff(float cutoff_hz) {
    if (cutoff_hz < 40.0f || cutoff_hz > 20000.0f) {
        puts("(cutoff must be 40.0-20000.0 Hz)");
        return -1;
    }
    ksynth_engine_set_filter_cutoff(cutoff_hz);
    printf("filter cutoff=%.1fHz\n", cutoff_hz);
    return 0;
}

static int host_set_filter_res(float resonance) {
    if (resonance < 0.05f || resonance > 1.20f) {
        puts("(resonance must be 0.05-1.20)");
        return -1;
    }
    ksynth_engine_set_filter_resonance(resonance);
    printf("filter resonance=%.3f\n", resonance);
    return 0;
}

static int host_set_filter_keytrack(float keytrack) {
    if (keytrack < 0.0f || keytrack > 1.5f) {
        puts("(keytrack must be 0.0-1.5)");
        return -1;
    }
    ksynth_engine_set_filter_keytrack(keytrack);
    printf("filter keytrack=%.3f\n", keytrack);
    return 0;
}

static int host_set_filter_mode(const char *mode) {
    if (!mode) {
        return -1;
    }
    if (strcmp(mode, "lp") == 0) {
        ksynth_engine_set_filter_mode(0);
        puts("filter mode=lp");
        return 0;
    }
    if (strcmp(mode, "bp") == 0) {
        ksynth_engine_set_filter_mode(1);
        puts("filter mode=bp");
        return 0;
    }
    if (strcmp(mode, "hp") == 0) {
        ksynth_engine_set_filter_mode(2);
        puts("filter mode=hp");
        return 0;
    }
    puts("(filtermode must be lp, bp, or hp)");
    return -1;
}

static int host_set_filter_drive(float drive) {
    if (drive < 0.1f || drive > 12.0f) {
        puts("(fdrive must be 0.1-12.0)");
        return -1;
    }
    ksynth_engine_set_filter_drive(drive);
    printf("filter drive=%.3f\n", drive);
    return 0;
}

static int host_set_gain_db(float gain_db) {
    float gain_linear;

    if (gain_db < -96.0f || gain_db > 24.0f) {
        puts("(gain db must be in range -96.0 to +24.0)");
        return -1;
    }

    gain_linear = powf(10.0f, gain_db / 20.0f);
    ksynth_engine_set_gain(gain_linear);
    if (gain_db > 0.0f) {
        printf("gain set db=%.2f (lin=%.4f, overdrive possible)\n", gain_db, gain_linear);
    } else {
        printf("gain set db=%.2f (lin=%.4f)\n", gain_db, gain_linear);
    }
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
    {
        const char *mode = "lp";
        if (s.filter_mode == 1) {
            mode = "bp";
        } else if (s.filter_mode == 2) {
            mode = "hp";
        }
        printf("  filter: mode=%s cutoff=%.1fHz res=%.3f keytrack=%.3f drive=%.3f\n",
               mode, s.filter_cutoff_hz, s.filter_resonance, s.filter_keytrack, s.filter_drive);
    }
    printf("  delay: time=%.1fms feedback=%.3f wet=%.3f\n",
           s.delay_time_ms, s.delay_feedback, s.delay_wet);
    printf("  ch0: pan=%.3f spread=%.3f panlfo=%.3f dsend=%.3f\n",
           s.channel0_pan, s.channel0_pan_spread, s.channel0_pan_lfo_depth, s.channel0_delay_send);
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

static int host_set_channel_pan(int channel, float pan) {
    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (pan < -1.0f || pan > 1.0f) {
        puts("(pan must be -1.0 to 1.0)");
        return -1;
    }
    ksynth_engine_set_channel_pan(channel, pan);
    printf("channel %X pan=%.3f\n", channel, pan);
    return 0;
}

static int host_set_channel_pan_spread(int channel, float spread) {
    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (spread < 0.0f || spread > 1.0f) {
        puts("(panspread must be 0.0 to 1.0)");
        return -1;
    }
    ksynth_engine_set_channel_pan_spread(channel, spread);
    printf("channel %X panspread=%.3f\n", channel, spread);
    return 0;
}

static int host_set_channel_pan_lfo(int channel, float depth) {
    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (depth < 0.0f || depth > 1.0f) {
        puts("(panlfo depth must be 0.0 to 1.0)");
        return -1;
    }
    ksynth_engine_set_channel_pan_lfo_depth(channel, depth);
    printf("channel %X panlfo=%.3f\n", channel, depth);
    return 0;
}

static int host_set_channel_delay_send_db(int channel, float send_db) {
    float send;

    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }
    if (send_db < -96.0f || send_db > 0.0f) {
        puts("(delay send db must be in range -96.0 to 0.0)");
        return -1;
    }
    send = powf(10.0f, send_db / 20.0f);
    ksynth_engine_set_channel_delay_send(channel, send);
    printf("channel %X delay send db=%.2f (lin=%.3f)\n", channel, send_db, send);
    return 0;
}

static int host_set_delay(float time_ms, float feedback, float wet) {
    if (time_ms < 1.0f || time_ms > 4000.0f) {
        puts("(delay time must be 1.0-4000.0 ms)");
        return -1;
    }
    if (feedback < 0.0f || feedback > 0.98f) {
        puts("(delay feedback must be 0.0-0.98)");
        return -1;
    }
    if (wet < 0.0f || wet > 1.0f) {
        puts("(delay wet must be 0.0-1.0)");
        return -1;
    }
    ksynth_engine_set_delay(time_ms, feedback, wet);
    printf("delay time=%.1fms feedback=%.3f wet=%.3f\n", time_ms, feedback, wet);
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

static int host_trig_wavetable(int channel, int note, int vel127) {
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

    ksynth_engine_note_on_ch(channel, note, (float)vel127 / 127.0f);
    printf("trigwt ch=%X note=%d vel=%d\n", channel, note, vel127);
    return 0;
}

static int host_channel_state(int channel) {
    KSChannelStateSnapshot state;
    const char *mode_text;

    if (channel < 0 || channel >= 16) {
        puts("(channel must be 0-F)");
        return -1;
    }

    ksynth_engine_get_channel_state(channel, &state);
    mode_text = (state.mode == KS_CHANNEL_MONO) ? "mono" : "poly";
    printf("ch=%X mode=%s glide=%.1fms held=%d stack=%d top=%d active=%d\n",
           channel,
           mode_text,
           state.glide_ms,
           state.held_count,
           state.stack_len,
           state.top_note,
           state.active_voice_count);
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
    char resolved[PATH_MAX];
    static const char *script_exts[] = { ".ks2.txt", ".txt", ".ks2", ".ks" };

    if (!path || path[0] == '\0') {
        puts("(script path missing)");
        return -1;
    }
    if (g_script_depth >= 8) {
        puts("(script nesting too deep)");
        return -1;
    }

    if (ks_resolve_input_path(path, resolved, sizeof(resolved),
                              script_exts, (int)(sizeof(script_exts) / sizeof(script_exts[0]))) != 0) {
        ks_print_path_context("script file", path, "':script <name>.ks2.txt' or ':script ks/<name>.ks2.txt'");
        return -1;
    }

    fp = fopen(resolved, "rb");
    if (!fp) {
        ks_print_path_context("script file", path, "':script <name>.ks2.txt' or ':script ks/<name>.ks2.txt'");
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
    printf("script %s done (%d lines)\n", resolved, lineno);
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

static int ks_has_wildcards(const char *s) {
    if (!s) {
        return 0;
    }
    return strchr(s, '*') != NULL || strchr(s, '?') != NULL;
}

static int ks_names_append(char ***names, int *count, int *cap, const char *text) {
    char *name;

    if (!names || !count || !cap || !text) {
        return -1;
    }
    if (*count >= *cap) {
        int next_cap = *cap == 0 ? 32 : *cap * 2;
        char **next_names = realloc(*names, (size_t)next_cap * sizeof(char *));
        if (!next_names) {
            return -1;
        }
        *names = next_names;
        *cap = next_cap;
    }
    name = ks_strdup(text);
    if (!name) {
        return -1;
    }
    (*names)[(*count)++] = name;
    return 0;
}

static void ks_names_free(char **names, int count) {
    int i;
    if (!names) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

static void ks_print_names_columns(char **names, int count) {
    int i;
    int col_width = 0;
    int cols;
    int rows;
    int width;

    if (!names || count <= 0) {
        return;
    }

    for (i = 0; i < count; i++) {
        int len = (int)strlen(names[i]);
        if (len > col_width) {
            col_width = len;
        }
    }

    col_width += 2;
    width = ks_terminal_width();
    cols = width / col_width;
    if (cols < 1) {
        cols = 1;
    }
    rows = (count + cols - 1) / cols;

    for (i = 0; i < rows; i++) {
        int c;
        for (c = 0; c < cols; c++) {
            int idx = i + (c * rows);
            if (idx >= count) {
                continue;
            }
            if (c == cols - 1 || idx + rows >= count) {
                printf("%s", names[idx]);
            } else {
                printf("%-*s", col_width, names[idx]);
            }
        }
        putchar('\n');
    }
}

static int host_ls(const char *path) {
    DIR *dir;
    struct dirent *ent;
    const char *target = path;
    char **names = NULL;
    int count = 0;
    int cap = 0;

    if (!target || target[0] == '\0') {
        target = ".";
    }

    if (ks_has_wildcards(target)) {
#if defined(_WIN32)
        WIN32_FIND_DATAA fd;
        HANDLE h;
        h = FindFirstFileA(target, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            puts("(no matches)");
            return -1;
        }
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            if (ks_names_append(&names, &count, &cap, fd.cFileName) != 0) {
                FindClose(h);
                ks_names_free(names, count);
                puts("(out of memory)");
                return -1;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        glob_t g;
        int grc;
        size_t gi;

        memset(&g, 0, sizeof(g));
        grc = glob(target, GLOB_TILDE, NULL, &g);
        if (grc != 0) {
            globfree(&g);
            puts("(no matches)");
            return -1;
        }
        for (gi = 0; gi < g.gl_pathc; gi++) {
            if (ks_names_append(&names, &count, &cap, g.gl_pathv[gi]) != 0) {
                globfree(&g);
                ks_names_free(names, count);
                puts("(out of memory)");
                return -1;
            }
        }
        globfree(&g);
#endif
        if (count == 0) {
            ks_names_free(names, count);
            puts("(no matches)");
            return -1;
        }
        ks_print_names_columns(names, count);
        ks_names_free(names, count);
        return 0;
    }

    dir = opendir(target);
    if (!dir) {
        puts("(could not open directory)");
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (ks_names_append(&names, &count, &cap, ent->d_name) != 0) {
            closedir(dir);
            ks_names_free(names, count);
            puts("(out of memory)");
            return -1;
        }
    }
    closedir(dir);

    if (count == 0) {
        ks_names_free(names, count);
        return 0;
    }

    ks_print_names_columns(names, count);
    ks_names_free(names, count);
    return 0;
}

static int host_cd(const char *path) {
    char cwd[PATH_MAX];

    if (!path || path[0] == '\0') {
        if (ks_getcwd(cwd, sizeof(cwd))) {
            puts(cwd);
            return 0;
        }
        puts("(could not read current directory)");
        return -1;
    }

    if (ks_chdir(path) != 0) {
        puts("(could not change directory)");
        return -1;
    }
    if (ks_getcwd(cwd, sizeof(cwd))) {
        puts(cwd);
    }
    return 0;
}

static int host_kspath(const char *path) {
    DIR *d;

    if (!path || path[0] == '\0') {
        if (g_kspath[0] == '\0') {
            puts("(kspath not set)");
        } else {
            printf("%s\n", g_kspath);
        }
        return 0;
    }

    d = opendir(path);
    if (!d) {
        puts("(kspath must be an existing directory)");
        return -1;
    }
    closedir(d);

    if (strlen(path) >= sizeof(g_kspath)) {
        puts("(kspath too long)");
        return -1;
    }
    strcpy(g_kspath, path);
    printf("kspath=%s\n", g_kspath);
    return 0;
}

static int host_load_patch(const char *path) {
    char *text;
    K* result;
    K* w;
    char resolved[PATH_MAX];
    static const char *patch_exts[] = { ".ks" };

    if (ks_resolve_input_path(path, resolved, sizeof(resolved),
                              patch_exts, (int)(sizeof(patch_exts) / sizeof(patch_exts[0]))) != 0) {
        ks_print_path_context("patch file", path, "':load <name>.ks' or ':load ks/<name>.ks'");
        return -1;
    }

    text = read_text_file(resolved);
    if (!text) {
        ks_print_path_context("patch file", path, "':load <name>.ks' or ':load ks/<name>.ks'");
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
        printf("loaded %s, W len=%d\n", resolved, w->n);
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
    const char *arg;

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
            return 1;
        }
        return 1;
    }
    if (strcmp(line, ":kspath") == 0) {
        host_kspath(NULL);
        return 1;
    }
    if (strncmp(line, ":kspath ", 8) == 0) {
        arg = line + 8;
        while (*arg && isspace((unsigned char)*arg)) {
            arg++;
        }
        host_kspath(arg);
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
    if (sscanf(line, ":filter %f %f", &v1, &v2) == 2) {
        host_set_filter(v1, v2);
        return 1;
    }
    if (sscanf(line, ":cutoff %f", &v1) == 1) {
        host_set_filter_cutoff(v1);
        return 1;
    }
    if (sscanf(line, ":res %f", &v1) == 1) {
        host_set_filter_res(v1);
        return 1;
    }
    if (sscanf(line, ":keytrack %f", &v1) == 1) {
        host_set_filter_keytrack(v1);
        return 1;
    }
    if (sscanf(line, ":filtermode %31s", word) == 1) {
        host_set_filter_mode(word);
        return 1;
    }
    if (sscanf(line, ":fdrive %f", &v1) == 1) {
        host_set_filter_drive(v1);
        return 1;
    }
    if (sscanf(line, ":gain %f", &v1) == 1) {
        host_set_gain_db(v1);
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
    if (sscanf(line, ":pan %x %f", &channel, &v1) == 2) {
        host_set_channel_pan(channel, v1);
        return 1;
    }
    if (sscanf(line, ":panspread %x %f", &channel, &v1) == 2) {
        host_set_channel_pan_spread(channel, v1);
        return 1;
    }
    if (sscanf(line, ":panlfo %x %f", &channel, &v1) == 2) {
        host_set_channel_pan_lfo(channel, v1);
        return 1;
    }
    if (sscanf(line, ":chsenddelay %x %f", &channel, &v1) == 2) {
        host_set_channel_delay_send_db(channel, v1);
        return 1;
    }
    if (sscanf(line, ":delay %f %f %f", &v1, &v2, &v3) == 3) {
        host_set_delay(v1, v2, v3);
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
    if (sscanf(line, ":trigwt %x %d %d", &channel, &note_i, &vel127) == 3) {
        host_trig_wavetable(channel, note_i, vel127);
        return 1;
    }
    if (sscanf(line, ":chstate %x", &channel) == 1) {
        host_channel_state(channel);
        return 1;
    }
    if (sscanf(line, ":sleep %31s", word) == 1) {
        host_sleep(word);
        return 1;
    }
    if (strcmp(line, ":ls") == 0) {
        host_ls(NULL);
        return 1;
    }
    if (strncmp(line, ":ls ", 4) == 0) {
        arg = line + 4;
        while (*arg && isspace((unsigned char)*arg)) {
            arg++;
        }
        host_ls(arg);
        return 1;
    }
    if (strcmp(line, ":cd") == 0) {
        host_cd(NULL);
        return 1;
    }
    if (strncmp(line, ":cd ", 4) == 0) {
        arg = line + 4;
        while (*arg && isspace((unsigned char)*arg)) {
            arg++;
        }
        host_cd(arg);
        return 1;
    }

    puts("(unknown command)");
    return 1;
}

int main(void) {
    char line[KS_REPL_MAX_LINE];
    int i;

    signal(SIGINT, ks_handle_sigint);
    ks_init_kspath();
    ks_history_init();
    if (audio_init(44100, 2, 256) != 0) {
        ks_history_shutdown();
        return 1;
    }

    audio_start();
    printf("KSynth v%s — to learn what to do next, enter :help<ret>\n\n", KS2_VERSION);

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
