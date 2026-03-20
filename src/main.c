#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "ksynth.h"
#include "audio.h"
#include "uedit.h"

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

static void print_repl_help(void) {
    puts("KSynth REPL");
    puts("  Enter DSL scripts such as:");
    puts("    N: 44100");
    puts("    T: !N");
    puts("    W: w s +\\(N#(440*(p 2)%44100))");
    puts("    P: ~1024");
    puts("    T: s P");
    puts("    W: w T t 440,N");
    puts("  Commands:");
    puts("    :help   show this help");
    puts("    :quit   exit");
    puts("    :load <file.ks>   evaluate a ksynth patch file");
    puts("    :play <var>       legacy alias for :playsample <var>");
    puts("    :playwt <var>     stop the sequence and audition a vector as a wavetable");
    puts("    :playsample <var> stop the sequence and audition a vector as a sample");
    puts("    :stop             stop the sequencer and silence active voices");
    puts("    :start            resume the sequencer transport");
    puts("    :wt <0-3> <var>   bank a vector variable into an engine wavetable slot");
    puts("    :sample <hex> <var> bank a vector variable into a sample slot");
    puts("    :slot <hex> <var> legacy alias for :sample");
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
    char var_name;
    char path[512];

    if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) {
        return -1;
    }
    if (strcmp(line, ":help") == 0 || strcmp(line, ":h") == 0) {
        print_repl_help();
        return 1;
    }
    if (strcmp(line, ":slots") == 0) {
        host_list_slots();
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
    if (sscanf(line, ":sample %x %c", &slot, &var_name) == 2) {
        host_bank_sample(slot, var_name);
        return 1;
    }
    if (sscanf(line, ":slot %x %c", &slot, &var_name) == 2) {
        host_bank_sample(slot, var_name);
        return 1;
    }

    puts("(unknown command)");
    return 1;
}

int main(void) {
    char line[UEDIT_MAX_LINE];
    int i;

    if (audio_init(44100, 2, 256) != 0) return 1;

    audio_start();
    print_repl_help();
    puts("");

    while (1) {
        K* result;
        int nread;

        nread = uedit("ks> ", line, (int)sizeof(line));
        if (nread < 0) {
            break;
        }

        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == ':') {
            nread = handle_repl_command(line);
            if (nread < 0) {
                break;
            }
            continue;
        }

        result = k_eval_script(line);
        if (result) {
            p(result);
            k_free(result);
        } else {
            puts("(error)");
        }
    }

    audio_stop();
    audio_shutdown();
    for (i = 0; i < 16; i++) {
        if (g_host.slots[i]) {
            k_free(g_host.slots[i]);
        }
    }
    return 0;
}
