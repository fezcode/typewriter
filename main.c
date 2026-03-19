/*
 * typewriter - a lightweight typewriter-style text editor
 *
 * Dependencies: SDL2, SDL2_ttf
 * Build: make  (or)  gcc -O2 main.c -o typewriter $(pkg-config --cflags --libs sdl2 SDL2_ttf) -lm
 */

#define TYPEWRITER_VERSION "0.2.0"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Embedded sound data (PCM float32, 44100Hz, mono) */
#include "snd_click.h"
#include "snd_clack.h"
#include "snd_space.h"
#include "snd_backspace.h"
#include "snd_bell.h"

/* ── Configuration ─────────────────────────────────────────────── */

#define WINDOW_W        900
#define WINDOW_H        640
#define FONT_SIZE       18
#define TAB_STOP        4
#define SCROLL_SPEED    3
#define MARGIN_LEFT_MIN 20
#define MARGIN_TOP      50
#define GUTTER_PAD      14  /* pixels between line numbers and text */
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

/* ── Options ───────────────────────────────────────────────────── */

typedef struct {
    int sound_enabled;
    int show_line_numbers;
    int show_notebook_lines;
} Options;

#define MENU_ITEM_COUNT 3

static const char *menu_labels[MENU_ITEM_COUNT] = {
    "Sound effects",
    "Line numbers",
    "Notebook lines",
};

/* ── Globals ───────────────────────────────────────────────────── */

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static TTF_Font     *g_font;
static int           g_char_w;
static int           g_char_h;
static int           g_running = 1;
static int           g_need_redraw = 1;
static Doc           g_doc;
static Options       g_opts = { 1, 1, 0 }; /* sound=on, lnums=on, lines=off */
static int           g_menu_open = 0;
static int           g_menu_sel  = 0;
static int           g_quit_dialog = 0; /* save-before-quit dialog */
static int           g_quit_sel    = 0; /* 0=Save, 1=Don't Save, 2=Cancel */

/* ── Forward declarations ──────────────────────────────────────── */

static void doc_grow(Doc *d);
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

/* How many decimal digits in n (at least 1) */
static int digit_count(int n) {
    if (n < 0) n = -n;
    int d = 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

/* Compute the left margin (gutter width) based on line count */
static int compute_text_x(int line_count) {
    if (!g_opts.show_line_numbers) return MARGIN_LEFT_MIN;
    int digits = digit_count(line_count);
    if (digits < 3) digits = 3; /* minimum 3 digits wide */
    return (digits + 1) * g_char_w + GUTTER_PAD;
}

/* ── Sound (embedded PCM data) ─────────────────────────────────── */

static SoundBuf soundbuf_from_embed(const float *data, int len) {
    SoundBuf sb;
    sb.len = len;
    sb.samples = (float *)xmalloc(len * sizeof(float));
    memcpy(sb.samples, data, len * sizeof(float));
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

    snd_click     = soundbuf_from_embed(snd_embed_click_data,     snd_embed_click_LEN);
    snd_clack     = soundbuf_from_embed(snd_embed_clack_data,     snd_embed_clack_LEN);
    snd_space     = soundbuf_from_embed(snd_embed_space_data,     snd_embed_space_LEN);
    snd_backspace = soundbuf_from_embed(snd_embed_backspace_data, snd_embed_backspace_LEN);
    snd_bell      = soundbuf_from_embed(snd_embed_bell_data,      snd_embed_bell_LEN);
}

static void play_sound(SoundBuf *sb) {
    if (!g_opts.sound_enabled) return;
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
        if (d->count + 1 >= d->cap)
            doc_grow(d);
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
        if (d->count + 1 >= d->cap)
            doc_grow(d);
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

static void doc_grow(Doc *d) {
    int new_cap = d->cap < 1024 ? d->cap * 2 : d->cap + d->cap / 2;
    if (new_cap <= d->cap) new_cap = d->cap + 1024; /* overflow guard */
    d->lines = (Line *)xrealloc(d->lines, (size_t)new_cap * sizeof(Line));
    d->cap = new_cap;
}

static void doc_ensure_line(Doc *d, int idx) {
    while (d->count <= idx) {
        if (d->count >= d->cap)
            doc_grow(d);
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

    int text_x = compute_text_x(d->count);

    /* Subtle left margin line */
    if (g_opts.show_line_numbers) {
        SDL_SetRenderDrawColor(g_ren, 220, 200, 190, 255);
        SDL_RenderDrawLine(g_ren, text_x - 10, 0, text_x - 10, wh - 28);
    }

    SDL_Color text_col  = { COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, 255 };
    SDL_Color lnum_col  = { COL_LNUM_R, COL_LNUM_G, COL_LNUM_B, 255 };
    SDL_Color sel_col_bg = { 180, 210, 230, 255 };

    int vis = doc_visible_lines();
    int sel_r1 = 0, sel_c1 = 0, sel_r2 = 0, sel_c2 = 0;
    if (d->sel_active)
        sel_get_range(d, &sel_r1, &sel_c1, &sel_r2, &sel_c2);

    /* Notebook lines — draw across the full visible area */
    if (g_opts.show_notebook_lines) {
        SDL_SetRenderDrawColor(g_ren, 195, 215, 230, 255);
        for (int i = 0; i < vis + 1; i++) {
            int ly = MARGIN_TOP + i * g_char_h + g_char_h - 1;
            if (ly < wh - 28)
                SDL_RenderDrawLine(g_ren, 0, ly, ww, ly);
        }
        /* Red margin line for notebook look */
        SDL_SetRenderDrawColor(g_ren, 220, 140, 140, 180);
        int margin_x = text_x - 6;
        SDL_RenderDrawLine(g_ren, margin_x, 0, margin_x, wh - 28);
    }

    for (int i = 0; i < vis && d->scroll_y + i < d->count; i++) {
        int li = d->scroll_y + i;
        int y = MARGIN_TOP + i * g_char_h;
        Line *ln = &d->lines[li];

        /* Line number — right-aligned in the gutter */
        if (g_opts.show_line_numbers) {
            char lnum[32];
            int digits = digit_count(d->count);
            if (digits < 3) digits = 3;
            snprintf(lnum, sizeof(lnum), "%*d", digits, li + 1);
            int lnum_w = (int)strlen(lnum) * g_char_w;
            render_text(lnum, (int)strlen(lnum), text_x - GUTTER_PAD - lnum_w, y, lnum_col);
        }

        /* Selection highlight */
        if (d->sel_active && li >= sel_r1 && li <= sel_r2) {
            int sc = (li == sel_r1) ? sel_c1 : 0;
            int ec = (li == sel_r2) ? sel_c2 : ln->len;
            if (sc < ec) {
                SDL_Rect hr = {
                    text_x + sc * g_char_w, y,
                    (ec - sc) * g_char_w, g_char_h
                };
                SDL_SetRenderDrawColor(g_ren, sel_col_bg.r, sel_col_bg.g, sel_col_bg.b, 255);
                SDL_RenderFillRect(g_ren, &hr);
            }
        }

        /* Line text */
        if (ln->len > 0)
            render_text(ln->text, ln->len, text_x, y, text_col);
    }

    /* Cursor */
    Uint32 now = SDL_GetTicks();
    if ((now / CURSOR_BLINK_MS) % 2 == 0) {
        int cx_screen = text_x + d->cx * g_char_w;
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
    snprintf(status, sizeof(status), " %s%s  |  Ln %d, Col %d  |  %d lines  |  Ctrl+K options",
             fname_short, d->dirty ? " *" : "", d->cy + 1, d->cx + 1, d->count);
    SDL_Color status_col = { COL_STATUS_FG_R, COL_STATUS_FG_G, COL_STATUS_FG_B, 255 };
    render_text(status, (int)strlen(status), 8, wh - 24, status_col);

    /* ── Options menu overlay ── */
    if (g_menu_open) {
        /* Semi-transparent backdrop */
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 140);
        SDL_Rect overlay = { 0, 0, ww, wh };
        SDL_RenderFillRect(g_ren, &overlay);

        /* Shortcuts reference */
        static const char *shortcut_keys[] = {
            "Ctrl+S", "Ctrl+O", "Ctrl+Q", "Ctrl+Z",
            "Ctrl+C", "Ctrl+X", "Ctrl+V", "Ctrl+A",
        };
        static const char *shortcut_desc[] = {
            "Save", "Open", "Quit", "Undo",
            "Copy", "Cut", "Paste", "Select all",
        };
        #define SHORTCUT_COUNT 8

        /* Menu panel — sized to fit options + shortcuts */
        int opts_h = MENU_ITEM_COUNT * 32;
        int shorts_h = SHORTCUT_COUNT * 22;
        int panel_w = 400;
        int panel_h = 48 + opts_h + 20 + shorts_h + 44;
        int panel_x = (ww - panel_w) / 2;
        int panel_y = (wh - panel_h) / 2;

        /* Panel background */
        SDL_SetRenderDrawColor(g_ren, 50, 48, 44, 240);
        SDL_Rect panel = { panel_x, panel_y, panel_w, panel_h };
        SDL_RenderFillRect(g_ren, &panel);

        /* Panel border */
        SDL_SetRenderDrawColor(g_ren, 90, 85, 78, 255);
        SDL_RenderDrawRect(g_ren, &panel);

        /* Title */
        SDL_Color title_col = { 230, 225, 215, 255 };
        const char *title = "Options";
        render_text(title, (int)strlen(title), panel_x + 16, panel_y + 12, title_col);

        /* Separator */
        SDL_SetRenderDrawColor(g_ren, 80, 76, 70, 255);
        SDL_RenderDrawLine(g_ren, panel_x + 12, panel_y + 40, panel_x + panel_w - 12, panel_y + 40);

        /* Toggle items */
        int *opt_ptrs[MENU_ITEM_COUNT] = {
            &g_opts.sound_enabled,
            &g_opts.show_line_numbers,
            &g_opts.show_notebook_lines,
        };

        for (int mi = 0; mi < MENU_ITEM_COUNT; mi++) {
            int item_y = panel_y + 50 + mi * 32;

            if (mi == g_menu_sel) {
                SDL_SetRenderDrawColor(g_ren, 75, 70, 64, 255);
                SDL_Rect hl = { panel_x + 6, item_y - 2, panel_w - 12, 28 };
                SDL_RenderFillRect(g_ren, &hl);
            }

            SDL_Color item_col = (mi == g_menu_sel)
                ? (SDL_Color){ 255, 245, 220, 255 }
                : (SDL_Color){ 190, 185, 175, 255 };

            char row[128];
            snprintf(row, sizeof(row), "[%c] %s",
                     *opt_ptrs[mi] ? 'x' : ' ', menu_labels[mi]);
            render_text(row, (int)strlen(row), panel_x + 20, item_y + 2, item_col);
        }

        /* Shortcuts section */
        int sc_top = panel_y + 50 + opts_h + 8;
        SDL_SetRenderDrawColor(g_ren, 80, 76, 70, 255);
        SDL_RenderDrawLine(g_ren, panel_x + 12, sc_top, panel_x + panel_w - 12, sc_top);

        SDL_Color sc_title_col = { 200, 195, 185, 255 };
        const char *sc_title = "Keyboard Shortcuts";
        render_text(sc_title, (int)strlen(sc_title), panel_x + 16, sc_top + 6, sc_title_col);

        SDL_Color sc_key_col  = { 180, 175, 165, 255 };
        SDL_Color sc_desc_col = { 145, 140, 132, 255 };
        for (int si = 0; si < SHORTCUT_COUNT; si++) {
            int sy = sc_top + 28 + si * 22;
            render_text(shortcut_keys[si], (int)strlen(shortcut_keys[si]),
                        panel_x + 24, sy, sc_key_col);
            render_text(shortcut_desc[si], (int)strlen(shortcut_desc[si]),
                        panel_x + 24 + 10 * g_char_w, sy, sc_desc_col);
        }

        /* Footer hint */
        SDL_Color hint_col = { 140, 135, 128, 255 };
        const char *hint = "Up/Down  Enter=toggle  Esc=close";
        render_text(hint, (int)strlen(hint), panel_x + 16,
                    panel_y + panel_h - 24, hint_col);

        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
    }

    /* ── Save-before-quit dialog ── */
    if (g_quit_dialog) {
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 160);
        SDL_Rect overlay = { 0, 0, ww, wh };
        SDL_RenderFillRect(g_ren, &overlay);

        int qw = 420, qh = 130;
        int qx = (ww - qw) / 2, qy = (wh - qh) / 2;

        /* Panel */
        SDL_SetRenderDrawColor(g_ren, 50, 48, 44, 245);
        SDL_Rect qpanel = { qx, qy, qw, qh };
        SDL_RenderFillRect(g_ren, &qpanel);
        SDL_SetRenderDrawColor(g_ren, 90, 85, 78, 255);
        SDL_RenderDrawRect(g_ren, &qpanel);

        /* Message */
        SDL_Color msg_col = { 230, 225, 215, 255 };
        const char *msg = "You have unsaved changes.";
        render_text(msg, (int)strlen(msg), qx + 20, qy + 16, msg_col);

        /* Buttons */
        static const char *btn_labels[3] = { "[S]ave", "[D]on't Save", "[C]ancel" };
        int btn_x = qx + 20;
        for (int bi = 0; bi < 3; bi++) {
            int bw = (int)strlen(btn_labels[bi]) * g_char_w + 24;
            int bh = g_char_h + 16;
            int by = qy + qh - bh - 18;

            /* Button background */
            if (bi == g_quit_sel) {
                SDL_SetRenderDrawColor(g_ren, 100, 95, 85, 255);
            } else {
                SDL_SetRenderDrawColor(g_ren, 65, 62, 56, 255);
            }
            SDL_Rect brect = { btn_x, by, bw, bh };
            SDL_RenderFillRect(g_ren, &brect);

            /* Button border */
            SDL_SetRenderDrawColor(g_ren, 110, 105, 95, 255);
            SDL_RenderDrawRect(g_ren, &brect);

            /* Button text */
            SDL_Color bcol = (bi == g_quit_sel)
                ? (SDL_Color){ 255, 245, 220, 255 }
                : (SDL_Color){ 180, 175, 165, 255 };
            render_text(btn_labels[bi], (int)strlen(btn_labels[bi]),
                        btn_x + 12, by + 8, bcol);

            btn_x += bw + 12;
        }

        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
    }

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

/* ── Native file dialogs ───────────────────────────────────────── */

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>

static int file_dialog_open(char *out, int out_size) {
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    memset(out, 0, out_size);
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = NULL;
    ofn.lpstrFilter  = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = out;
    ofn.nMaxFile     = out_size;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle   = "Open File";
    return GetOpenFileNameA(&ofn) ? 0 : -1;
}

static int file_dialog_save(char *out, int out_size) {
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    memset(out, 0, out_size);
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = NULL;
    ofn.lpstrFilter  = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = out;
    ofn.nMaxFile     = out_size;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt  = "txt";
    ofn.lpstrTitle   = "Save File";
    return GetSaveFileNameA(&ofn) ? 0 : -1;
}

#elif __APPLE__

static int file_dialog_open(char *out, int out_size) {
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
}

static int file_dialog_save(char *out, int out_size) {
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
}

#else /* Linux */

static int file_dialog_open(char *out, int out_size) {
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
}

static int file_dialog_save(char *out, int out_size) {
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
}

#endif

/* ── Options menu helpers ───────────────────────────────────────── */

static int *menu_opt_ptr(int idx) {
    switch (idx) {
    case 0: return &g_opts.sound_enabled;
    case 1: return &g_opts.show_line_numbers;
    case 2: return &g_opts.show_notebook_lines;
    default: return NULL;
    }
}

static void menu_toggle(void) {
    g_menu_open = !g_menu_open;
    g_need_redraw = 1;
}

static void handle_menu_key(SDL_KeyboardEvent *ev) {
    switch (ev->keysym.sym) {
    case SDLK_UP:
        g_menu_sel = (g_menu_sel - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        break;
    case SDLK_DOWN:
        g_menu_sel = (g_menu_sel + 1) % MENU_ITEM_COUNT;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: {
        int *p = menu_opt_ptr(g_menu_sel);
        if (p) *p = !(*p);
        break;
    }
    case SDLK_ESCAPE:
        g_menu_open = 0;
        break;
    default:
        if ((ev->keysym.mod & KMOD_CTRL) && ev->keysym.sym == SDLK_k)
            g_menu_open = 0;
        return;
    }
    g_need_redraw = 1;
}

/* ── Quit dialog ───────────────────────────────────────────────── */

static void try_quit(Doc *d) {
    if (d->dirty) {
        g_quit_dialog = 1;
        g_quit_sel = 0;
        g_need_redraw = 1;
    } else {
        g_running = 0;
    }
}

static void handle_quit_key(SDL_KeyboardEvent *ev, Doc *d) {
    switch (ev->keysym.sym) {
    case SDLK_LEFT:
        g_quit_sel = (g_quit_sel - 1 + 3) % 3;
        break;
    case SDLK_RIGHT:
        g_quit_sel = (g_quit_sel + 1) % 3;
        break;
    case SDLK_TAB:
        g_quit_sel = (g_quit_sel + 1) % 3;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (g_quit_sel == 0) { /* Save */
            if (d->filepath[0] == '\0') {
                char path[MAX_PATH_LEN] = {0};
                if (file_dialog_save(path, sizeof(path)) == 0)
                    snprintf(d->filepath, MAX_PATH_LEN, "%s", path);
            }
            if (doc_save(d) == 0)
                g_running = 0;
            else
                g_quit_dialog = 0; /* save failed, stay open */
        } else if (g_quit_sel == 1) { /* Don't Save */
            g_running = 0;
        } else { /* Cancel */
            g_quit_dialog = 0;
        }
        break;
    case SDLK_ESCAPE:
        g_quit_dialog = 0;
        break;
    case SDLK_s:
        /* Quick shortcut: S = save & quit */
        if (d->filepath[0] == '\0') {
            char path[MAX_PATH_LEN] = {0};
            if (file_dialog_save(path, sizeof(path)) == 0)
                snprintf(d->filepath, MAX_PATH_LEN, "%s", path);
        }
        if (doc_save(d) == 0)
            g_running = 0;
        else
            g_quit_dialog = 0;
        break;
    case SDLK_d:
    case SDLK_n:
        /* Quick shortcut: D/N = don't save */
        g_running = 0;
        break;
    default:
        return;
    }
    g_need_redraw = 1;
}

/* ── Input handling ────────────────────────────────────────────── */

static void handle_keydown(SDL_KeyboardEvent *ev, Doc *d) {
    /* Quit dialog intercepts all input */
    if (g_quit_dialog) {
        handle_quit_key(ev, d);
        return;
    }

    /* Menu intercepts all input when open */
    if (g_menu_open) {
        handle_menu_key(ev);
        return;
    }

    int ctrl = (ev->keysym.mod & KMOD_CTRL) != 0;
    int shift = (ev->keysym.mod & KMOD_SHIFT) != 0;

    /* Ctrl shortcuts */
    if (ctrl) {
        switch (ev->keysym.sym) {
        case SDLK_k:
            menu_toggle();
            return;
        case SDLK_q:
            try_quit(d);
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
    if (g_menu_open || g_quit_dialog) return; /* Ignore typing while dialog is open */
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

static void print_help(const char *prog) {
    printf("typewriter %s - a lightweight typewriter-style text editor\n\n", TYPEWRITER_VERSION);
    printf("Usage: %s [options] [file]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help       Show this help message and exit\n");
    printf("  -v, --version    Show version and exit\n\n");
    printf("Keyboard shortcuts:\n");
    printf("  Ctrl+S           Save file\n");
    printf("  Ctrl+O           Open file\n");
    printf("  Ctrl+Q           Quit\n");
    printf("  Ctrl+Z           Undo\n");
    printf("  Ctrl+C/X/V       Copy / Cut / Paste\n");
    printf("  Ctrl+A           Select all\n");
    printf("  Ctrl+K           Open options menu\n\n");
    printf("Environment:\n");
    printf("  TYPEWRITER_FONT  Path to a .ttf font file\n\n");
    printf("You can also drag and drop files onto the window.\n");
}

int main(int argc, char *argv[]) {
    /* Parse flags before SDL init (so -h works without a display) */
    const char *open_file = NULL;

#ifdef _WIN32
    /* Attach to parent console so printf works when launched from terminal */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("typewriter %s\n", TYPEWRITER_VERSION);
            return 0;
        }
        /* First non-flag arg is the file */
        if (argv[i][0] != '-' && !open_file) {
            open_file = argv[i];
        }
    }

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

    /* Set window icon from icon.png next to the executable */
    {
        const char *base = SDL_GetBasePath();
        if (base) {
            char icon_path[MAX_PATH_LEN];
            snprintf(icon_path, sizeof(icon_path), "%sicon.png", base);
            SDL_Surface *icon = SDL_LoadBMP(icon_path); /* try BMP first */
            if (!icon) {
                /* SDL_image not available; try loading via SDL_RWops trick:
                   fall back — the .ico in the resource file handles Windows exe icon */
            }
            if (icon) {
                SDL_SetWindowIcon(g_win, icon);
                SDL_FreeSurface(icon);
            }
        }
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
    if (open_file) {
        if (doc_load(&g_doc, open_file) != 0) {
            fprintf(stderr, "warning: could not open '%s', starting empty\n", open_file);
            snprintf(g_doc.filepath, MAX_PATH_LEN, "%s", open_file);
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
                    try_quit(&g_doc);
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
