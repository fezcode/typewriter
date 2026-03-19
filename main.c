/*
 * typewriter - a lightweight typewriter-style text editor
 *
 * Dependencies: SDL2, SDL2_ttf
 * Build: mkdir build && cd build && cmake .. && cmake --build . --config Release
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Configuration ─────────────────────────────────────────────── */

#define WINDOW_W        900
#define WINDOW_H        640
#define FONT_SIZE       18
#define TAB_STOP        4
#define SCROLL_SPEED    3
#define MARGIN_LEFT     60
#define MARGIN_TOP      50
#define MARGIN_RIGHT    60
#define CURSOR_BLINK_MS 530
#define BELL_COLUMN     80
#define MAX_PATH_LEN    4096
#define UNDO_MAX        512

/* Colors (R, G, B) */
#define COL_PAPER_R   245
#define COL_PAPER_G   240
#define COL_PAPER_B   230

#define COL_TEXT_R    44
#define COL_TEXT_G    44
#define COL_TEXT_B    44

#define COL_CURSOR_R  180
#define COL_CURSOR_G  40
#define COL_CURSOR_B  40

#define COL_LNUM_R    180
#define COL_LNUM_G    175
#define COL_LNUM_B    165

#define COL_STATUS_BG_R  60
#define COL_STATUS_BG_G  58
#define COL_STATUS_BG_B  54

#define COL_STATUS_FG_R  210
#define COL_STATUS_FG_G  205
#define COL_STATUS_FG_B  195

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Line buffer ───────────────────────────────────────────────── */

typedef struct {
    char *text;
    int   len;
    int   cap;
} Line;

typedef struct {
    Line *lines;
    int   count;
    int   cap;
    int   cx, cy;       /* cursor col, row */
    int   scroll_y;     /* first visible line */
    int   sel_active;
    int   sel_ax, sel_ay; /* selection anchor */
    int   dirty;
    char  filepath[MAX_PATH_LEN];
} Doc;

/* ── Undo system ───────────────────────────────────────────────── */

typedef enum {
    UNDO_INSERT_CHAR,
    UNDO_DELETE_CHAR,
    UNDO_INSERT_LINE,
    UNDO_DELETE_LINE,
    UNDO_JOIN_LINES,
    UNDO_SPLIT_LINE,
} UndoType;

typedef struct {
    UndoType type;
    int      line, col;
    char     ch;
    char    *text;      /* for line ops; owned, must free */
} UndoEntry;

static UndoEntry g_undo[UNDO_MAX];
static int       g_undo_count = 0;
static int       g_undo_pos   = 0; /* next slot to write */

/* ── Sound ─────────────────────────────────────────────────────── */

#define SAMPLE_RATE 44100

typedef struct {
    float *samples;
    int    len;
} SoundBuf;

static SoundBuf snd_click;
static SoundBuf snd_clack;    /* enter */
static SoundBuf snd_space;
static SoundBuf snd_bell;
static SoundBuf snd_backspace;

static SDL_AudioDeviceID audio_dev;

/* ── Globals ───────────────────────────────────────────────────── */

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static TTF_Font     *g_font;
static int           g_char_w;
static int           g_char_h;
static int           g_running = 1;
static int           g_need_redraw = 1;
static Doc           g_doc;

/* ── Forward declarations ──────────────────────────────────────── */

static void doc_init(Doc *d);
static void doc_free(Doc *d);
static void doc_insert_char(Doc *d, char c);
static void doc_delete_back(Doc *d);
static void doc_delete_forward(Doc *d);
static void doc_insert_newline(Doc *d);
static void doc_ensure_line(Doc *d, int idx);
static int  doc_load(Doc *d, const char *path);
static int  doc_save(Doc *d);
static int  doc_visible_lines(void);

/* ── Utility ───────────────────────────────────────────────────── */

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "out of memory\n"); exit(1); }
    return q;
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static int min_i(int a, int b) { return a < b ? a : b; }

/* ── Sound generation ──────────────────────────────────────────── */

static float randf(void) {
    return (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
}

static SoundBuf gen_sound(float dur, float freq_lo, float freq_hi,
                          float attack, float decay, float vol) {
    SoundBuf sb;
    sb.len = (int)(dur * SAMPLE_RATE);
    sb.samples = (float *)xmalloc(sb.len * sizeof(float));
    for (int i = 0; i < sb.len; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env;
        if (t < attack)
            env = t / attack;
        else
            env = 1.0f - (t - attack) / (dur - attack);
        if (env < 0.0f) env = 0.0f;
        env *= env; /* sharper decay */

        /* Mix noise + tone */
        float noise = randf() * 0.6f;
        float freq = freq_lo + (freq_hi - freq_lo) * (t / dur);
        float tone = sinf(2.0f * (float)M_PI * freq * t) * 0.4f;
        sb.samples[i] = (noise + tone) * env * vol;
    }
    return sb;
}

static SoundBuf gen_bell_sound(float dur, float freq, float vol) {
    SoundBuf sb;
    sb.len = (int)(dur * SAMPLE_RATE);
    sb.samples = (float *)xmalloc(sb.len * sizeof(float));
    for (int i = 0; i < sb.len; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = expf(-t * 6.0f);
        float s = sinf(2.0f * (float)M_PI * freq * t) * 0.6f
                + sinf(2.0f * (float)M_PI * freq * 2.0f * t) * 0.25f
                + sinf(2.0f * (float)M_PI * freq * 3.01f * t) * 0.15f;
        sb.samples[i] = s * env * vol;
    }
    return sb;
}

static void sound_init(void) {
    SDL_AudioSpec want = {0}, have;
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_F32SYS;
    want.channels = 1;
    want.samples  = 512;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "warning: no audio: %s\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_dev, 0);
    }

    srand((unsigned)SDL_GetTicks());
    snd_click     = gen_sound(0.025f, 800, 2000, 0.001f, 0.024f, 0.35f);
    snd_clack     = gen_sound(0.06f,  300, 600,  0.002f, 0.058f, 0.45f);
    snd_space     = gen_sound(0.03f,  500, 1200, 0.001f, 0.029f, 0.25f);
    snd_backspace = gen_sound(0.03f,  600, 1500, 0.001f, 0.029f, 0.30f);
    snd_bell      = gen_bell_sound(0.3f, 1800.0f, 0.30f);
}

static void play_sound(SoundBuf *sb) {
    if (audio_dev && sb->samples) {
        /* Clear queued audio to avoid latency buildup */
        Uint32 queued = SDL_GetQueuedAudioSize(audio_dev);
        if (queued > SAMPLE_RATE * sizeof(float) / 4)
            SDL_ClearQueuedAudio(audio_dev);
        SDL_QueueAudio(audio_dev, sb->samples, sb->len * sizeof(float));
    }
}

static void sound_cleanup(void) {
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    free(snd_click.samples);
    free(snd_clack.samples);
    free(snd_space.samples);
    free(snd_backspace.samples);
    free(snd_bell.samples);
}

/* ── Undo ──────────────────────────────────────────────────────── */

static void undo_push(UndoType type, int line, int col, char ch, const char *text) {
    UndoEntry *e = &g_undo[g_undo_pos % UNDO_MAX];
    /* Free previous text if overwriting */
    if (e->text) { free(e->text); e->text = NULL; }
    e->type = type;
    e->line = line;
    e->col  = col;
    e->ch   = ch;
    e->text = text ? SDL_strdup(text) : NULL;
    g_undo_pos++;
    if (g_undo_count < UNDO_MAX) g_undo_count++;
    /* After pushing, we can't redo anything */
}

static void undo_pop(Doc *d) {
    if (g_undo_count == 0) return;
    g_undo_count--;
    g_undo_pos--;
    UndoEntry *e = &g_undo[g_undo_pos % UNDO_MAX];

    switch (e->type) {
    case UNDO_INSERT_CHAR: {
        /* An insert was done, so we delete it */
        Line *ln = &d->lines[e->line];
        if (e->col < ln->len) {
            memmove(&ln->text[e->col], &ln->text[e->col + 1], ln->len - e->col - 1);
            ln->len--;
        }
        d->cy = e->line; d->cx = e->col;
    } break;
    case UNDO_DELETE_CHAR: {
        /* A char was deleted, re-insert it */
        Line *ln = &d->lines[e->line];
        if (ln->len + 1 >= ln->cap) {
            ln->cap = (ln->len + 1) * 2;
            ln->text = (char *)xrealloc(ln->text, ln->cap);
        }
        memmove(&ln->text[e->col + 1], &ln->text[e->col], ln->len - e->col);
        ln->text[e->col] = e->ch;
        ln->len++;
        d->cy = e->line; d->cx = e->col + 1;
    } break;
    case UNDO_SPLIT_LINE: {
        /* A newline was inserted, join lines back */
        if (e->line + 1 < d->count) {
            Line *cur  = &d->lines[e->line];
            Line *next = &d->lines[e->line + 1];
            int need = cur->len + next->len + 1;
            if (need >= cur->cap) { cur->cap = need * 2; cur->text = (char *)xrealloc(cur->text, cur->cap); }
            memcpy(&cur->text[cur->len], next->text, next->len);
            int old_len = cur->len;
            cur->len += next->len;
            free(next->text);
            memmove(&d->lines[e->line + 1], &d->lines[e->line + 2],
                    (d->count - e->line - 2) * sizeof(Line));
            d->count--;
            d->cy = e->line; d->cx = old_len;
        }
    } break;
    case UNDO_JOIN_LINES: {
        /* Lines were joined, split them back */
        Line *ln = &d->lines[e->line];
        int split_at = e->col;
        /* Insert new line */
        if (d->count + 1 >= d->cap) {
            d->cap *= 2;
            d->lines = (Line *)xrealloc(d->lines, d->cap * sizeof(Line));
        }
        memmove(&d->lines[e->line + 2], &d->lines[e->line + 1],
                (d->count - e->line - 1) * sizeof(Line));
        d->count++;
        Line *newln = &d->lines[e->line + 1];
        int tail = ln->len - split_at;
        newln->cap = tail > 16 ? tail * 2 : 16;
        newln->text = (char *)xmalloc(newln->cap);
        memcpy(newln->text, &ln->text[split_at], tail);
        newln->len = tail;
        ln->len = split_at;
        d->cy = e->line + 1; d->cx = 0;
    } break;
    case UNDO_INSERT_LINE:
        /* A line was inserted, remove it */
        if (e->line < d->count) {
            free(d->lines[e->line].text);
            memmove(&d->lines[e->line], &d->lines[e->line + 1],
                    (d->count - e->line - 1) * sizeof(Line));
            d->count--;
            d->cy = e->line > 0 ? e->line - 1 : 0;
            d->cx = d->lines[d->cy].len;
        }
        break;
    case UNDO_DELETE_LINE:
        /* A line was deleted, re-insert it */
        if (d->count + 1 >= d->cap) {
            d->cap *= 2;
            d->lines = (Line *)xrealloc(d->lines, d->cap * sizeof(Line));
        }
        memmove(&d->lines[e->line + 1], &d->lines[e->line],
                (d->count - e->line) * sizeof(Line));
        d->count++;
        d->lines[e->line].len = e->text ? (int)strlen(e->text) : 0;
        d->lines[e->line].cap = d->lines[e->line].len + 16;
        d->lines[e->line].text = (char *)xmalloc(d->lines[e->line].cap);
        if (e->text) memcpy(d->lines[e->line].text, e->text, d->lines[e->line].len);
        d->cy = e->line; d->cx = 0;
        break;
    }

    if (e->text) { free(e->text); e->text = NULL; }
    g_need_redraw = 1;
}

/* ── Document operations ───────────────────────────────────────── */

static void line_init(Line *ln) {
    ln->cap  = 64;
    ln->len  = 0;
    ln->text = (char *)xmalloc(ln->cap);
}

static void doc_init(Doc *d) {
    memset(d, 0, sizeof(*d));
    d->cap   = 64;
    d->lines = (Line *)xmalloc(d->cap * sizeof(Line));
    d->count = 1;
    line_init(&d->lines[0]);
}

static void doc_free(Doc *d) {
    for (int i = 0; i < d->count; i++) free(d->lines[i].text);
    free(d->lines);
}

static void doc_ensure_line(Doc *d, int idx) {
    while (d->count <= idx) {
        if (d->count >= d->cap) {
            d->cap *= 2;
            d->lines = (Line *)xrealloc(d->lines, d->cap * sizeof(Line));
        }
        line_init(&d->lines[d->count]);
        d->count++;
    }
}

static void line_insert_char(Line *ln, int pos, char c) {
    if (ln->len + 1 >= ln->cap) {
        ln->cap *= 2;
        ln->text = (char *)xrealloc(ln->text, ln->cap);
    }
    memmove(&ln->text[pos + 1], &ln->text[pos], ln->len - pos);
    ln->text[pos] = c;
    ln->len++;
}

static void doc_insert_char(Doc *d, char c) {
    doc_ensure_line(d, d->cy);
    Line *ln = &d->lines[d->cy];
    int pos = clamp(d->cx, 0, ln->len);
    undo_push(UNDO_INSERT_CHAR, d->cy, pos, c, NULL);
    line_insert_char(ln, pos, c);
    d->cx = pos + 1;
    d->dirty = 1;

    /* Bell at margin */
    if (d->cx == BELL_COLUMN)
        play_sound(&snd_bell);
}

static void doc_delete_back(Doc *d) {
    if (d->cx > 0) {
        Line *ln = &d->lines[d->cy];
        d->cx = clamp(d->cx, 0, ln->len);
        if (d->cx > 0) {
            d->cx--;
            undo_push(UNDO_DELETE_CHAR, d->cy, d->cx, ln->text[d->cx], NULL);
            memmove(&ln->text[d->cx], &ln->text[d->cx + 1], ln->len - d->cx - 1);
            ln->len--;
            d->dirty = 1;
        }
    } else if (d->cy > 0) {
        /* Join with previous line */
        Line *prev = &d->lines[d->cy - 1];
        Line *cur  = &d->lines[d->cy];
        int old_len = prev->len;
        undo_push(UNDO_JOIN_LINES, d->cy - 1, old_len, 0, NULL);
        if (prev->len + cur->len + 1 >= prev->cap) {
            prev->cap = (prev->len + cur->len + 1) * 2;
            prev->text = (char *)xrealloc(prev->text, prev->cap);
        }
        memcpy(&prev->text[prev->len], cur->text, cur->len);
        prev->len += cur->len;
        free(cur->text);
        memmove(&d->lines[d->cy], &d->lines[d->cy + 1],
                (d->count - d->cy - 1) * sizeof(Line));
        d->count--;
        d->cy--;
        d->cx = old_len;
        d->dirty = 1;
    }
}

static void doc_delete_forward(Doc *d) {
    Line *ln = &d->lines[d->cy];
    if (d->cx < ln->len) {
        undo_push(UNDO_DELETE_CHAR, d->cy, d->cx, ln->text[d->cx], NULL);
        memmove(&ln->text[d->cx], &ln->text[d->cx + 1], ln->len - d->cx - 1);
        ln->len--;
        d->dirty = 1;
    } else if (d->cy + 1 < d->count) {
        /* Join with next line */
        Line *next = &d->lines[d->cy + 1];
        undo_push(UNDO_JOIN_LINES, d->cy, ln->len, 0, NULL);
        if (ln->len + next->len + 1 >= ln->cap) {
            ln->cap = (ln->len + next->len + 1) * 2;
            ln->text = (char *)xrealloc(ln->text, ln->cap);
        }
        memcpy(&ln->text[ln->len], next->text, next->len);
        ln->len += next->len;
        free(next->text);
        memmove(&d->lines[d->cy + 1], &d->lines[d->cy + 2],
                (d->count - d->cy - 2) * sizeof(Line));
        d->count--;
        d->dirty = 1;
    }
}

static void doc_insert_newline(Doc *d) {
    doc_ensure_line(d, d->cy);
    Line *ln = &d->lines[d->cy];
    d->cx = clamp(d->cx, 0, ln->len);

    undo_push(UNDO_SPLIT_LINE, d->cy, d->cx, 0, NULL);

    /* Make room for new line */
    if (d->count + 1 >= d->cap) {
        d->cap *= 2;
        d->lines = (Line *)xrealloc(d->lines, d->cap * sizeof(Line));
    }
    memmove(&d->lines[d->cy + 2], &d->lines[d->cy + 1],
            (d->count - d->cy - 1) * sizeof(Line));
    d->count++;

    /* Split text */
    Line *newln = &d->lines[d->cy + 1];
    int tail = ln->len - d->cx;
    newln->cap = tail > 16 ? tail * 2 : 16;
    newln->text = (char *)xmalloc(newln->cap);
    memcpy(newln->text, &ln->text[d->cx], tail);
    newln->len = tail;
    ln->len = d->cx;

    d->cy++;
    d->cx = 0;
    d->dirty = 1;
}

static int doc_visible_lines(void) {
    int ww, wh;
    SDL_GetWindowSize(g_win, &ww, &wh);
    return (wh - MARGIN_TOP - 30) / g_char_h; /* 30px for status bar */
}

static void doc_scroll_to_cursor(Doc *d) {
    int vis = doc_visible_lines();
    if (d->cy < d->scroll_y)
        d->scroll_y = d->cy;
    else if (d->cy >= d->scroll_y + vis)
        d->scroll_y = d->cy - vis + 1;
}

static int doc_load(Doc *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* Clear existing doc */
    for (int i = 0; i < d->count; i++) free(d->lines[i].text);
    d->count = 0;
    d->cx = d->cy = d->scroll_y = 0;
    d->dirty = 0;
    snprintf(d->filepath, MAX_PATH_LEN, "%s", path);

    char buf[8192];
    int line_idx = 0;
    doc_ensure_line(d, 0);

    while (fgets(buf, sizeof(buf), f)) {
        int len = (int)strlen(buf);
        /* Strip \r\n or \n */
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) len--;

        doc_ensure_line(d, line_idx);
        Line *ln = &d->lines[line_idx];
        if (len >= ln->cap) {
            ln->cap = len * 2;
            ln->text = (char *)xrealloc(ln->text, ln->cap);
        }
        memcpy(ln->text, buf, len);
        ln->len = len;
        line_idx++;
    }
    if (d->count == 0) doc_ensure_line(d, 0);

    fclose(f);
    /* Reset undo on load */
    g_undo_count = g_undo_pos = 0;
    return 0;
}

static int doc_save(Doc *d) {
    if (d->filepath[0] == '\0') return -1;
    FILE *f = fopen(d->filepath, "wb");
    if (!f) return -1;
    for (int i = 0; i < d->count; i++) {
        fwrite(d->lines[i].text, 1, d->lines[i].len, f);
        fputc('\n', f);
    }
    fclose(f);
    d->dirty = 0;
    return 0;
}

/* ── Selection helpers ─────────────────────────────────────────── */

static void sel_start(Doc *d) {
    if (!d->sel_active) {
        d->sel_active = 1;
        d->sel_ax = d->cx;
        d->sel_ay = d->cy;
    }
}

static void sel_clear(Doc *d) {
    d->sel_active = 0;
}

static void sel_get_range(Doc *d, int *r1, int *c1, int *r2, int *c2) {
    if (d->sel_ay < d->cy || (d->sel_ay == d->cy && d->sel_ax <= d->cx)) {
        *r1 = d->sel_ay; *c1 = d->sel_ax;
        *r2 = d->cy;     *c2 = d->cx;
    } else {
        *r1 = d->cy;     *c1 = d->cx;
        *r2 = d->sel_ay;  *c2 = d->sel_ax;
    }
}

static void sel_delete(Doc *d) {
    if (!d->sel_active) return;
    int r1, c1, r2, c2;
    sel_get_range(d, &r1, &c1, &r2, &c2);
    d->cy = r1; d->cx = c1;

    if (r1 == r2) {
        Line *ln = &d->lines[r1];
        memmove(&ln->text[c1], &ln->text[c2], ln->len - c2);
        ln->len -= (c2 - c1);
    } else {
        /* Keep start of first line + end of last line */
        Line *first = &d->lines[r1];
        Line *last  = &d->lines[r2];
        int tail = last->len - c2;
        if (c1 + tail >= first->cap) {
            first->cap = (c1 + tail) * 2;
            first->text = (char *)xrealloc(first->text, first->cap);
        }
        memcpy(&first->text[c1], &last->text[c2], tail);
        first->len = c1 + tail;
        /* Remove intermediate lines */
        for (int i = r1 + 1; i <= r2; i++) free(d->lines[i].text);
        int removed = r2 - r1;
        memmove(&d->lines[r1 + 1], &d->lines[r2 + 1],
                (d->count - r2 - 1) * sizeof(Line));
        d->count -= removed;
    }
    sel_clear(d);
    d->dirty = 1;
}

static char *sel_get_text(Doc *d) {
    if (!d->sel_active) return NULL;
    int r1, c1, r2, c2;
    sel_get_range(d, &r1, &c1, &r2, &c2);

    /* Calculate total size */
    int total = 0;
    for (int i = r1; i <= r2; i++) {
        int start = (i == r1) ? c1 : 0;
        int end   = (i == r2) ? c2 : d->lines[i].len;
        total += (end - start);
        if (i < r2) total++; /* newline */
    }

    char *text = (char *)xmalloc(total + 1);
    int pos = 0;
    for (int i = r1; i <= r2; i++) {
        int start = (i == r1) ? c1 : 0;
        int end   = (i == r2) ? c2 : d->lines[i].len;
        memcpy(&text[pos], &d->lines[i].text[start], end - start);
        pos += (end - start);
        if (i < r2) text[pos++] = '\n';
    }
    text[pos] = '\0';
    return text;
}

/* ── Font loading ──────────────────────────────────────────────── */

/* Try multiple typewriter/monospace fonts in order of preference */
static const char *font_candidates[] = {
    /* Typewriter-style fonts */
    "C:/Windows/Fonts/cour.ttf",         /* Courier New (Windows) */
    "C:/Windows/Fonts/courbd.ttf",       /* Courier New Bold (Windows) */
    "C:/Windows/Fonts/lucon.ttf",        /* Lucida Console (Windows) */
    "/Library/Fonts/Courier New.ttf",    /* macOS */
    "/System/Library/Fonts/Courier.dfont", /* macOS system */
    "/Library/Fonts/American Typewriter.ttf", /* macOS typewriter */
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", /* Linux */
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    "/usr/share/fonts/gnu-free/FreeMono.ttf",
    "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    NULL
};

static TTF_Font *load_font(int size) {
    /* First try bundled font next to executable */
    char exe_path[MAX_PATH_LEN];
    /* Try "typewriter.ttf" next to the binary */
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(exe_path, sizeof(exe_path), "%stypewriter.ttf", base);
        TTF_Font *f = TTF_OpenFont(exe_path, size);
        if (f) return f;
    }

    /* Try font path from environment */
    const char *env_font = SDL_getenv("TYPEWRITER_FONT");
    if (env_font) {
        TTF_Font *f = TTF_OpenFont(env_font, size);
        if (f) return f;
    }

    /* Try system fonts */
    for (int i = 0; font_candidates[i]; i++) {
        TTF_Font *f = TTF_OpenFont(font_candidates[i], size);
        if (f) return f;
    }
    return NULL;
}

/* ── Rendering ─────────────────────────────────────────────────── */

static void render_text(const char *text, int len, int x, int y,
                        SDL_Color col) {
    if (len <= 0) return;
    /* Build null-terminated substring */
    char *buf = (char *)xmalloc(len + 1);
    memcpy(buf, text, len);
    buf[len] = '\0';

    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font, buf, col);
    free(buf);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(g_ren, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void render(Doc *d) {
    int ww, wh;
    SDL_GetWindowSize(g_win, &ww, &wh);

    /* Paper background */
    SDL_SetRenderDrawColor(g_ren, COL_PAPER_R, COL_PAPER_G, COL_PAPER_B, 255);
    SDL_RenderClear(g_ren);

    /* Subtle left margin line */
    SDL_SetRenderDrawColor(g_ren, 220, 200, 190, 255);
    SDL_RenderDrawLine(g_ren, MARGIN_LEFT - 10, 0, MARGIN_LEFT - 10, wh - 28);

    SDL_Color text_col  = { COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, 255 };
    SDL_Color lnum_col  = { COL_LNUM_R, COL_LNUM_G, COL_LNUM_B, 255 };
    SDL_Color sel_col_bg = { 180, 210, 230, 255 };

    int vis = doc_visible_lines();
    int sel_r1 = 0, sel_c1 = 0, sel_r2 = 0, sel_c2 = 0;
    if (d->sel_active)
        sel_get_range(d, &sel_r1, &sel_c1, &sel_r2, &sel_c2);

    for (int i = 0; i < vis && d->scroll_y + i < d->count; i++) {
        int li = d->scroll_y + i;
        int y = MARGIN_TOP + i * g_char_h;
        Line *ln = &d->lines[li];

        /* Line number */
        char lnum[16];
        snprintf(lnum, sizeof(lnum), "%3d", li + 1);
        render_text(lnum, (int)strlen(lnum), 8, y, lnum_col);

        /* Selection highlight */
        if (d->sel_active && li >= sel_r1 && li <= sel_r2) {
            int sc = (li == sel_r1) ? sel_c1 : 0;
            int ec = (li == sel_r2) ? sel_c2 : ln->len;
            if (sc < ec) {
                SDL_Rect hr = {
                    MARGIN_LEFT + sc * g_char_w, y,
                    (ec - sc) * g_char_w, g_char_h
                };
                SDL_SetRenderDrawColor(g_ren, sel_col_bg.r, sel_col_bg.g, sel_col_bg.b, 255);
                SDL_RenderFillRect(g_ren, &hr);
            }
        }

        /* Line text */
        if (ln->len > 0)
            render_text(ln->text, ln->len, MARGIN_LEFT, y, text_col);
    }

    /* Cursor */
    Uint32 now = SDL_GetTicks();
    if ((now / CURSOR_BLINK_MS) % 2 == 0) {
        int cx_screen = MARGIN_LEFT + d->cx * g_char_w;
        int cy_screen = MARGIN_TOP + (d->cy - d->scroll_y) * g_char_h;
        if (d->cy >= d->scroll_y && d->cy < d->scroll_y + vis) {
            SDL_SetRenderDrawColor(g_ren, COL_CURSOR_R, COL_CURSOR_G, COL_CURSOR_B, 200);
            SDL_Rect cr = { cx_screen, cy_screen, 2, g_char_h };
            SDL_RenderFillRect(g_ren, &cr);
            /* Underscore style second cursor indicator */
            SDL_Rect ul = { cx_screen, cy_screen + g_char_h - 2, g_char_w, 2 };
            SDL_RenderFillRect(g_ren, &ul);
        }
    }

    /* Status bar */
    SDL_Rect sb = { 0, wh - 28, ww, 28 };
    SDL_SetRenderDrawColor(g_ren, COL_STATUS_BG_R, COL_STATUS_BG_G, COL_STATUS_BG_B, 255);
    SDL_RenderFillRect(g_ren, &sb);

    char status[512];
    char fname_short[128];
    const char *fname = d->filepath[0] ? d->filepath : "[untitled]";
    /* Truncate displayed filename to fit status bar */
    snprintf(fname_short, sizeof(fname_short), "%.120s", fname);
    snprintf(status, sizeof(status), " %s%s  |  Ln %d, Col %d  |  %d lines  |  Ctrl+S save  Ctrl+O open  Ctrl+Q quit",
             fname_short, d->dirty ? " *" : "", d->cy + 1, d->cx + 1, d->count);
    SDL_Color status_col = { COL_STATUS_FG_R, COL_STATUS_FG_G, COL_STATUS_FG_B, 255 };
    render_text(status, (int)strlen(status), 8, wh - 24, status_col);

    SDL_RenderPresent(g_ren);
    g_need_redraw = 0;
}

/* ── Clipboard ─────────────────────────────────────────────────── */

static void clipboard_copy(Doc *d) {
    char *text = sel_get_text(d);
    if (text) {
        SDL_SetClipboardText(text);
        free(text);
    }
}

static void clipboard_paste(Doc *d) {
    if (!SDL_HasClipboardText()) return;
    char *text = SDL_GetClipboardText();
    if (!text) return;
    if (d->sel_active) sel_delete(d);
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\r') continue;
        if (text[i] == '\n')
            doc_insert_newline(d);
        else
            doc_insert_char(d, text[i]);
    }
    SDL_free(text);
    play_sound(&snd_click);
}

/* ── Simple file dialog (native on each platform via zenity/powershell/osascript) */

static int file_dialog_open(char *out, int out_size) {
#ifdef _WIN32
    /* Use PowerShell for file open dialog */
    FILE *p = _popen("powershell -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                     "$f = New-Object System.Windows.Forms.OpenFileDialog; "
                     "$f.Filter = 'Text files|*.txt|All files|*.*'; "
                     "if($f.ShowDialog() -eq 'OK'){$f.FileName}\"", "r");
    if (!p) return -1;
    if (fgets(out, out_size, p)) {
        int len = (int)strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
        _pclose(p);
        return len > 0 ? 0 : -1;
    }
    _pclose(p);
    return -1;
#elif __APPLE__
    FILE *p = popen("osascript -e 'POSIX path of (choose file)' 2>/dev/null", "r");
    if (!p) return -1;
    if (fgets(out, out_size, p)) {
        int len = (int)strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
        pclose(p);
        return len > 0 ? 0 : -1;
    }
    pclose(p);
    return -1;
#else
    /* Try zenity, then kdialog */
    FILE *p = popen("zenity --file-selection 2>/dev/null || kdialog --getopenfilename ~ 2>/dev/null", "r");
    if (!p) return -1;
    if (fgets(out, out_size, p)) {
        int len = (int)strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
        pclose(p);
        return len > 0 ? 0 : -1;
    }
    pclose(p);
    return -1;
#endif
}

static int file_dialog_save(char *out, int out_size) {
#ifdef _WIN32
    FILE *p = _popen("powershell -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                     "$f = New-Object System.Windows.Forms.SaveFileDialog; "
                     "$f.Filter = 'Text files|*.txt|All files|*.*'; "
                     "if($f.ShowDialog() -eq 'OK'){$f.FileName}\"", "r");
    if (!p) return -1;
    if (fgets(out, out_size, p)) {
        int len = (int)strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
        _pclose(p);
        return len > 0 ? 0 : -1;
    }
    _pclose(p);
    return -1;
#elif __APPLE__
    FILE *p = popen("osascript -e 'POSIX path of (choose file name)' 2>/dev/null", "r");
    if (!p) return -1;
    if (fgets(out, out_size, p)) {
        int len = (int)strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
        pclose(p);
        return len > 0 ? 0 : -1;
    }
    pclose(p);
    return -1;
#else
    FILE *p = popen("zenity --file-selection --save 2>/dev/null || kdialog --getsavefilename ~ 2>/dev/null", "r");
    if (!p) return -1;
    if (fgets(out, out_size, p)) {
        int len = (int)strlen(out);
        while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r')) out[--len] = '\0';
        pclose(p);
        return len > 0 ? 0 : -1;
    }
    pclose(p);
    return -1;
#endif
}

/* ── Input handling ────────────────────────────────────────────── */

static void handle_keydown(SDL_KeyboardEvent *ev, Doc *d) {
    int ctrl = (ev->keysym.mod & KMOD_CTRL) != 0;
    int shift = (ev->keysym.mod & KMOD_SHIFT) != 0;

    /* Ctrl shortcuts */
    if (ctrl) {
        switch (ev->keysym.sym) {
        case SDLK_q:
            g_running = 0;
            return;
        case SDLK_s:
            if (d->filepath[0] == '\0') {
                char path[MAX_PATH_LEN] = {0};
                if (file_dialog_save(path, sizeof(path)) == 0) {
                    snprintf(d->filepath, MAX_PATH_LEN, "%s", path);
                }
            }
            if (doc_save(d) == 0) {
                g_need_redraw = 1;
            }
            return;
        case SDLK_o: {
            char path[MAX_PATH_LEN] = {0};
            if (file_dialog_open(path, sizeof(path)) == 0) {
                doc_load(d, path);
                g_need_redraw = 1;
            }
            return;
        }
        case SDLK_z:
            undo_pop(d);
            doc_scroll_to_cursor(d);
            return;
        case SDLK_c:
            clipboard_copy(d);
            return;
        case SDLK_x:
            clipboard_copy(d);
            sel_delete(d);
            g_need_redraw = 1;
            return;
        case SDLK_v:
            clipboard_paste(d);
            doc_scroll_to_cursor(d);
            g_need_redraw = 1;
            return;
        case SDLK_a:
            d->sel_active = 1;
            d->sel_ax = 0; d->sel_ay = 0;
            d->cy = d->count - 1;
            d->cx = d->lines[d->cy].len;
            g_need_redraw = 1;
            return;
        case SDLK_HOME:
            d->cy = 0; d->cx = 0;
            doc_scroll_to_cursor(d);
            g_need_redraw = 1;
            return;
        case SDLK_END:
            d->cy = d->count - 1;
            d->cx = d->lines[d->cy].len;
            doc_scroll_to_cursor(d);
            g_need_redraw = 1;
            return;
        default: break;
        }
    }

    /* Navigation and editing keys */
    switch (ev->keysym.sym) {
    case SDLK_LEFT:
        if (shift) sel_start(d); else sel_clear(d);
        if (ctrl) {
            /* Word left */
            Line *ln = &d->lines[d->cy];
            if (d->cx > 0) {
                d->cx--;
                while (d->cx > 0 && ln->text[d->cx] == ' ') d->cx--;
                while (d->cx > 0 && ln->text[d->cx - 1] != ' ') d->cx--;
            } else if (d->cy > 0) {
                d->cy--; d->cx = d->lines[d->cy].len;
            }
        } else {
            if (d->cx > 0) d->cx--;
            else if (d->cy > 0) { d->cy--; d->cx = d->lines[d->cy].len; }
        }
        break;
    case SDLK_RIGHT:
        if (shift) sel_start(d); else sel_clear(d);
        if (ctrl) {
            Line *ln = &d->lines[d->cy];
            if (d->cx < ln->len) {
                while (d->cx < ln->len && ln->text[d->cx] != ' ') d->cx++;
                while (d->cx < ln->len && ln->text[d->cx] == ' ') d->cx++;
            } else if (d->cy + 1 < d->count) {
                d->cy++; d->cx = 0;
            }
        } else {
            Line *ln = &d->lines[d->cy];
            if (d->cx < ln->len) d->cx++;
            else if (d->cy + 1 < d->count) { d->cy++; d->cx = 0; }
        }
        break;
    case SDLK_UP:
        if (shift) sel_start(d); else sel_clear(d);
        if (d->cy > 0) {
            d->cy--;
            d->cx = min_i(d->cx, d->lines[d->cy].len);
        }
        break;
    case SDLK_DOWN:
        if (shift) sel_start(d); else sel_clear(d);
        if (d->cy + 1 < d->count) {
            d->cy++;
            d->cx = min_i(d->cx, d->lines[d->cy].len);
        }
        break;
    case SDLK_HOME:
        if (shift) sel_start(d); else sel_clear(d);
        d->cx = 0;
        break;
    case SDLK_END:
        if (shift) sel_start(d); else sel_clear(d);
        d->cx = d->lines[d->cy].len;
        break;
    case SDLK_PAGEUP:
        if (shift) sel_start(d); else sel_clear(d);
        d->cy -= doc_visible_lines();
        if (d->cy < 0) d->cy = 0;
        d->cx = min_i(d->cx, d->lines[d->cy].len);
        break;
    case SDLK_PAGEDOWN:
        if (shift) sel_start(d); else sel_clear(d);
        d->cy += doc_visible_lines();
        if (d->cy >= d->count) d->cy = d->count - 1;
        d->cx = min_i(d->cx, d->lines[d->cy].len);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (d->sel_active) sel_delete(d);
        doc_insert_newline(d);
        play_sound(&snd_clack);
        break;
    case SDLK_BACKSPACE:
        if (d->sel_active) {
            sel_delete(d);
            play_sound(&snd_backspace);
        } else {
            doc_delete_back(d);
            play_sound(&snd_backspace);
        }
        break;
    case SDLK_DELETE:
        if (d->sel_active) {
            sel_delete(d);
        } else {
            doc_delete_forward(d);
        }
        play_sound(&snd_backspace);
        break;
    case SDLK_TAB: {
        if (d->sel_active) sel_delete(d);
        for (int t = 0; t < TAB_STOP; t++)
            doc_insert_char(d, ' ');
        play_sound(&snd_space);
        break;
    }
    case SDLK_ESCAPE:
        sel_clear(d);
        break;
    default:
        return; /* Don't set redraw for unhandled keys */
    }

    doc_scroll_to_cursor(d);
    g_need_redraw = 1;
}

static void handle_textinput(SDL_TextInputEvent *ev, Doc *d) {
    if (d->sel_active) sel_delete(d);

    /* Insert each UTF-8 byte (works for ASCII; multi-byte passes through) */
    for (int i = 0; ev->text[i]; i++) {
        char c = ev->text[i];
        if (c == ' ')
            play_sound(&snd_space);
        else
            play_sound(&snd_click);
        doc_insert_char(d, c);
    }
    doc_scroll_to_cursor(d);
    g_need_redraw = 1;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    g_win = SDL_CreateWindow("Typewriter",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    g_ren = SDL_CreateRenderer(g_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) {
        /* Fallback to software renderer */
        g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win); TTF_Quit(); SDL_Quit();
        return 1;
    }

    /* Load font */
    g_font = load_font(FONT_SIZE);
    if (!g_font) {
        fprintf(stderr, "Could not load any font. Place 'typewriter.ttf' next to the executable\n"
                        "or set TYPEWRITER_FONT=/path/to/font.ttf\n");
        SDL_DestroyRenderer(g_ren); SDL_DestroyWindow(g_win);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    /* Measure character size (monospace assumed) */
    TTF_SizeText(g_font, "M", &g_char_w, &g_char_h);

    /* Init sound */
    sound_init();

    /* Init document */
    doc_init(&g_doc);

    /* Load file from command line */
    if (argc > 1) {
        if (doc_load(&g_doc, argv[1]) != 0) {
            fprintf(stderr, "warning: could not open '%s', starting empty\n", argv[1]);
            snprintf(g_doc.filepath, MAX_PATH_LEN, "%s", argv[1]);
        }
    }

    SDL_StartTextInput();

    /* Main loop */
    while (g_running) {
        SDL_Event ev;
        /* Wait for events to save CPU, but wake up for cursor blink */
        int has_event = SDL_WaitEventTimeout(&ev, CURSOR_BLINK_MS / 2);

        if (has_event) {
            do {
                switch (ev.type) {
                case SDL_QUIT:
                    g_running = 0;
                    break;
                case SDL_KEYDOWN:
                    handle_keydown(&ev.key, &g_doc);
                    break;
                case SDL_TEXTINPUT:
                    handle_textinput(&ev.text, &g_doc);
                    break;
                case SDL_MOUSEWHEEL:
                    g_doc.scroll_y -= ev.wheel.y * SCROLL_SPEED;
                    g_doc.scroll_y = clamp(g_doc.scroll_y, 0,
                        g_doc.count > doc_visible_lines() ?
                        g_doc.count - doc_visible_lines() : 0);
                    g_need_redraw = 1;
                    break;
                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
                        ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                        ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                        g_need_redraw = 1;
                    break;
                case SDL_DROPFILE: {
                    char *path = ev.drop.file;
                    doc_load(&g_doc, path);
                    SDL_free(path);
                    g_need_redraw = 1;
                    break;
                }
                }
            } while (SDL_PollEvent(&ev));
        }

        /* Always redraw for cursor blink (cheap with vsync) */
        render(&g_doc);
    }

    SDL_StopTextInput();
    doc_free(&g_doc);
    sound_cleanup();
    if (g_font) TTF_CloseFont(g_font);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    TTF_Quit();
    SDL_Quit();

    /* Free undo entries */
    for (int i = 0; i < UNDO_MAX; i++) {
        if (g_undo[i].text) free(g_undo[i].text);
    }
    return 0;
}
