#include "ksynth.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define KS_MAX_LITERAL 1024
#define KS_SR 44100.0

static K* g_vars[26];
static K* g_args[2];

static double safe_val(double v) {
    if (isnan(v) || isinf(v)) {
        return 0.0;
    }
    if (v > 1e6) {
        return 1e6;
    }
    if (v < -1e6) {
        return -1e6;
    }
    return v;
}

static void skip_spaces(char **s) {
    while (s && *s && **s == ' ') {
        (*s)++;
    }
}

static void skip_ws(char **s) {
    while (s && *s && **s && isspace((unsigned char)**s)) {
        (*s)++;
    }
}

static K* k_clone(const K* src) {
    K* dst;
    int cells;

    if (!src) {
        return NULL;
    }
    if (src->n < 0) {
        return k_func(k_func_body((K*)src));
    }

    dst = k_new(src->n);
    if (!dst) {
        return NULL;
    }
    memcpy(dst->f, src->f, (size_t)src->n * sizeof(double));
    cells = src->n;
    (void)cells;
    return dst;
}

static K* k_scalar(double v) {
    K* x = k_new(1);
    if (!x) {
        return NULL;
    }
    x->f[0] = v;
    return x;
}

static double k_at(const K* x, int i) {
    if (!x || x->n <= 0) {
        return 0.0;
    }
    if (x->n == 1) {
        return x->f[0];
    }
    if (i < x->n) {
        return x->f[i];
    }
    return x->f[i % x->n];
}

static K* scan(char op, K* b);
static K* expr(char **s);

K* k_new(int n) {
    K* x;

    if (n < 0) {
        n = 0;
    }
    x = malloc(sizeof(K) + sizeof(double) * (size_t)n);
    if (!x) {
        return NULL;
    }
    x->r = 1;
    x->n = n;
    return x;
}

void k_free(K* x) {
    if (x && --x->r == 0) {
        free(x);
    }
}

K* k_get(char name) {
    K* v;

    if (name >= 'a' && name <= 'z') {
        name = (char)(name - 'a' + 'A');
    }
    if (name < 'A' || name > 'Z') {
        return NULL;
    }

    v = g_vars[name - 'A'];
    if (v) {
        v->r++;
    }
    return v;
}

K* k_func(char* body) {
    K* x;
    size_t len;
    size_t bytes;
    size_t cells;

    if (!body) {
        return NULL;
    }

    len = strlen(body);
    bytes = len + 1;
    cells = (bytes + sizeof(double) - 1) / sizeof(double);
    x = k_new((int)cells);
    if (!x) {
        return NULL;
    }
    x->n = -1;
    memcpy((char*)x->f, body, bytes);
    return x;
}

int k_is_func(K* x) {
    return x && x->n == -1;
}

char* k_func_body(K* x) {
    return k_is_func(x) ? (char*)x->f : NULL;
}

K* k_call(K* fn, K* args[], int nargs) {
    char *body;
    char *cursor;
    K* old_x = g_args[0];
    K* old_y = g_args[1];
    K* result;
    int uses_x;
    int uses_y;
    int required;

    if (!k_is_func(fn)) {
        return NULL;
    }

    body = k_func_body(fn);
    if (!body) {
        return NULL;
    }

    uses_x = strchr(body, 'x') != NULL;
    uses_y = strchr(body, 'y') != NULL;
    required = uses_y ? 2 : (uses_x ? 1 : 0);
    if (nargs < required) {
        fprintf(stderr, "ksynth: function requires %d argument%s, got %d\n",
            required, required == 1 ? "" : "s", nargs);
        return k_new(0);
    }

    g_args[0] = k_new(0);
    g_args[1] = k_new(0);
    if (nargs > 0 && args && args[0]) {
        k_free(g_args[0]);
        args[0]->r++;
        g_args[0] = args[0];
    }
    if (nargs > 1 && args && args[1]) {
        k_free(g_args[1]);
        args[1]->r++;
        g_args[1] = args[1];
    }

    cursor = body;
    result = e(&cursor);

    k_free(g_args[0]);
    k_free(g_args[1]);
    g_args[0] = old_x;
    g_args[1] = old_y;

    return result;
}

static K* scan(char op, K* b) {
    K* x;
    double acc;
    int i;

    if (!b || b->n < 1) {
        return b;
    }

    x = k_new(b->n);
    if (!x) {
        k_free(b);
        return NULL;
    }

    switch (op) {
        case '+':
            acc = 0.0;
            for (i = 0; i < b->n; i++) {
                acc += b->f[i];
                x->f[i] = acc;
            }
            break;
        case '*':
            acc = 1.0;
            for (i = 0; i < b->n; i++) {
                acc *= b->f[i];
                x->f[i] = acc;
            }
            break;
        case '-':
            acc = 0.0;
            for (i = 0; i < b->n; i++) {
                acc -= b->f[i];
                x->f[i] = acc;
            }
            break;
        case '%':
            acc = 1.0;
            for (i = 0; i < b->n; i++) {
                if (b->f[i] != 0.0) {
                    acc /= b->f[i];
                }
                x->f[i] = acc;
            }
            break;
        case '&':
            acc = b->f[0];
            x->f[0] = acc;
            for (i = 1; i < b->n; i++) {
                if (b->f[i] < acc) {
                    acc = b->f[i];
                }
                x->f[i] = acc;
            }
            break;
        case '|':
            acc = b->f[0];
            x->f[0] = acc;
            for (i = 1; i < b->n; i++) {
                if (b->f[i] > acc) {
                    acc = b->f[i];
                }
                x->f[i] = acc;
            }
            break;
        case '^':
            acc = b->f[0];
            x->f[0] = acc;
            for (i = 1; i < b->n; i++) {
                acc = pow(acc, b->f[i]);
                x->f[i] = acc;
            }
            break;
        default:
            memcpy(x->f, b->f, (size_t)b->n * sizeof(double));
            break;
    }

    k_free(b);
    return x;
}

K* mo(char c, K* b) {
    K* x;
    int i;

    if (!b) {
        return NULL;
    }

    if (c >= 'A' && c <= 'Z') {
        K* var = g_vars[c - 'A'];
        if (k_is_func(var)) {
            K* call_args[1];
            call_args[0] = b;
            return k_call(var, call_args, 1);
        }
    }

    if (c == '!') {
        int n = (int)k_at(b, 0);
        if (n < 0) {
            n = 0;
        }
        x = k_new(n);
        if (!x) {
            k_free(b);
            return NULL;
        }
        for (i = 0; i < n; i++) {
            x->f[i] = (double)i;
        }
        k_free(b);
        return x;
    }

    if (c == '~') {
        int n = (int)k_at(b, 0);
        if (n < 1) {
            k_free(b);
            return k_new(0);
        }
        x = k_new(n);
        if (!x) {
            k_free(b);
            return NULL;
        }
        for (i = 0; i < n; i++) {
            x->f[i] = (2.0 * M_PI * (double)i) / (double)n;
        }
        k_free(b);
        return x;
    }

    if (c == '+') {
        double t = 0.0;
        for (i = 0; i < b->n; i++) {
            t += b->f[i];
        }
        x = k_scalar(t);
        k_free(b);
        return x;
    }

    if (c == '>') {
        double m = 0.0;
        for (i = 0; i < b->n; i++) {
            double a = fabs(b->f[i]);
            if (a > m) {
                m = a;
            }
        }
        x = k_scalar(m);
        k_free(b);
        return x;
    }

    if (c == 'w') {
        double peak = 0.0;
        x = k_new(b->n);
        if (!x) {
            k_free(b);
            return NULL;
        }
        for (i = 0; i < b->n; i++) {
            if (fabs(b->f[i]) > peak) {
                peak = fabs(b->f[i]);
            }
        }
        if (peak > 1e-10) {
            double scale = 1.0 / peak;
            for (i = 0; i < b->n; i++) {
                x->f[i] = b->f[i] * scale;
            }
        }
        k_free(b);
        return x;
    }

    if (c == 'j' || c == 'k') {
        if (b->n < 2) {
            k_free(b);
            return k_new(0);
        }
        x = k_new(b->n / 2);
        if (!x) {
            k_free(b);
            return NULL;
        }
        for (i = 0; i < x->n; i++) {
            x->f[i] = b->f[i * 2 + (c == 'k')];
        }
        k_free(b);
        return x;
    }

    if (c == 'v') {
        x = k_new(b->n);
        if (!x) {
            k_free(b);
            return NULL;
        }
        for (i = 0; i < b->n; i++) {
            x->f[i] = floor(b->f[i] * 4.0) / 4.0;
        }
        k_free(b);
        return x;
    }

    x = k_new(b->n);
    if (!x) {
        k_free(b);
        return NULL;
    }

    for (i = 0; i < b->n; i++) {
        double v = b->f[i];

        switch (c) {
            case 's': x->f[i] = sin(v); break;
            case 'c': x->f[i] = cos(v); break;
            case 't': x->f[i] = tan(v); break;
            case 'h': x->f[i] = tanh(v); break;
            case 'a': x->f[i] = fabs(v); break;
            case 'q': x->f[i] = sqrt(fabs(v)); break;
            case 'l': x->f[i] = log(fabs(v) + 1e-10); break;
            case 'e':
                if (v > 100.0) v = 100.0;
                if (v < -100.0) v = -100.0;
                x->f[i] = exp(v);
                break;
            case '_': x->f[i] = floor(v); break;
            case 'r': x->f[i] = ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0; break;
            case 'p': x->f[i] = (v == 0.0) ? KS_SR : M_PI * v; break;
            case 'i': x->f[i] = b->f[b->n - 1 - i]; break;
            case 'x': x->f[i] = exp(-5.0 * v); break;
            case 'd': x->f[i] = tanh(v * 3.0); break;
            case 'm': {
                unsigned int clock = (unsigned int)i;
                unsigned int hh = (clock * 13U) ^ (clock >> 5) ^ (clock * 193U);
                x->f[i] = (hh & 128U) ? 0.7 : -0.7;
                break;
            }
            case 'b': {
                static const double ff[] = {2.43, 3.01, 3.52, 4.11, 5.23, 6.78};
                double ss = 0.0;
                int j;
                for (j = 0; j < 6; j++) {
                    ss += (sin((double)i * 0.1 * ff[j]) > 0.0) ? 1.0 : -1.0;
                }
                x->f[i] = ss / 6.0;
                break;
            }
            case 'u':
                x->f[i] = (i < 10) ? (double)i / 10.0 : 1.0;
                break;
            case 'n':
                x->f[i] = 440.0 * pow(2.0, (v - 69.0) / 12.0);
                break;
            default:
                x->f[i] = v;
                break;
        }
    }

    k_free(b);
    return x;
}

K* dy(char c, K* a, K* b) {
    K* x;
    int i;

    if (!a || !b) {
        k_free(a);
        k_free(b);
        return NULL;
    }

    if (c >= 'A' && c <= 'Z') {
        K* var = g_vars[c - 'A'];
        if (k_is_func(var)) {
            K* call_args[2];
            call_args[0] = a;
            call_args[1] = b;
            return k_call(var, call_args, 2);
        }
    }

    if (c == 'z') {
        int mn = a->n < b->n ? a->n : b->n;
        x = k_new(mn * 2);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < mn; i++) {
            x->f[i * 2] = a->f[i];
            x->f[i * 2 + 1] = b->f[i];
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == 'o') {
        int j;
        x = k_new(a->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < a->n; i++) {
            double acc = 0.0;
            for (j = 0; j < b->n; j++) {
                acc += sin(a->f[i] * b->f[j]);
            }
            x->f[i] = acc;
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == '$') {
        int j;
        x = k_new(a->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < a->n; i++) {
            double acc = 0.0;
            for (j = 0; j < b->n; j++) {
                acc += b->f[j] * sin(a->f[i] * (double)(j + 1));
            }
            x->f[i] = acc;
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == 't') {
        double freq_hz;
        int n_out;
        int tbl_len;
        double phase_inc;
        double phase;

        if (a->n < 1 || b->n < 1) {
            k_free(a);
            k_free(b);
            return k_new(0);
        }

        freq_hz = b->f[0];
        if (b->n >= 2) {
            n_out = (int)b->f[1];
        } else {
            K* nv = g_vars['N' - 'A'];
            n_out = (nv && nv->n > 0) ? (int)nv->f[0] : 0;
        }
        tbl_len = a->n;
        if (n_out < 1 || tbl_len < 1) {
            k_free(a);
            k_free(b);
            return k_new(0);
        }

        phase_inc = freq_hz * (double)tbl_len / KS_SR;
        phase = 0.0;
        x = k_new(n_out);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }

        for (i = 0; i < n_out; i++) {
            int idx;
            int idx2;
            double frac;

            while (phase >= tbl_len) phase -= tbl_len;
            while (phase < 0.0) phase += tbl_len;
            idx = (int)phase;
            frac = phase - idx;
            idx2 = (idx + 1) % tbl_len;
            x->f[i] = a->f[idx] * (1.0 - frac) + a->f[idx2] * frac;
            phase += phase_inc;
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == 'v') {
        double levels = (a->n > 0 && a->f[0] > 0.0) ? a->f[0] : 4.0;
        x = k_new(b->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < b->n; i++) {
            x->f[i] = floor(b->f[i] * levels) / levels;
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == 'f') {
        double b0 = 0.0;
        double b1 = 0.0;
        x = k_new(b->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < b->n; i++) {
            double ct = (a->n > i) ? a->f[i] : a->f[0];
            double rs = (a->n >= 2) ? a->f[1] : 0.0;
            double in;

            if (ct > 0.95) ct = 0.95;
            if (rs > 3.98) rs = 3.98;
            in = b->f[i] - (rs * b1);
            b0 += ct * (in - b0);
            b1 += ct * (b0 - b1);
            b0 = safe_val(b0);
            b1 = safe_val(b1);
            x->f[i] = b1;
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == 'g') {
        double s0 = 0.0;
        double s1 = 0.0;
        double static_f = a->f[0];
        double q_val = (a->n >= 2) ? a->f[1] : 0.5;
        double damp = 1.0 / (q_val < 0.01 ? 0.01 : q_val);

        x = k_new(b->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < b->n; i++) {
            double f_hz = (a->n == b->n) ? a->f[i] : static_f;
            double f_coeff = 2.0 * sin(M_PI * f_hz / KS_SR);
            double hp;

            if (f_coeff > 1.99) f_coeff = 1.99;
            hp = b->f[i] - s0 - damp * s1;
            s1 += f_coeff * hp;
            s0 += f_coeff * s1;
            s0 = safe_val(s0);
            s1 = safe_val(s1);
            x->f[i] = s1;
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == 'y') {
        int dd = (int)a->f[0];
        double g = (a->n > 1) ? a->f[1] : 0.4;

        x = k_new(b->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < b->n; i++) {
            double delayed = (i >= dd) ? x->f[i - dd] : 0.0;
            x->f[i] = safe_val(b->f[i] + (g * delayed));
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == '#') {
        int n = (int)a->f[0];
        x = k_new(n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        if (b->n > 0) {
            for (i = 0; i < n; i++) {
                x->f[i] = b->f[i % b->n];
            }
        }
        k_free(a);
        k_free(b);
        return x;
    }

    if (c == ',') {
        x = k_new(a->n + b->n);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        memcpy(x->f, a->f, (size_t)a->n * sizeof(double));
        memcpy(x->f + a->n, b->f, (size_t)b->n * sizeof(double));
        k_free(a);
        k_free(b);
        return x;
    }

    {
        int mn = a->n > b->n ? a->n : b->n;
        x = k_new(mn);
        if (!x) {
            k_free(a);
            k_free(b);
            return NULL;
        }
        for (i = 0; i < mn; i++) {
            double va = k_at(a, i);
            double vb = k_at(b, i);

            switch (c) {
                case '+': x->f[i] = va + vb; break;
                case '*': x->f[i] = va * vb; break;
                case '-': x->f[i] = va - vb; break;
                case '%': x->f[i] = (vb == 0.0) ? 0.0 : va / vb; break;
                case '^': x->f[i] = safe_val(pow(fabs(va), vb)); break;
                case '&': x->f[i] = (va < vb) ? va : vb; break;
                case '|': x->f[i] = (va > vb) ? va : vb; break;
                case '<': x->f[i] = (va < vb) ? 1.0 : 0.0; break;
                case '>': x->f[i] = (va > vb) ? 1.0 : 0.0; break;
                case '=': x->f[i] = (va == vb) ? 1.0 : 0.0; break;
                default:  x->f[i] = 0.0; break;
            }
        }
    }

    k_free(a);
    k_free(b);
    return x;
}

static K* parse_number_or_literal_vector(char **s) {
    double buf[KS_MAX_LITERAL];
    int n = 0;
    char *ptr = *s;

    while (n < KS_MAX_LITERAL) {
        char *after;
        char *peek;
        int had_space;

        buf[n++] = strtod(ptr, &ptr);
        after = ptr;
        peek = ptr;
        while (*peek == ' ') {
            peek++;
        }
        had_space = (peek != after);

        if (*peek >= '0' && *peek <= '9') {
            ptr = peek;
            continue;
        }
        if (*peek == '-' && peek[1] >= '0' && had_space) {
            ptr = peek;
            continue;
        }
        if (had_space && *peek >= 'A' && *peek <= 'Z' && peek[1] != ':') {
            K* v = g_vars[*peek - 'A'];
            if (v && v->n == 1) {
                buf[n++] = v->f[0];
                ptr = peek + 1;
                continue;
            }
        }
        break;
    }

    *s = ptr;
    if (n == 1) {
        return k_scalar(buf[0]);
    }

    {
        K* x = k_new(n);
        if (!x) {
            return NULL;
        }
        memcpy(x->f, buf, (size_t)n * sizeof(double));
        return x;
    }
}

K* atom(char **s) {
    char c;
    int is_scan;

    skip_spaces(s);
    if (**s == '/') {
        while (**s && **s != '\n') {
            (*s)++;
        }
        if (**s == '\n') {
            (*s)++;
        }
        return atom(s);
    }
    if (!**s || **s == '\n' || **s == ')' || **s == ';') {
        return NULL;
    }

    if (**s == '(') {
        K* x;
        (*s)++;
        x = e(s);
        if (**s == ')') {
            (*s)++;
        }
        return x;
    }

    if (**s == '{') {
        char *start;
        int depth;

        (*s)++;
        start = *s;
        depth = 1;
        while (**s && depth > 0) {
            if (**s == '{') depth++;
            else if (**s == '}') depth--;
            (*s)++;
        }
        if (depth == 0) {
            int len = (int)((*s - 1) - start);
            char *body = malloc((size_t)len + 1);
            K* fn;

            if (!body) {
                return NULL;
            }
            memcpy(body, start, (size_t)len);
            body[len] = '\0';
            fn = k_func(body);
            free(body);
            return fn;
        }
        return NULL;
    }

    c = **s;
    if ((c >= '0' && c <= '9') || (c == '.' && (*s)[1] >= '0') ||
        (c == '-' && (((*s)[1] >= '0' && (*s)[1] <= '9') || (*s)[1] == '.'))) {
        return parse_number_or_literal_vector(s);
    }

    (*s)++;

    if (**s == ':') {
        K* x;

        (*s)++;
        x = expr(s);
        if (c >= 'A' && c <= 'Z') {
            int i = c - 'A';
            if (g_vars[i]) {
                k_free(g_vars[i]);
            }
            if (x) {
                x->r++;
                g_vars[i] = x;
            }
        }
        if (x) {
            x->r++;
        }
        return x;
    }

    if (c >= 'A' && c <= 'Z') {
        K* first = k_get(c);
        if (!first || first->n != 1) {
            return first;
        }

        {
            double buf[KS_MAX_LITERAL];
            int n = 0;
            char *ptr = *s;

            buf[n++] = first->f[0];
            k_free(first);

            while (n < KS_MAX_LITERAL) {
                char *peek = ptr;
                K* v;

                while (*peek == ' ') {
                    peek++;
                }
                if (peek == ptr) {
                    break;
                }
                if (*peek < 'A' || *peek > 'Z' || peek[1] == ':') {
                    break;
                }
                v = g_vars[*peek - 'A'];
                if (!v || v->n != 1) {
                    break;
                }
                buf[n++] = v->f[0];
                ptr = peek + 1;
            }
            *s = ptr;
            if (n == 1) {
                return k_scalar(buf[0]);
            }
            first = k_new(n);
            if (!first) {
                return NULL;
            }
            memcpy(first->f, buf, (size_t)n * sizeof(double));
            return first;
        }
    }

    if (c == 'x') {
        return g_args[0] ? (g_args[0]->r++, g_args[0]) : k_new(0);
    }
    if (c == 'y') {
        return g_args[1] ? (g_args[1]->r++, g_args[1]) : k_new(0);
    }

    is_scan = 0;
    skip_spaces(s);
    if (**s == '\\') {
        is_scan = 1;
        (*s)++;
    }

    {
        K* arg = e(s);
        return is_scan ? scan(c, arg) : mo(c, arg);
    }
}

static int next_token_is_operator(char c) {
    switch (c) {
        case '+': case '-': case '*': case '%': case '^':
        case '&': case '|': case '<': case '>': case '=':
        case ',': case '#': case 'o': case '$': case 'f':
        case 'g': case 'y': case 'z': case 's': case 't':
        case 'h': case 'a': case 'q': case 'l': case 'e':
        case 'r': case 'p': case 'c': case 'i': case 'w':
        case 'd': case 'v': case 'm': case 'b': case 'u':
        case 'j': case 'k': case 'n':
            return 1;
        default:
            return 0;
    }
}

static K* expr(char **s) {
    K* x = atom(s);

    skip_spaces(s);
    if (k_is_func(x) && **s && **s != '\n' && **s != ')' && **s != ';' && **s != '}' && **s != '/') {
        if (!next_token_is_operator(**s)) {
            K* arg = expr(s);
            K* call_args[1];
            K* result;

            call_args[0] = arg;
            result = k_call(x, call_args, 1);
            k_free(x);
            return result;
        }
    }

    if (!**s || **s == '\n' || **s == ')' || **s == ';' || **s == '}' || **s == '/') {
        return x;
    }

    {
        char op = *(*s)++;
        return dy(op, x, expr(s));
    }
}

K* e(char **s) {
    K* x = atom(s);

    skip_spaces(s);
    while (**s == ';') {
        (*s)++;
        if (x) {
            k_free(x);
        }
        skip_spaces(s);
        if (!**s || **s == '\n' || **s == ')' || **s == '}') {
            return k_new(0);
        }
        x = atom(s);
        skip_spaces(s);
    }

    if (k_is_func(x) && **s && **s != '\n' && **s != ')' && **s != ';' && **s != '/') {
        if (!next_token_is_operator(**s)) {
            K* arg = e(s);
            K* call_args[1];
            K* result;

            call_args[0] = arg;
            result = k_call(x, call_args, 1);
            k_free(x);
            return result;
        }
    }

    if (!**s || **s == '\n' || **s == ')' || **s == ';' || **s == '/') {
        return x;
    }

    {
        char op = *(*s)++;
        return dy(op, x, e(s));
    }
}

K* k_eval_script(const char *script) {
    char *cursor;
    K* result = NULL;

    if (!script) {
        return NULL;
    }

    cursor = (char*)script;
    while (*cursor) {
        K* next;

        skip_ws(&cursor);
        if (!*cursor) {
            break;
        }

        next = e(&cursor);
        if (!next) {
            while (*cursor && *cursor != '\n') {
                cursor++;
            }
            if (*cursor == '\n') {
                cursor++;
            }
            continue;
        }

        if (result) {
            k_free(result);
        }
        result = next;

        while (*cursor == ';' || *cursor == '\n' || *cursor == '\r') {
            cursor++;
        }
    }

    return result ? result : k_new(0);
}

void p(K* x) {
    int i;
    int limit;

    if (!x) {
        puts("()");
        return;
    }
    if (k_is_func(x)) {
        printf("{%s}\n", k_func_body(x));
        return;
    }
    if (x->n == 1) {
        printf("[%g]\n", x->f[0]);
        return;
    }

#define K_PRINT_MAX (10)
    limit = x->n < K_PRINT_MAX ? x->n : K_PRINT_MAX;
    putchar('(');
    for (i = 0; i < limit; i++) {
        if (i) {
            printf(" ");
        }
        printf("%g", x->f[i]);
    }
    if (limit < x->n) {
        printf(" ... %g", x->f[x->n-1]);
    }
    printf(") / len:%d\n", x->n);
}

K* ksynth_render_sample(void) {
    K* out = k_new(1);
    float sample_lr[2] = {0.0f, 0.0f};

    if (!out) {
        return NULL;
    }
    ksynth_engine_render_stereo(sample_lr, 1);
    out->f[0] = 0.5f * (double)(sample_lr[0] + sample_lr[1]);
    return out;
}
