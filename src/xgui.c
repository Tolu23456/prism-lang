/* ============================================================
   xgui.c  —  Prism native X11 GUI toolkit  v4.0
   Inspired by Dear ImGui, Flutter, Qt, and iOS UIKit.

   Key design choices:
     • Immediate-mode API (xgui_begin / xgui_end per frame)
     • 64-entry alpha-mask LRU cache  → zero repeated pixmap uploads
     • Inigo-Quilez signed-distance-function for ALL rounded shapes
     • Spring-physics scroll (spring + damping, no lerp sluggishness)
     • 24-entry Xft font LRU cache
     • Viewport clipping (off-screen widgets skipped)
     • PSS-driven theming for every widget state
     • Game-mode raw drawing primitives + key-hold state
   ============================================================ */

#ifdef HAVE_X11

#include "xgui.h"
#include "pss.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ------------------------------------------------------------------ constants */

#define MAX_INPUTS       64
#define INPUT_BUF        1024
#define KEY_BUF          64
#define WIDGET_GAP       10
#define FONT_CACHE_MAX   24
#define MASK_CACHE_MAX   64     /* alpha-mask LRU cache entries */
#define BLINK_HALF       26     /* frames per cursor-blink half-period */
#define SCROLL_STEP      60     /* px per scroll-wheel click */
#define SCROLL_OMEGA     22.0f  /* spring frequency (higher = snappier, critically damped) */
#define SNAP_THRESH      0.35f  /* px threshold to snap scroll to target */
#define SB_MARGIN        4
#define SHADOW_LAYERS    6
#define GRID_MAX_COLS    8

enum { MASK_FILL = 0, MASK_OUTLINE = 1 };

/* ------------------------------------------------------------------ types */

typedef struct { char id[64]; char buf[INPUT_BUF]; } InputState;
typedef struct { char key[164]; XftFont *font; } FCacheEntry;

typedef struct {
    int     w, h, r, bw, type; /* cache key */
    Pixmap  pix;
    Picture pic;
    int     age;
} MCacheEntry;

/* Deferred dropdown popup (drawn last so it overlaps everything) */
typedef struct {
    bool    active;
    int     x, y, w, item_h;
    int     count;
    int     hovered;
    char    id[64];
    char    labels[32][128];
} DeferredDropdown;

/* ------------------------------------------------------------------ XGui struct */

struct XGui {
    /* X11 core */
    Display  *dpy;
    int       screen;
    Window    win;
    Pixmap    backbuf;
    GC        gc;
    XftDraw  *xft;
    Picture   backbuf_pic;
    Atom      wm_delete;
    int       width, height;

    PssTheme  theme;

    /* Layout cursor */
    int       cx, cy, margin;
    bool      in_row;
    int       row_max_h;

    /* Grid layout */
    bool      in_grid;
    int       grid_cols, grid_col_w, grid_col;
    int       grid_row_y0, grid_row_h;

    /* Card stack */
    int       card_x0, card_y0, card_w;
    bool      in_card;

    /* Spring-physics smooth scroll */
    int       scroll_target;
    float     scroll_pos_f;
    float     scroll_vel;
    int       scroll_y;
    int       content_h;       /* measured last frame */

    /* Horizontal scroll */
    int       hscroll_target;
    float     hscroll_pos_f;
    float     hscroll_vel;
    int       hscroll_x;
    int       content_w;

    /* Scrollbar drag */
    bool      sb_dragging;
    int       sb_drag_y0, sb_drag_s0;

    /* Mouse */
    int       mx, my;
    bool      mouse_down, mouse_released;

    /* Key events (single frame) */
    char      key_chars[KEY_BUF];
    int       key_count;
    bool      key_backspace, key_enter;
    bool      key_esc_this_frame;
    bool      key_pgup, key_pgdn, key_home, key_end;
    bool      key_shift;

    /* Key-hold state (for games) — cleared on KeyRelease */
    bool      key_held[256];   /* ASCII 0-255 */
    bool      key_up, key_down, key_left, key_right, key_space;

    /* Input widget state */
    InputState inputs[MAX_INPUTS];
    int        input_count;
    char       focused_id[64];
    char       active_id[64];  /* widget being dragged/pressed */

    /* Font LRU cache */
    FCacheEntry fcache[FONT_CACHE_MAX];
    int         fcache_n;

    /* Alpha-mask LRU cache */
    MCacheEntry mcache[MASK_CACHE_MAX];

    /* Animation frame counter */
    int  frame;

    /* Delta time (ms between frames) */
    float delta_ms;
    struct timespec last_frame_t;

    /* Toast notification */
    char  toast_text[256];
    int   toast_end_frame;

    /* Dropdown popup deferred draw */
    DeferredDropdown dd;

    /* Tooltip */
    char  tip_text[256];
    int   tip_x, tip_y;
    bool  tip_show;

    bool  running;
};

/* ------------------------------------------------------------------ color */

static unsigned long alloc_color(XGui *g, uint32_t rgb) {
    XColor c;
    c.red   = (unsigned short)(((rgb >> 16) & 0xff) * 0x101);
    c.green = (unsigned short)(((rgb >>  8) & 0xff) * 0x101);
    c.blue  = (unsigned short)(( rgb        & 0xff) * 0x101);
    c.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(g->dpy, DefaultColormap(g->dpy, g->screen), &c);
    return c.pixel;
}

static void make_xft_color(XGui *g, uint32_t rgb, XftColor *out) {
    XRenderColor rc;
    rc.red   = (unsigned short)(((rgb >> 16) & 0xff) * 0x101);
    rc.green = (unsigned short)(((rgb >>  8) & 0xff) * 0x101);
    rc.blue  = (unsigned short)(( rgb        & 0xff) * 0x101);
    rc.alpha = 0xffff;
    XftColorAllocValue(g->dpy, DefaultVisual(g->dpy, g->screen),
                       DefaultColormap(g->dpy, g->screen), &rc, out);
}

static void xr_color(uint32_t rgb, unsigned short a16, XRenderColor *out) {
    out->red   = (unsigned short)(((rgb >> 16) & 0xff) * 0x101);
    out->green = (unsigned short)(((rgb >>  8) & 0xff) * 0x101);
    out->blue  = (unsigned short)(( rgb        & 0xff) * 0x101);
    out->alpha = a16;
}

/* ------------------------------------------------------------------ font cache */

static XftFont *font_open_raw(XGui *g, const char *fam, int sz, int wt) {
    XftFont *f = XftFontOpen(g->dpy, g->screen,
        XFT_FAMILY, XftTypeString,  fam,
        XFT_SIZE,   XftTypeDouble,  (double)sz,
        XFT_WEIGHT, XftTypeInteger, wt,
        XFT_ANTIALIAS, XftTypeBool, True, NULL);
    if (!f)
        f = XftFontOpen(g->dpy, g->screen,
            XFT_FAMILY, XftTypeString,  "sans",
            XFT_SIZE,   XftTypeDouble,  (double)sz,
            XFT_ANTIALIAS, XftTypeBool, True, NULL);
    return f;
}

static XftFont *get_font(XGui *g, const char *fam, int sz, int wt) {
    char key[164];
    snprintf(key, sizeof(key), "%s:%d:%d", fam, sz, wt);
    for (int i = 0; i < g->fcache_n; i++)
        if (strcmp(g->fcache[i].key, key) == 0) return g->fcache[i].font;

    XftFont *f = font_open_raw(g, fam, sz, wt);
    if (!f) return NULL;

    if (g->fcache_n < FONT_CACHE_MAX) {
        snprintf(g->fcache[g->fcache_n].key, sizeof(g->fcache[0].key), "%s", key);
        g->fcache[g->fcache_n++].font = f;
    } else {
        XftFontClose(g->dpy, g->fcache[0].font);
        memmove(&g->fcache[0], &g->fcache[1],
                sizeof(FCacheEntry) * (FONT_CACHE_MAX - 1));
        snprintf(g->fcache[FONT_CACHE_MAX - 1].key, sizeof(g->fcache[0].key), "%s", key);
        g->fcache[FONT_CACHE_MAX - 1].font = f;
    }
    return f;
}

static void text_size(XGui *g, XftFont *font, const char *t, int *w, int *h) {
    XGlyphInfo e;
    XftTextExtentsUtf8(g->dpy, font, (const FcChar8 *)t, (int)strlen(t), &e);
    if (w) *w = e.xOff;
    if (h) *h = font->ascent + font->descent;
}

static void draw_text(XGui *g, XftFont *f, const char *t, int x, int y, uint32_t rgb) {
    XftColor c; make_xft_color(g, rgb, &c);
    XftDrawStringUtf8(g->xft, &c, f, x, y, (const FcChar8 *)t, (int)strlen(t));
    XftColorFree(g->dpy, DefaultVisual(g->dpy, g->screen),
                 DefaultColormap(g->dpy, g->screen), &c);
}

/* ------------------------------------------------------------------ mask cache (core performance) */

/* Inigo-Quilez signed-distance-function for a rounded rectangle.
   Returns: +value = inside, -value = outside, 0 = on boundary. */
static float iq_rrect_sdf(float px, float py, float w, float h, float r) {
    float bx = w * 0.5f - r;
    float by = h * 0.5f - r;
    float cx = w * 0.5f, cy = h * 0.5f;
    float qx = fabsf(px + 0.5f - cx) - bx;
    float qy = fabsf(py + 0.5f - cy) - by;
    float d;
    if (qx > 0.0f && qy > 0.0f)
        d = sqrtf(qx * qx + qy * qy) - r;
    else
        d = fmaxf(qx, qy) - r;
    return -d;  /* flip so positive = inside */
}

static unsigned char sdf_to_alpha(float sdf) {
    float a = sdf + 0.5f;   /* 0.5px soft AA edge centred on boundary */
    if (a >= 1.0f) return 255;
    if (a <= 0.0f) return 0;
    return (unsigned char)(a * 255.0f + 0.5f);
}

static bool mask_upload(XGui *g, int w, int h,
                         const unsigned char *data, int stride,
                         Pixmap *pix_out, Picture *pic_out) {
    XRenderPictFormat *a8 = XRenderFindStandardFormat(g->dpy, PictStandardA8);
    if (!a8) return false;
    Pixmap pix = XCreatePixmap(g->dpy, g->win, (unsigned)w, (unsigned)h, 8);
    GC gc8 = XCreateGC(g->dpy, pix, 0, NULL);
    XImage xi; memset(&xi, 0, sizeof(xi));
    xi.width = w; xi.height = h; xi.format = ZPixmap;
    xi.data = (char *)data; xi.byte_order = LSBFirst;
    xi.bitmap_bit_order = LSBFirst; xi.bitmap_pad = 8;
    xi.depth = 8; xi.bytes_per_line = stride; xi.bits_per_pixel = 8;
    XInitImage(&xi);
    XPutImage(g->dpy, pix, gc8, &xi, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XFreeGC(g->dpy, gc8);
    XRenderPictureAttributes pa; memset(&pa, 0, sizeof(pa));
    *pic_out = XRenderCreatePicture(g->dpy, pix, a8, 0, &pa);
    *pix_out = pix;
    return true;
}

static MCacheEntry *mask_evict(XGui *g) {
    int oldest = 0;
    for (int i = 1; i < MASK_CACHE_MAX; i++)
        if (g->mcache[i].age < g->mcache[oldest].age) oldest = i;
    MCacheEntry *e = &g->mcache[oldest];
    if (e->pic) { XRenderFreePicture(g->dpy, e->pic); e->pic = None; }
    if (e->pix) { XFreePixmap(g->dpy, e->pix);        e->pix = 0;   }
    return e;
}

/* Get-or-create an alpha mask Picture. Returns None on failure.
   Mask Pictures are CACHED — caller must NEVER free them. */
static Picture mask_get(XGui *g, int w, int h, int r, int type, int bw) {
    if (w <= 0 || h <= 0) return None;
    /* clamp radius */
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;

    /* cache lookup */
    for (int i = 0; i < MASK_CACHE_MAX; i++) {
        MCacheEntry *e = &g->mcache[i];
        if (e->pic && e->w == w && e->h == h && e->r == r
                   && e->type == type && e->bw == bw) {
            e->age = g->frame;
            return e->pic;
        }
    }

    /* build pixel data */
    int stride = (w + 3) & ~3;
    unsigned char *data = (unsigned char *)calloc((size_t)(stride * h), 1);
    if (!data) return None;

    if (type == MASK_FILL) {
        for (int py = 0; py < h; py++)
            for (int px = 0; px < w; px++)
                data[py * stride + px] = sdf_to_alpha(iq_rrect_sdf((float)px, (float)py, (float)w, (float)h, (float)r));
    } else { /* MASK_OUTLINE */
        float ir = (float)((r > bw) ? r - bw : 0);
        float iw = (float)(w - 2 * bw), ih = (float)(h - 2 * bw);
        for (int py = 0; py < h; py++)
            for (int px = 0; px < w; px++) {
                float outer = (float)sdf_to_alpha(iq_rrect_sdf((float)px, (float)py, (float)w, (float)h, (float)r));
                float inner = 0.0f;
                if (iw > 0 && ih > 0)
                    inner = (float)sdf_to_alpha(iq_rrect_sdf((float)(px - bw), (float)(py - bw), iw, ih, ir));
                float a = outer * (255.0f - inner) / 255.0f;
                data[py * stride + px] = (unsigned char)(a + 0.5f);
            }
    }

    /* upload and cache */
    MCacheEntry *e = mask_evict(g);
    if (!mask_upload(g, w, h, data, stride, &e->pix, &e->pic)) {
        free(data); return None;
    }
    free(data);
    e->w = w; e->h = h; e->r = r; e->type = type; e->bw = bw;
    e->age = g->frame;
    return e->pic;
}

/* ------------------------------------------------------------------ drawing primitives */

static void fill_rect(XGui *g, int x, int y, int w, int h, uint32_t rgb) {
    XSetForeground(g->dpy, g->gc, alloc_color(g, rgb));
    XFillRectangle(g->dpy, g->backbuf, g->gc, x, y, (unsigned)w, (unsigned)h);
}

/* Fill a rounded rect using cached mask + XRender. alpha_16: 0=transparent, 0xffff=opaque */
static void fill_rrect(XGui *g, int x, int y, int w, int h,
                        int r, uint32_t rgb, unsigned short alpha_16) {
    if (w <= 0 || h <= 0) return;
    if (!g->backbuf_pic) { fill_rect(g, x, y, w, h, rgb); return; }
    if (r <= 0 && alpha_16 == 0xffff) { fill_rect(g, x, y, w, h, rgb); return; }

    Picture mask = mask_get(g, w, h, r, MASK_FILL, 0);
    if (!mask) { fill_rect(g, x, y, w, h, rgb); return; }

    XRenderColor sc; xr_color(rgb, alpha_16, &sc);
    Picture src = XRenderCreateSolidFill(g->dpy, &sc);
    XRenderComposite(g->dpy, PictOpOver, src, mask, g->backbuf_pic,
                     0, 0, 0, 0, x, y, (unsigned)w, (unsigned)h);
    XRenderFreePicture(g->dpy, src);
}

static void fill_rounded_rect(XGui *g, int x, int y, int w, int h, int r, uint32_t rgb) {
    fill_rrect(g, x, y, w, h, r, rgb, 0xffff);
}

static void fill_rounded_rect_alpha(XGui *g, int x, int y, int w, int h,
                                     int r, uint32_t rgb, int a255) {
    fill_rrect(g, x, y, w, h, r, rgb, (unsigned short)(a255 * 0x101));
}

static void draw_outline(XGui *g, int x, int y, int w, int h,
                          int r, int bw, uint32_t rgb) {
    if (bw <= 0 || w <= 0 || h <= 0) return;
    if (!g->backbuf_pic) return;
    Picture mask = mask_get(g, w, h, r, MASK_OUTLINE, bw);
    if (!mask) return;
    XRenderColor sc; xr_color(rgb, 0xffff, &sc);
    Picture src = XRenderCreateSolidFill(g->dpy, &sc);
    XRenderComposite(g->dpy, PictOpOver, src, mask, g->backbuf_pic,
                     0, 0, 0, 0, x, y, (unsigned)w, (unsigned)h);
    XRenderFreePicture(g->dpy, src);
}

/* Perfect AA circle via IQ SDF on a circle mask */
static void fill_circle(XGui *g, int x, int y, int r, uint32_t rgb) {
    int d = 2 * r;
    if (d <= 0) return;
    if (!g->backbuf_pic) {
        XSetForeground(g->dpy, g->gc, alloc_color(g, rgb));
        XFillArc(g->dpy, g->backbuf, g->gc, x, y, (unsigned)d, (unsigned)d, 0, 360*64);
        return;
    }
    /* Use fill_rrect with r = half-size (maximum radius = circle) */
    fill_rrect(g, x, y, d, d, r, rgb, 0xffff);
}

/* Multi-layer soft drop shadow using cached AA rounded rects */
static void draw_shadow(XGui *g, int x, int y, int w, int h, int r) {
    typedef struct { int exp, dy, a; } L;
    static const L layers[SHADOW_LAYERS] = {
        {10,9,8},{7,7,12},{5,5,18},{3,3,24},{2,2,30},{1,1,35}
    };
    for (int i = 0; i < SHADOW_LAYERS; i++)
        fill_rounded_rect_alpha(g,
            x - layers[i].exp, y + layers[i].dy - layers[i].exp,
            w + 2*layers[i].exp, h + 2*layers[i].exp,
            r + layers[i].exp, 0x000000, layers[i].a);
}

/* ------------------------------------------------------------------ input state */

static InputState *find_input(XGui *g, const char *id) {
    for (int i = 0; i < g->input_count; i++)
        if (strcmp(g->inputs[i].id, id) == 0) return &g->inputs[i];
    if (g->input_count >= MAX_INPUTS) return &g->inputs[0];
    InputState *s = &g->inputs[g->input_count++];
    snprintf(s->id, sizeof(s->id), "%s", id);
    s->buf[0] = '\0';
    return s;
}

/* ------------------------------------------------------------------ scrollbar geometry */

static void sb_geo(XGui *g, int *sbx, int *sby, int *sbh, int *thy, int *thh) {
    int sw = g->theme.scrollbar.min_width;
    if (sw < 6) sw = 8;
    *sbx = g->width - sw - SB_MARGIN;
    *sby = SB_MARGIN;
    *sbh = g->height - 2 * SB_MARGIN;
    float ratio = (g->content_h > 0)
                  ? (float)g->height / (float)g->content_h : 1.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    *thh = (int)(ratio * (float)(*sbh));
    if (*thh < 24) *thh = 24;
    int maxs = g->content_h - g->height;
    float t = (maxs > 0) ? (float)g->scroll_y / (float)maxs : 0.0f;
    if (t < 0.0f) { t = 0.0f; } else if (t > 1.0f) { t = 1.0f; }
    *thy = *sby + (int)(t * (float)(*sbh - *thh));
}

/* ------------------------------------------------------------------ backbuffer picture */

static void make_backbuf_pic(XGui *g) {
    XRenderPictFormat *vf =
        XRenderFindVisualFormat(g->dpy, DefaultVisual(g->dpy, g->screen));
    if (!vf) { g->backbuf_pic = None; return; }
    XRenderPictureAttributes pa; memset(&pa, 0, sizeof(pa));
    g->backbuf_pic = XRenderCreatePicture(g->dpy, g->backbuf, vf, 0, &pa);
}

/* ------------------------------------------------------------------ lifecycle */

XGui *xgui_init(int width, int height, const char *title) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "[xgui] cannot open X display\n"); return NULL; }

    XGui *g = (XGui *)calloc(1, sizeof(XGui));
    g->dpy = dpy; g->screen = DefaultScreen(dpy);
    g->width = width; g->height = height;
    g->running = true;

    pss_theme_default(&g->theme);

    g->win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(dpy, g->screen),
        alloc_color(g, g->theme.window.background));

    XStoreName(dpy, g->win, title);
    XSelectInput(dpy, g->win,
        ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        Button4Mask | Button5Mask |
        PointerMotionMask | StructureNotifyMask);

    g->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, g->win, &g->wm_delete, 1);
    g->gc = XCreateGC(dpy, g->win, 0, NULL);

    g->backbuf = XCreatePixmap(dpy, g->win, (unsigned)width, (unsigned)height,
                                (unsigned)DefaultDepth(dpy, g->screen));
    g->xft = XftDrawCreate(dpy, g->backbuf,
                            DefaultVisual(dpy, g->screen),
                            DefaultColormap(dpy, g->screen));
    make_backbuf_pic(g);

    XMapWindow(dpy, g->win);
    XFlush(dpy);

    g->margin = g->theme.window.padding_y;
    clock_gettime(CLOCK_MONOTONIC, &g->last_frame_t);
    return g;
}

void xgui_load_style(XGui *g, const char *path) {
    if (!g || !path) return;
    pss_theme_load(&g->theme, path);
    g->margin = g->theme.window.padding_y;
}

void xgui_set_dark(XGui *g, bool dark) {
    if (!g) return;
    if (dark) {
        /* Catppuccin Mocha palette */
        g->theme.window.background          = 0x1e1e2e;
        g->theme.label.color                = 0xcdd6f4;
        g->theme.label.background           = 0x1e1e2e;
        g->theme.title.color                = 0xcdd6f4;
        g->theme.subtitle.color             = 0x89b4fa;
        g->theme.subtitle.background        = 0x1e1e2e;
        g->theme.button.background          = 0x89b4fa;
        g->theme.button.color               = 0x1e1e2e;
        g->theme.button.border_color        = 0x89b4fa;
        g->theme.button_hover.background    = 0x74c7ec;
        g->theme.button_hover.color         = 0x1e1e2e;
        g->theme.button_active.background   = 0x5ba3cc;
        g->theme.button_active.color        = 0x1e1e2e;
        g->theme.input.background           = 0x313244;
        g->theme.input.color                = 0xcdd6f4;
        g->theme.input.border_color         = 0x45475a;
        g->theme.input_focus.background     = 0x313244;
        g->theme.input_focus.color          = 0xcdd6f4;
        g->theme.input_focus.border_color   = 0x89b4fa;
        g->theme.textarea.background        = 0x313244;
        g->theme.textarea.color             = 0xcdd6f4;
        g->theme.textarea.border_color      = 0x45475a;
        g->theme.textarea_focus.background  = 0x313244;
        g->theme.textarea_focus.color       = 0xcdd6f4;
        g->theme.textarea_focus.border_color = 0x89b4fa;
        g->theme.scrollbar.background       = 0x313244;
        g->theme.scrollbar_thumb.background = 0x585b70;
        g->theme.scrollbar_thumb_hover.background = 0x6c7086;
        g->theme.separator.background       = 0x45475a;
        g->theme.card.background            = 0x313244;
        g->theme.card.color                 = 0xcdd6f4;
        g->theme.card.border_color          = 0x45475a;
        g->theme.progressbar.background     = 0x313244;
        g->theme.progressbar_fill.background = 0x89b4fa;
        g->theme.checkbox.background        = 0x313244;
        g->theme.checkbox.border_color      = 0x585b70;
        g->theme.checkbox_checked.background = 0x89b4fa;
        g->theme.checkbox_checked.border_color = 0x89b4fa;
    } else {
        pss_theme_default(&g->theme);
    }
    g->margin = g->theme.window.padding_y;
}

bool  xgui_running(XGui *g)    { return g && g->running; }
void  xgui_close(XGui *g)      { if (g) g->running = false; }
int   xgui_win_w(XGui *g)      { return g ? g->width  : 0; }
int   xgui_win_h(XGui *g)      { return g ? g->height : 0; }
float xgui_delta_ms(XGui *g)   { return g ? g->delta_ms : 16.0f; }

long long xgui_clock_ms(XGui *g) {
    (void)g;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void xgui_sleep_ms(XGui *g, int ms) {
    (void)g;
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)((ms % 1000) * 1000000) };
    nanosleep(&ts, NULL);
}

void xgui_destroy(XGui *g) {
    if (!g) return;
    for (int i = 0; i < MASK_CACHE_MAX; i++) {
        if (g->mcache[i].pic) XRenderFreePicture(g->dpy, g->mcache[i].pic);
        if (g->mcache[i].pix) XFreePixmap(g->dpy, g->mcache[i].pix);
    }
    for (int i = 0; i < g->fcache_n; i++)
        if (g->fcache[i].font) XftFontClose(g->dpy, g->fcache[i].font);
    if (g->backbuf_pic) XRenderFreePicture(g->dpy, g->backbuf_pic);
    XftDrawDestroy(g->xft);
    XFreePixmap(g->dpy, g->backbuf);
    XFreeGC(g->dpy, g->gc);
    XDestroyWindow(g->dpy, g->win);
    XCloseDisplay(g->dpy);
    free(g);
}

/* ------------------------------------------------------------------ frame begin */

void xgui_begin(XGui *g) {
    if (!g) return;

    /* delta time */
    struct timespec now_t;
    clock_gettime(CLOCK_MONOTONIC, &now_t);
    long long dt_us = (now_t.tv_sec  - g->last_frame_t.tv_sec ) * 1000000LL
                    + (now_t.tv_nsec - g->last_frame_t.tv_nsec) / 1000LL;
    g->delta_ms = (float)dt_us / 1000.0f;
    if (g->delta_ms > 100.0f) g->delta_ms = 16.0f;
    g->last_frame_t = now_t;

    g->frame = (g->frame + 1) & 0x7fffffff;
    g->mouse_released   = false;
    g->key_count        = 0;
    g->key_backspace    = false;
    g->key_enter        = false;
    g->key_esc_this_frame = false;
    g->key_pgup         = false;
    g->key_pgdn         = false;
    g->key_home         = false;
    g->key_end          = false;
    g->tip_show         = false;
    g->dd.active        = false;

    /* Scrollbar geometry for event hit-testing */
    int sbx, sby, sbh, thy, thh;
    bool sb_vis = (g->content_h > g->height);
    if (sb_vis) sb_geo(g, &sbx, &sby, &sbh, &thy, &thh);

    /* --- event loop --- */
    XEvent e;
    while (XPending(g->dpy)) {
        XNextEvent(g->dpy, &e);
        switch (e.type) {

        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == g->wm_delete)
                g->running = false;
            break;

        case ButtonPress:
            if (e.xbutton.button == Button1) {
                g->mouse_down = true;
                g->mx = e.xbutton.x; g->my = e.xbutton.y;
                if (sb_vis && e.xbutton.x >= sbx) {
                    if (e.xbutton.y >= thy && e.xbutton.y < thy + thh) {
                        g->sb_dragging = true;
                        g->sb_drag_y0  = e.xbutton.y;
                        g->sb_drag_s0  = g->scroll_target;
                    } else {
                        /* Page up/down when clicking track */
                        int ms = g->content_h - g->height;
                        if (ms < 0) ms = 0;
                        if (e.xbutton.y < thy) g->scroll_target -= (g->height - 40);
                        else                   g->scroll_target += (g->height - 40);
                        if (g->scroll_target < 0) g->scroll_target = 0;
                        if (g->scroll_target > ms) g->scroll_target = ms;
                    }
                }
            } else if (e.xbutton.button == Button4) {
                if (g->key_shift) {
                    g->hscroll_target -= SCROLL_STEP;
                } else {
                    g->scroll_target -= SCROLL_STEP;
                    if (g->scroll_target < -50) g->scroll_target = -50;
                }
            } else if (e.xbutton.button == Button5) {
                if (g->key_shift) {
                    g->hscroll_target += SCROLL_STEP;
                } else {
                    int ms = g->content_h - g->height;
                    if (ms < 0) ms = 0;
                    g->scroll_target += SCROLL_STEP;
                    if (g->scroll_target > ms + 50) g->scroll_target = ms + 50;
                }
            }
            break;

        case ButtonRelease:
            if (e.xbutton.button == Button1) {
                g->mouse_down     = false;
                g->mouse_released = true;
                g->sb_dragging    = false;
                g->mx = e.xbutton.x; g->my = e.xbutton.y;
            }
            break;

        case MotionNotify:
            g->mx = e.xmotion.x; g->my = e.xmotion.y;
            if (g->sb_dragging && sb_vis) {
                int track = sbh - thh;
                if (track > 0) {
                    int ms = g->content_h - g->height;
                    if (ms < 0) ms = 0;
                    int ns = g->sb_drag_s0
                           + (int)((float)(g->my - g->sb_drag_y0)
                                   / (float)track * (float)ms);
                    if (ns < 0) { ns = 0; } else if (ns > ms) { ns = ms; }
                    g->scroll_target = ns;
                    g->scroll_pos_f  = (float)ns;
                    g->scroll_vel    = 0;
                    g->scroll_y      = ns;
                }
            }
            break;

        case KeyPress: {
            char buf[16] = {0}; KeySym ks = 0;
            XLookupString(&e.xkey, buf, 15, &ks, NULL);
            if (ks == XK_BackSpace)             g->key_backspace = true;
            else if (ks == XK_Return || ks == XK_KP_Enter) g->key_enter = true;
            else if (ks == XK_Escape)           { g->key_esc_this_frame = true; g->focused_id[0] = '\0'; }
            else if (ks == XK_Page_Up)          g->key_pgup = true;
            else if (ks == XK_Page_Down)        g->key_pgdn = true;
            else if (ks == XK_Home)             g->key_home = true;
            else if (ks == XK_End)              g->key_end  = true;
            else if (ks == XK_Shift_L || ks == XK_Shift_R) g->key_shift = true;
            else if (buf[0] >= 32 && g->key_count < KEY_BUF - 1)
                g->key_chars[g->key_count++] = buf[0];
            /* hold state */
            { unsigned char _c0 = (unsigned char)buf[0];
              if (_c0 >= 32 && _c0 < 128) g->key_held[(int)_c0] = true; }
            if (ks == XK_Up    || ks == XK_KP_Up)    g->key_up    = true;
            if (ks == XK_Down  || ks == XK_KP_Down)  g->key_down  = true;
            if (ks == XK_Left  || ks == XK_KP_Left)  g->key_left  = true;
            if (ks == XK_Right || ks == XK_KP_Right) g->key_right = true;
            if (ks == XK_space)                       g->key_space = true;
            break;
        }

        case KeyRelease: {
            char buf[16] = {0}; KeySym ks = 0;
            XLookupString(&e.xkey, buf, 15, &ks, NULL);
            { unsigned char _c0 = (unsigned char)buf[0];
              if (_c0 >= 32 && _c0 < 128) g->key_held[(int)_c0] = false; }
            if (ks == XK_Up    || ks == XK_KP_Up)    g->key_up    = false;
            if (ks == XK_Down  || ks == XK_KP_Down)  g->key_down  = false;
            if (ks == XK_Left  || ks == XK_KP_Left)  g->key_left  = false;
            if (ks == XK_Right || ks == XK_KP_Right) g->key_right = false;
            if (ks == XK_space)                       g->key_space = false;
            if (ks == XK_Shift_L || ks == XK_Shift_R) g->key_shift = false;
            break;
        }

        case ConfigureNotify:
            if (e.xconfigure.width  != g->width ||
                e.xconfigure.height != g->height) {
                g->width  = e.xconfigure.width;
                g->height = e.xconfigure.height;
                if (g->backbuf_pic) { XRenderFreePicture(g->dpy, g->backbuf_pic); g->backbuf_pic = None; }
                XFreePixmap(g->dpy, g->backbuf);
                g->backbuf = XCreatePixmap(g->dpy, g->win,
                    (unsigned)g->width, (unsigned)g->height,
                    (unsigned)DefaultDepth(g->dpy, g->screen));
                XftDrawChange(g->xft, g->backbuf);
                make_backbuf_pic(g);
            }
            break;

        default: break;
        }
    }

    if (g->mouse_released) {
        g->active_id[0] = '\0';
    }

    /* Scroll keyboard support */
    if (!g->focused_id[0]) {
        int ms = g->content_h - g->height;
        if (ms < 0) ms = 0;
        if (g->key_pgup)  g->scroll_target -= (g->height - 40);
        if (g->key_pgdn)  g->scroll_target += (g->height - 40);
        if (g->key_home)  g->scroll_target = 0;
        if (g->key_end)   g->scroll_target = ms;
        if (g->key_up && !g->in_row && !g->in_grid)   g->scroll_target -= 10;
        if (g->key_down && !g->in_row && !g->in_grid) g->scroll_target += 10;
        if (g->scroll_target < -50) g->scroll_target = -50;
        if (g->scroll_target > ms + 50) g->scroll_target = ms + 50;
    }

    /* Apply key events to focused input */
    if (g->focused_id[0]) {
        InputState *inp = find_input(g, g->focused_id);
        int len = (int)strlen(inp->buf);
        if (g->key_backspace && len > 0) inp->buf[--len] = '\0';
        for (int i = 0; i < g->key_count && len < INPUT_BUF - 2; i++)
            inp->buf[len++] = g->key_chars[i];
        inp->buf[len] = '\0';
    }

    /* Spring-physics smooth scroll (critically damped, framerate independent) */
    if (!g->sb_dragging) {
        float dt = g->delta_ms / 1000.0f;
        if (dt > 0.1f) dt = 0.1f; /* cap dt to avoid instability on huge lag */

        float omega = SCROLL_OMEGA;
        float exp_term = expf(-omega * dt);
        float diff_pos = g->scroll_pos_f - (float)g->scroll_target;

        /* Critically damped spring integration */
        float n1 = g->scroll_vel + omega * diff_pos;
        float next_pos = (float)g->scroll_target + (diff_pos + n1 * dt) * exp_term;
        float next_vel = (g->scroll_vel - omega * n1 * dt) * exp_term;

        g->scroll_pos_f = next_pos;
        g->scroll_vel   = next_vel;

        if (fabsf(g->scroll_pos_f - (float)g->scroll_target) < SNAP_THRESH &&
            fabsf(g->scroll_vel) < SNAP_THRESH) {
            g->scroll_pos_f = (float)g->scroll_target;
            g->scroll_vel   = 0.0f;
        }
        g->scroll_y = (int)(g->scroll_pos_f + 0.5f);
    }

    /* Horizontal smooth scroll */
    {
        float dt = g->delta_ms / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        float omega = SCROLL_OMEGA;
        float exp_term = expf(-omega * dt);
        float diff_pos = g->hscroll_pos_f - (float)g->hscroll_target;
        float n1 = g->hscroll_vel + omega * diff_pos;
        g->hscroll_pos_f = (float)g->hscroll_target + (diff_pos + n1 * dt) * exp_term;
        g->hscroll_vel   = (g->hscroll_vel - omega * n1 * dt) * exp_term;
        if (fabsf(g->hscroll_pos_f - (float)g->hscroll_target) < SNAP_THRESH &&
            fabsf(g->hscroll_vel) < SNAP_THRESH) {
            g->hscroll_pos_f = (float)g->hscroll_target;
            g->hscroll_vel   = 0.0f;
        }
        g->hscroll_x = (int)(g->hscroll_pos_f + 0.5f);
    }

    /* Clear backbuffer */
    fill_rect(g, 0, 0, g->width, g->height, g->theme.window.background);

    /* Reset layout cursor */
    g->cx        = g->theme.window.padding_x - g->hscroll_x;
    g->cy        = g->margin - g->scroll_y;
    g->in_row    = false;
    g->row_max_h = 0;
    g->in_grid   = false;
}

/* ------------------------------------------------------------------ advanced scroll implementation */

void xgui_set_scroll(XGui *g, int y) {
    if (!g) return;
    g->scroll_target = y;
}

int xgui_get_scroll(XGui *g) {
    return g ? g->scroll_y : 0;
}

void xgui_set_hscroll(XGui *g, int x) {
    if (!g) return;
    g->hscroll_target = x;
}

int xgui_get_hscroll(XGui *g) {
    return g ? g->hscroll_x : 0;
}

void xgui_scroll_to_bottom(XGui *g) {
    if (!g) return;
    int ms = g->content_h - g->height;
    if (ms > 0) g->scroll_target = ms;
}

void xgui_ensure_visible(XGui *g, int x, int y, int w, int h) {
    if (!g) return;
    /* Vertical */
    if (y < 0) {
        g->scroll_target += y - 10;
    } else if (y + h > g->height) {
        g->scroll_target += (y + h - g->height) + 10;
    }
    /* Horizontal */
    if (x < 0) {
        g->hscroll_target += x - 10;
    } else if (x + w > g->width) {
        g->hscroll_target += (x + w - g->width) + 10;
    }
}

/* ------------------------------------------------------------------ frame end */

void xgui_end(XGui *g) {
    if (!g) return;

    /* Record content dimensions */
    g->content_h = g->cy + g->scroll_y + g->theme.window.padding_y;
    g->content_w = g->cx + g->hscroll_x + g->theme.window.padding_x;
    int maxs = g->content_h - g->height;
    if (maxs < 0) maxs = 0;
    if (g->scroll_target > maxs) g->scroll_target = maxs;
    if (g->scroll_target < 0)    g->scroll_target = 0;

    int maxh = g->content_w - g->width;
    if (maxh < 0) maxh = 0;
    if (g->hscroll_target > maxh) g->hscroll_target = maxh;
    if (g->hscroll_target < 0)    g->hscroll_target = 0;

    /* Draw deferred dropdown popup on top of everything */
    if (g->dd.active) {
        DeferredDropdown *dd = &g->dd;
        int r = 6, ph = dd->count * dd->item_h + 8;
        draw_shadow(g, dd->x, dd->y, dd->w, ph, r);
        fill_rounded_rect(g, dd->x, dd->y, dd->w, ph, r,
                          g->theme.input.background ? g->theme.input.background : 0xffffff);
        draw_outline(g, dd->x, dd->y, dd->w, ph, r, 1,
                     g->theme.input.border_color ? g->theme.input.border_color : 0xdddddd);
        XftFont *f = get_font(g, g->theme.label.font[0] ? g->theme.label.font : "sans",
                              g->theme.label.font_size > 0 ? g->theme.label.font_size : 13, 400);
        for (int i = 0; i < dd->count; i++) {
            int iy = dd->y + 4 + i * dd->item_h;
            bool hov = (g->mx >= dd->x && g->mx < dd->x + dd->w
                     && g->my >= iy    && g->my < iy + dd->item_h);
            if (hov) fill_rounded_rect(g, dd->x + 2, iy, dd->w - 4, dd->item_h, 4,
                          g->theme.button.background ? g->theme.button.background : 0x0078d4);
            uint32_t tc = hov ? 0xffffff
                              : (g->theme.label.color ? g->theme.label.color : 0x222222);
            if (f) draw_text(g, f, dd->labels[i], dd->x + 12, iy + f->ascent + 4, tc);
        }
    }

    /* Scrollbar */
    if (g->content_h > g->height) {
        int sbx, sby, sbh, thy, thh;
        sb_geo(g, &sbx, &sby, &sbh, &thy, &thh);
        int sw = g->width - sbx - SB_MARGIN;
        uint32_t track_c = g->theme.scrollbar.background ? g->theme.scrollbar.background : 0xf0f0f0;
        fill_rounded_rect(g, sbx, sby, sw, sbh, sw/2, track_c);
        bool hov = (g->mx >= sbx && g->mx < g->width - SB_MARGIN
                 && g->my >= thy && g->my < thy + thh);
        uint32_t thumb_c = (hov || g->sb_dragging)
            ? (g->theme.scrollbar_thumb_hover.background ? g->theme.scrollbar_thumb_hover.background : 0x888888)
            : (g->theme.scrollbar_thumb.background       ? g->theme.scrollbar_thumb.background       : 0xbbbbbb);
        fill_rounded_rect(g, sbx + 1, thy + 1, sw - 2, thh - 2, (sw-2)/2, thumb_c);
    }

    /* Toast notification */
    if (g->toast_text[0] && g->frame < g->toast_end_frame) {
        int remaining = g->toast_end_frame - g->frame;
        int total     = g->toast_end_frame - (g->toast_end_frame - 90);
        (void)total;
        int alpha = (remaining < 20) ? (remaining * 255 / 20)
                  : (g->toast_end_frame - g->frame > 70) ? 255
                  : 255;
        XftFont *tf = get_font(g, "sans", 13, 400);
        if (tf && alpha > 0) {
            int tw, th; text_size(g, tf, g->toast_text, &tw, &th);
            int bw = tw + 32, bh = th + 16;
            int tx = (g->width - bw) / 2, ty = g->height - bh - 20;
            fill_rounded_rect_alpha(g, tx, ty, bw, bh, bh/2, 0x333333, alpha);
            draw_text(g, tf, g->toast_text, tx + 16,
                      ty + 8 + tf->ascent, 0xffffff);
        }
    } else if (g->frame >= g->toast_end_frame && g->toast_end_frame > 0) {
        g->toast_text[0] = '\0';
    }

    /* Tooltip */
    if (g->tip_show && g->tip_text[0]) {
        PssStyle *ts = &g->theme.tooltip;
        int fsz = ts->font_size > 0 ? ts->font_size : 11;
        XftFont *tf = get_font(g, "sans", fsz, 400);
        if (tf) {
            int tw, th; text_size(g, tf, g->tip_text, &tw, &th);
            int px = ts->padding_x > 0 ? ts->padding_x : 7;
            int py = ts->padding_y > 0 ? ts->padding_y : 4;
            int bw = tw + 2*px, bh = th + 2*py;
            int tx = g->tip_x + 14, ty = g->tip_y - bh - 6;
            if (tx + bw > g->width)  tx = g->width  - bw - 4;
            if (ty < 4)              ty = g->tip_y  + 20;
            fill_rounded_rect(g, tx, ty, bw, bh, ts->border_radius > 0 ? ts->border_radius : 4,
                              ts->background ? ts->background : 0x333333);
            draw_text(g, tf, g->tip_text, tx+px, ty+py+tf->ascent,
                      ts->color ? ts->color : 0xffffff);
        }
    }

    XCopyArea(g->dpy, g->backbuf, g->win, g->gc,
              0, 0, (unsigned)g->width, (unsigned)g->height, 0, 0);
    XFlush(g->dpy);
}

/* ------------------------------------------------------------------ visibility helper */

static bool vis(XGui *g, int y, int h) { return (y + h > 0) && (y < g->height); }

static void advance(XGui *g, int bw, int bh) {
    if (g->in_grid) {
        if (bh > g->grid_row_h) g->grid_row_h = bh;
        g->grid_col++;
        if (g->grid_col >= g->grid_cols) {
            g->grid_col = 0;
            g->cy = g->grid_row_y0 + g->grid_row_h + WIDGET_GAP;
            g->grid_row_y0 = g->cy;
            g->grid_row_h  = 0;
            g->cx = g->theme.window.padding_x;
        } else {
            g->cx += g->grid_col_w + WIDGET_GAP;
        }
    } else if (g->in_row) {
        g->cx += bw + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }
}

/* ------------------------------------------------------------------ core widgets */

void xgui_label(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.label;
    XftFont *f = get_font(g, s->font[0] ? s->font : "sans",
                          s->font_size > 0 ? s->font_size : 13,
                          s->font_weight > 0 ? s->font_weight : 400);
    if (!f) return;
    int tw, th; text_size(g, f, text, &tw, &th);
    int bh = th + 2 * s->padding_y;
    if (vis(g, g->cy, bh))
        draw_text(g, f, text, g->cx + s->padding_x,
                  g->cy + s->padding_y + f->ascent,
                  s->color ? s->color : 0x222222);
    advance(g, tw + 2*s->padding_x, bh);
}

void xgui_title(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.title;
    int fsz = s->font_size > 0 ? s->font_size : 26;
    int fw  = s->font_weight > 0 ? s->font_weight : 700;
    XftFont *f = get_font(g, s->font[0] ? s->font : "sans", fsz, fw);
    if (!f) return;
    int tw, th; text_size(g, f, text, &tw, &th);
    int py = s->padding_y > 0 ? s->padding_y : 8;
    if (vis(g, g->cy, th + 2*py))
        draw_text(g, f, text, g->cx, g->cy + py + f->ascent,
                  s->color ? s->color : 0x111111);
    advance(g, tw, th + 2*py);
}

void xgui_subtitle(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.subtitle;
    int fsz = s->font_size > 0 ? s->font_size : 16;
    int fw  = s->font_weight > 0 ? s->font_weight : 600;
    XftFont *f = get_font(g, s->font[0] ? s->font : "sans", fsz, fw);
    if (!f) return;
    int tw, th; text_size(g, f, text, &tw, &th);
    int py = s->padding_y > 0 ? s->padding_y : 5;
    if (vis(g, g->cy, th + 2*py))
        draw_text(g, f, text, g->cx, g->cy + py + f->ascent,
                  s->color ? s->color : 0x555555);
    advance(g, tw, th + 2*py);
}

void xgui_separator(XGui *g) {
    if (!g) return;
    int usable = g->width - 2*g->margin;
    uint32_t c = g->theme.separator.background ? g->theme.separator.background : 0xdddddd;
    if (vis(g, g->cy + 6, 2))
        fill_rounded_rect(g, g->cx, g->cy + 6, usable, 1, 0, c);
    advance(g, usable, 14);
}

void xgui_section(XGui *g, const char *title) {
    if (!g || !title) return;
    PssStyle *s = &g->theme.label;
    XftFont *f = get_font(g, s->font[0] ? s->font : "sans", 11, 600);
    if (!f) return;
    int usable = g->width - 2*g->margin;
    int tw, th; text_size(g, f, title, &tw, &th);
    int cy = g->cy;
    if (vis(g, cy, 20)) {
        /* two lines with label in between */
        uint32_t lc = g->theme.separator.background ? g->theme.separator.background : 0xdddddd;
        int mid = cy + 10;
        int txt_w = tw + 6;
        fill_rounded_rect(g, g->cx, mid, (usable - txt_w - 8) / 2, 1, 0, lc);
        fill_rounded_rect(g, g->cx + (usable - txt_w - 8)/2 + txt_w + 8, mid,
                          (usable - txt_w - 8)/2, 1, 0, lc);
        draw_text(g, f, title,
                  g->cx + (usable - txt_w)/2,
                  mid + f->ascent/2,
                  g->theme.label.color ? g->theme.label.color : 0x888888);
    }
    advance(g, usable, 22);
}

void xgui_spacer(XGui *g, int h) {
    if (!g) return;
    if (g->in_row) g->cx += h;
    else           g->cy += h;
}

/* ------------------------------------------------------------------ button */

bool xgui_button(XGui *g, const char *text) {
    if (!g || !text) return false;
    PssStyle *s = &g->theme.button;
    int fsz = s->font_size > 0 ? s->font_size : 13;
    int fw  = s->font_weight > 0 ? s->font_weight : 400;
    XftFont *f = get_font(g, s->font[0] ? s->font : "sans", fsz, fw);
    if (!f) return false;

    int tw, th; text_size(g, f, text, &tw, &th);
    int bw = tw + 2*(s->padding_x > 0 ? s->padding_x : 18);
    int bh = th + 2*(s->padding_y > 0 ? s->padding_y : 9);
    int x = g->cx, y = g->cy;
    if (g->in_grid) bw = g->grid_col_w;

    bool hov  = (g->mx >= x && g->mx < x+bw && g->my >= y && g->my < y+bh);
    bool held = hov && g->mouse_down;
    bool clicked = hov && g->mouse_released;

    if (vis(g, y, bh)) {
        PssStyle *ds = held ? &g->theme.button_active
                     : hov  ? &g->theme.button_hover : s;
        int r  = ds->border_radius > 0 ? ds->border_radius : 8;
        int dy = held ? 1 : 0;

        fill_rounded_rect(g, x, y+dy, bw, bh, r, ds->background ? ds->background : 0x0078d4);
        if (!held) /* top shine */
            fill_rounded_rect_alpha(g, x+1, y+1, bw-2, bh/3, r-1, 0xffffff, 18);
        if (ds->border_width > 0)
            draw_outline(g, x, y+dy, bw, bh, r, ds->border_width, ds->border_color);

        int tx = x + (bw - tw) / 2;
        draw_text(g, f, text, tx, y+dy + (bh - th)/2 + f->ascent,
                  ds->color ? ds->color : 0xffffff);
    }
    advance(g, bw, bh);
    return clicked;
}

bool xgui_icon_button(XGui *g, const char *icon, const char *label) {
    if (!g) return false;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s  %s", icon ? icon : "", label ? label : "");
    return xgui_button(g, buf);
}

/* ------------------------------------------------------------------ input */

const char *xgui_input(XGui *g, const char *id, const char *placeholder) {
    if (!g || !id) return "";
    InputState *inp = find_input(g, id);
    bool focused = (strcmp(g->focused_id, id) == 0);
    PssStyle *s  = &g->theme.input;
    PssStyle *ds = focused ? &g->theme.input_focus : s;

    int usable = g->width - 2*g->theme.window.padding_x;
    int bw = usable;
    if (g->in_row) bw = 140;
    if (g->in_grid) bw = g->grid_col_w;

    XftFont *f = get_font(g, s->font[0] ? s->font : "sans",
                          s->font_size > 0 ? s->font_size : 13, 400);
    if (!f) return inp->buf;

    int _tw, th; text_size(g, f, "Ag", &_tw, &th);
    int bh = th + 2*(s->padding_y > 0 ? s->padding_y : 7);
    int x = g->cx, y = g->cy;

    if (vis(g, y, bh)) {
        if (g->mouse_released && g->mx >= x && g->mx < x+bw && g->my >= y && g->my < y+bh) {
            snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
            focused = true; ds = &g->theme.input_focus;
        }
        int r = ds->border_radius > 0 ? ds->border_radius : 6;
        fill_rounded_rect(g, x, y, bw, bh, r, ds->background ? ds->background : 0xffffff);
        draw_outline(g, x, y, bw, bh, r,
                     ds->border_width > 0 ? ds->border_width : 1,
                     ds->border_color ? ds->border_color : 0xbcbcbc);

        int px2 = s->padding_x > 0 ? s->padding_x : 10;
        const char *disp = inp->buf[0] ? inp->buf : placeholder;
        uint32_t tc = inp->buf[0] ? (ds->color ? ds->color : 0x222222) : 0x999999;
        /* clip text display */
        draw_text(g, f, disp ? disp : "", x + px2, y + (bh-th)/2 + f->ascent, tc);

        if (focused && ((g->frame / BLINK_HALF) % 2 == 0)) {
            int cw, _ch; text_size(g, f, inp->buf, &cw, &_ch);
            fill_rounded_rect(g, x+px2+cw+1, y+(bh-th)/2+2, 2, th-4, 1,
                              ds->border_color ? ds->border_color : 0x0078d4);
        }
    }
    advance(g, bw, bh);
    return inp->buf;
}

const char *xgui_textarea(XGui *g, const char *id, const char *placeholder) {
    if (!g || !id) return "";
    InputState *inp = find_input(g, id);
    bool focused = (strcmp(g->focused_id, id) == 0);
    PssStyle *s  = &g->theme.textarea;
    PssStyle *ds = focused ? &g->theme.textarea_focus : s;
    int usable = g->width - 2*g->margin;
    int bh = 90, bw = usable;
    int x = g->cx, y = g->cy;

    if (vis(g, y, bh)) {
        if (g->mouse_released && g->mx >= x && g->mx < x+bw && g->my >= y && g->my < y+bh) {
            snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
            focused = true; ds = &g->theme.textarea_focus;
        }
        int r = ds->border_radius > 0 ? ds->border_radius : 6;
        fill_rounded_rect(g, x, y, bw, bh, r, ds->background ? ds->background : 0xffffff);
        draw_outline(g, x, y, bw, bh, r,
                     ds->border_width > 0 ? ds->border_width : 1,
                     ds->border_color ? ds->border_color : 0xbcbcbc);

        XftFont *f = get_font(g, s->font[0] ? s->font : "sans",
                              s->font_size > 0 ? s->font_size : 13, 400);
        if (f) {
            int px2 = s->padding_x > 0 ? s->padding_x : 10;
            int py2 = s->padding_y > 0 ? s->padding_y : 8;
            const char *disp = inp->buf[0] ? inp->buf : placeholder;
            uint32_t tc = inp->buf[0] ? (ds->color ? ds->color : 0x222222) : 0x999999;
            draw_text(g, f, disp ? disp : "", x+px2, y+py2+f->ascent, tc);

            if (focused && ((g->frame / BLINK_HALF) % 2 == 0)) {
                int cw, th; text_size(g, f, inp->buf, &cw, &th);
                fill_rounded_rect(g, x+px2+cw+1, y+py2+2, 2, th-4, 1,
                                  ds->border_color ? ds->border_color : 0x0078d4);
            }
        }
        if (focused) {
            int len = (int)strlen(inp->buf);
            if (g->key_backspace && len > 0) inp->buf[--len] = '\0';
            if (g->key_enter && len < INPUT_BUF-2) { inp->buf[len++]='\n'; inp->buf[len]='\0'; }
            for (int i = 0; i < g->key_count && len < INPUT_BUF-1; i++)
                inp->buf[len++] = g->key_chars[i];
            inp->buf[len] = '\0';
        }
    }
    advance(g, bw, bh);
    return inp->buf;
}

/* ------------------------------------------------------------------ checkbox */

bool xgui_checkbox(XGui *g, const char *id, const char *label) {
    if (!g || !id || !label) return false;
    InputState *inp = find_input(g, id);
    bool checked = (inp->buf[0] == '1');
    int box = 20, x = g->cx, y = g->cy;
    bool hov = (g->mx >= x && g->mx < x+box && g->my >= y && g->my < y+box);
    if (hov && g->mouse_released) {
        checked = !checked;
        inp->buf[0] = checked ? '1' : '0'; inp->buf[1] = '\0';
    }
    XftFont *f = get_font(g, g->theme.label.font[0] ? g->theme.label.font : "sans",
                          g->theme.label.font_size > 0 ? g->theme.label.font_size : 13, 400);
    int tw = 0, th = 14;
    if (f) text_size(g, f, label, &tw, &th);
    int total_h = box > th ? box : th;

    if (vis(g, y, total_h)) {
        int cy2 = y + (total_h - box)/2;
        uint32_t bg = checked ? (g->theme.checkbox_checked.background ? g->theme.checkbox_checked.background : 0x0078d4)
                              : (g->theme.checkbox.background ? g->theme.checkbox.background : 0xffffff);
        uint32_t bc = checked ? bg : (hov ? 0x0078d4 : (g->theme.checkbox.border_color ? g->theme.checkbox.border_color : 0xbcbcbc));
        fill_rounded_rect(g, x, cy2, box, box, 4, bg);
        draw_outline(g, x, cy2, box, box, 4, 2, bc);
        if (checked) {
            /* AA checkmark */
            XSetForeground(g->dpy, g->gc, alloc_color(g, 0xffffff));
            XSetLineAttributes(g->dpy, g->gc, 2, LineSolid, CapRound, JoinRound);
            XDrawLine(g->dpy, g->backbuf, g->gc, x+4, cy2+10, x+8, cy2+14);
            XDrawLine(g->dpy, g->backbuf, g->gc, x+8, cy2+14, x+16, cy2+6);
        }
        if (f) draw_text(g, f, label, x+box+8, y+(total_h-th)/2+f->ascent,
                         g->theme.label.color ? g->theme.label.color : 0x222222);
    }
    advance(g, box+8+tw, total_h);
    return checked;
}

/* ------------------------------------------------------------------ toggle switch (Flutter/iOS style) */

bool xgui_toggle(XGui *g, const char *id, bool value, const char *label) {
    if (!g || !id) return value;
    InputState *inp = find_input(g, id);
    /* init from value if buffer empty */
    if (inp->buf[0] == '\0') { inp->buf[0] = value ? '1' : '0'; inp->buf[1] = '\0'; }
    bool on = (inp->buf[0] == '1');

    int tw_px = 42, th_px = 24; /* track width/height */
    int x = g->cx, y = g->cy;
    bool hov = (g->mx >= x && g->mx < x+tw_px && g->my >= y && g->my < y+th_px);
    if (hov && g->mouse_released) {
        on = !on; inp->buf[0] = on ? '1' : '0'; inp->buf[1] = '\0';
    }

    XftFont *f = get_font(g, g->theme.label.font[0] ? g->theme.label.font : "sans",
                          g->theme.label.font_size > 0 ? g->theme.label.font_size : 13, 400);
    int lw = 0, lh = th_px;
    if (f && label) text_size(g, f, label, &lw, &lh);
    int total_h = th_px > lh ? th_px : lh;

    if (vis(g, y, total_h)) {
        int ty = y + (total_h - th_px)/2;
        uint32_t track_c = on ? (g->theme.button.background ? g->theme.button.background : 0x0078d4)
                              : (g->theme.checkbox.border_color ? g->theme.checkbox.border_color : 0xbcbcbc);
        fill_rounded_rect(g, x, ty, tw_px, th_px, th_px/2, track_c);

        /* thumb — animates position */
        int thumb_r = th_px/2 - 2;
        int thumb_x = on ? (x + tw_px - th_px/2 - 2) : (x + th_px/2);
        fill_circle(g, thumb_x - thumb_r, ty + 2, thumb_r, 0xffffff);

        if (f && label)
            draw_text(g, f, label, x + tw_px + 10, y + (total_h - lh)/2 + f->ascent,
                      g->theme.label.color ? g->theme.label.color : 0x222222);
    }
    advance(g, tw_px + (label ? 10 + lw : 0), total_h);
    return on;
}

/* ------------------------------------------------------------------ progress & slider */

void xgui_progress(XGui *g, int value, int max_val) {
    if (!g || max_val <= 0) return;
    int usable = g->width - 2*g->margin;
    int bh = g->theme.progressbar.min_height > 0 ? g->theme.progressbar.min_height : 10;
    if (bh < 6) bh = 10;
    int x = g->cx, y = g->cy;
    int r = bh/2;
    if (vis(g, y, bh)) {
        uint32_t tc = g->theme.progressbar.background       ? g->theme.progressbar.background       : 0xe8e8e8;
        uint32_t fc = g->theme.progressbar_fill.background  ? g->theme.progressbar_fill.background  : 0x0078d4;
        fill_rounded_rect(g, x, y, usable, bh, r, tc);
        int filled = (int)((long long)value * usable / max_val);
        if (filled > usable) filled = usable;
        if (filled > 0) {
            fill_rounded_rect(g, x, y, filled, bh, r, fc);
            fill_rounded_rect_alpha(g, x+1, y+1, filled-2, bh/2-1, r-1, 0xffffff, 22);
        }
    }
    advance(g, usable, bh);
}

float xgui_slider(XGui *g, const char *id, float min_v, float max_v, float cur) {
    if (!g || !id) return cur;
    InputState *inp = find_input(g, id);
    float val = (inp->buf[0]) ? (float)atof(inp->buf) : cur;
    if (!inp->buf[0]) snprintf(inp->buf, sizeof(inp->buf), "%f", cur);

    int usable = g->width - 2*g->margin;
    if (g->in_grid) usable = g->grid_col_w;
    int tr = 6, thumb_r = 10;
    int x = g->cx, y = g->cy;
    int track_y = y + thumb_r - tr/2;
    float range = max_v - min_v;

    bool hov = (g->mx >= x && g->mx < x+usable && g->my >= y && g->my < y+2*thumb_r);
    bool active = (strcmp(g->active_id, id) == 0);

    if (hov && g->mouse_down && g->active_id[0] == '\0') {
        strncpy(g->active_id, id, sizeof(g->active_id)-1);
        active = true;
    }

    if (active) {
        float t = (float)(g->mx - (x + thumb_r)) / (float)(usable - 2*thumb_r);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        val = min_v + t * range;
        snprintf(inp->buf, sizeof(inp->buf), "%f", val);
    }

    float t = (range > 0) ? (val - min_v) / range : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int thumb_cx = x + (int)(t * (float)(usable - 2*thumb_r)) + thumb_r;

    if (vis(g, y, 2*thumb_r)) {
        uint32_t tc  = 0xe0e0e0;
        uint32_t fc  = g->theme.progressbar_fill.background ? g->theme.progressbar_fill.background : 0x0078d4;
        uint32_t thc = hov ? 0x005fa3 : fc;
        fill_rounded_rect(g, x, track_y, usable, tr, tr/2, tc);
        if (thumb_cx - x > 0)
            fill_rounded_rect(g, x, track_y, thumb_cx - x, tr, tr/2, fc);
        /* thumb */
        fill_circle(g, thumb_cx - thumb_r, y, thumb_r, thc);
        fill_circle(g, thumb_cx - thumb_r + 3, y + 3, thumb_r - 3, 0xffffff);
    }
    advance(g, usable, 2*thumb_r);
    return val;
}

/* ------------------------------------------------------------------ badge & chip */

void xgui_badge(XGui *g, const char *text, uint32_t bg) {
    if (!g || !text) return;
    int fsz = g->theme.badge.font_size > 0 ? g->theme.badge.font_size : 11;
    XftFont *f = get_font(g, "sans", fsz, 700);
    if (!f) return;
    int tw, th; text_size(g, f, text, &tw, &th);
    int px = 8, py = 3;
    int bw = tw + 2*px, bh = th + 2*py;
    int r  = bh/2;
    int x = g->cx, y = g->cy;
    if (vis(g, y, bh)) {
        fill_rounded_rect(g, x, y, bw, bh, r, bg);
        fill_rounded_rect_alpha(g, x+1, y+1, bw-2, bh/2, r-1, 0xffffff, 18);
        draw_text(g, f, text, x+px, y+py+f->ascent, 0xffffff);
    }
    advance(g, bw, bh);
}

bool xgui_chip(XGui *g, const char *text, bool removable) {
    if (!g || !text) return false;
    int fsz = 12;
    XftFont *f = get_font(g, "sans", fsz, 400);
    if (!f) return false;
    int tw, th; text_size(g, f, text, &tw, &th);
    int px = 10, py = 5;
    int bw = tw + 2*px + (removable ? 20 : 0), bh = th + 2*py;
    int r  = bh/2;
    int x = g->cx, y = g->cy;
    bool remove_clicked = false;

    bool hov = (g->mx >= x && g->mx < x+bw && g->my >= y && g->my < y+bh);
    if (vis(g, y, bh)) {
        uint32_t bg = hov ? 0xe8e8f8 : (g->theme.input.background ? g->theme.input.background : 0xf0f0f0);
        uint32_t tc = g->theme.label.color ? g->theme.label.color : 0x333333;
        fill_rounded_rect(g, x, y, bw, bh, r, bg);
        draw_outline(g, x, y, bw, bh, r, 1, 0xcccccc);
        draw_text(g, f, text, x+px, y+py+f->ascent, tc);
        if (removable) {
            int xbtn_x = x + bw - 16, xbtn_y = y + bh/2;
            bool xhov = (g->mx >= xbtn_x-4 && g->mx < xbtn_x+12
                      && g->my >= y && g->my < y+bh);
            fill_circle(g, xbtn_x, xbtn_y - 6, 6, xhov ? 0xaaaaaa : 0xcccccc);
            XftColor xc; make_xft_color(g, 0xffffff, &xc);
            XftFont *sf = get_font(g, "sans", 9, 700);
            if (sf) XftDrawStringUtf8(g->xft, &xc, sf, xbtn_x+2, xbtn_y-1, (const FcChar8 *)"x", 1);
            XftColorFree(g->dpy, DefaultVisual(g->dpy, g->screen),
                         DefaultColormap(g->dpy, g->screen), &xc);
            if (xhov && g->mouse_released) remove_clicked = true;
        }
    }
    advance(g, bw, bh);
    return remove_clicked;
}

/* ------------------------------------------------------------------ tabs (Qt/Flutter style) */

int xgui_tabs(XGui *g, const char *id, const char **labels, int count) {
    if (!g || !id || !labels || count <= 0) return 0;
    InputState *inp = find_input(g, id);
    int active = inp->buf[0] ? (int)(inp->buf[0] - '0') : 0;
    if (active < 0 || active >= count) active = 0;

    int usable = g->width - 2*g->margin;
    int tab_w  = usable / count;
    int bh     = 40;
    int x0 = g->cx, y = g->cy;
    PssStyle *s  = &g->theme.tab;
    PssStyle *sa = &g->theme.tab_active;
    PssStyle *sh = &g->theme.tab_hover;

    XftFont *f = get_font(g, s->font[0] ? s->font : "sans",
                          s->font_size > 0 ? s->font_size : 13, 400);
    XftFont *fb = get_font(g, sa->font[0] ? sa->font : "sans",
                           sa->font_size > 0 ? sa->font_size : 13, 600);

    /* Draw tab bar background */
    if (vis(g, y, bh)) {
        uint32_t bar_bg = s->background ? s->background : 0xf0f0f0;
        fill_rounded_rect(g, x0, y, usable, bh, 6, bar_bg);

        for (int i = 0; i < count; i++) {
            int tx = x0 + i * tab_w, ty = y;
            bool hov = (g->mx >= tx && g->mx < tx+tab_w && g->my >= ty && g->my < ty+bh);
            bool sel = (i == active);

            if (g->mouse_released && hov) {
                active = i;
                inp->buf[0] = (char)('0' + i);
                inp->buf[1] = '\0';
            }

            if (sel) {
                fill_rounded_rect(g, tx+2, ty+2, tab_w-4, bh-4, 5,
                                  sa->background ? sa->background : 0xffffff);
                /* Bottom accent line */
                fill_rounded_rect(g, tx+4, ty+bh-3, tab_w-8, 3, 1,
                                  sa->border_color ? sa->border_color : 0x0078d4);
            } else if (hov) {
                fill_rounded_rect(g, tx+2, ty+2, tab_w-4, bh-4, 5,
                                  sh->background ? sh->background : 0xe8e8e8);
            }

            XftFont *tf = sel ? fb : f;
            if (tf) {
                int lw, lh; text_size(g, tf, labels[i], &lw, &lh);
                uint32_t tc = sel ? (sa->color ? sa->color : 0x0078d4)
                                  : (s->color ? s->color : 0x666666);
                draw_text(g, tf, labels[i],
                          tx + (tab_w - lw)/2, ty + (bh - lh)/2 + tf->ascent, tc);
            }
        }
    }
    advance(g, usable, bh);
    return active;
}

/* ------------------------------------------------------------------ select / dropdown */

int xgui_select(XGui *g, const char *id, const char **opts, int count, int cur) {
    if (!g || !id || !opts || count <= 0) return cur;
    InputState *inp = find_input(g, id);
    int sel = inp->buf[0] ? atoi(inp->buf) : cur;
    if (sel < 0 || sel >= count) sel = 0;
    if (!inp->buf[0]) snprintf(inp->buf, sizeof(inp->buf), "%d", sel);

    int usable = g->width - 2*g->margin;
    int bw = usable;
    if (g->in_grid) bw = g->grid_col_w;
    int x = g->cx, y = g->cy;
    bool open = (strcmp(g->dd.id, id) == 0);

    XftFont *f = get_font(g, g->theme.input.font[0] ? g->theme.input.font : "sans",
                          g->theme.input.font_size > 0 ? g->theme.input.font_size : 13, 400);
    int _tw, th; text_size(g, f ? f : get_font(g,"sans",13,400), "Ag", &_tw, &th);
    int bh = th + 2*(g->theme.input.padding_y > 0 ? g->theme.input.padding_y : 7);

    bool hov = (g->mx >= x && g->mx < x+bw && g->my >= y && g->my < y+bh);
    if (hov && g->mouse_released) {
        if (open) {
            g->dd.id[0] = '\0'; open = false;
        } else {
            snprintf(g->dd.id, sizeof(g->dd.id), "%s", id);
            open = true;
        }
    }

    if (vis(g, y, bh)) {
        PssStyle *ds = open ? &g->theme.input_focus : &g->theme.input;
        int r = ds->border_radius > 0 ? ds->border_radius : 6;
        fill_rounded_rect(g, x, y, bw, bh, r, ds->background ? ds->background : 0xffffff);
        draw_outline(g, x, y, bw, bh, r,
                     ds->border_width > 0 ? ds->border_width : 1,
                     ds->border_color ? ds->border_color : 0xbcbcbc);

        if (f) {
            int px2 = g->theme.input.padding_x > 0 ? g->theme.input.padding_x : 10;
            draw_text(g, f, opts[sel], x + px2, y + (bh-th)/2 + f->ascent,
                      ds->color ? ds->color : 0x222222);
        }
        /* Dropdown arrow */
        int ax = x + bw - 22, ay = y + bh/2;
        XSetForeground(g->dpy, g->gc, alloc_color(g, 0x888888));
        XPoint pts[3];
        if (open) {
            pts[0] = (XPoint){(short)(ax),   (short)(ay+3)};
            pts[1] = (XPoint){(short)(ax+8), (short)(ay+3)};
            pts[2] = (XPoint){(short)(ax+4), (short)(ay-3)};
        } else {
            pts[0] = (XPoint){(short)(ax),   (short)(ay-3)};
            pts[1] = (XPoint){(short)(ax+8), (short)(ay-3)};
            pts[2] = (XPoint){(short)(ax+4), (short)(ay+3)};
        }
        XFillPolygon(g->dpy, g->backbuf, g->gc, pts, 3, Convex, CoordModeOrigin);
    }

    /* Register deferred popup */
    if (open && count > 0 && count <= 32) {
        int item_h = bh;
        g->dd.active = true;
        g->dd.x = x; g->dd.y = y + bh + 2;
        g->dd.w = bw; g->dd.item_h = item_h;
        g->dd.count = count;
        snprintf(g->dd.id, sizeof(g->dd.id), "%s", id);
        for (int i = 0; i < count && i < 32; i++)
            snprintf(g->dd.labels[i], 128, "%s", opts[i]);

        /* Check if user clicked an option */
        for (int i = 0; i < count; i++) {
            int iy = y + bh + 2 + 4 + i * item_h;
            if (g->mouse_released && g->mx >= x && g->mx < x+bw
                                  && g->my >= iy && g->my < iy+item_h) {
                sel = i;
                snprintf(inp->buf, sizeof(inp->buf), "%d", sel);
                g->dd.id[0] = '\0';
                g->dd.active = false;
            }
        }
    }

    /* Click outside → close */
    if (open && g->mouse_released
             && !(g->mx >= x && g->mx < x+bw && g->my >= y)) {
        int popup_h = count * bh + 8;
        if (!(g->mx >= x && g->mx < x+bw && g->my >= y+bh+2 && g->my < y+bh+2+popup_h)) {
            g->dd.id[0] = '\0'; g->dd.active = false;
        }
    }

    advance(g, bw, bh);
    return sel;
}

/* ------------------------------------------------------------------ spinner (Flutter CircularProgressIndicator) */

void xgui_spinner(XGui *g, int size) {
    if (!g || size <= 0) return;
    int x = g->cx, y = g->cy;
    int r = size / 2;
    if (vis(g, y, size)) {
        int cx2 = x + r, cy2 = y + r;
        int nr = 10;
        float step = (float)(g->frame % nr) / (float)nr;
        uint32_t col = g->theme.button.background ? g->theme.button.background : 0x0078d4;
        for (int i = 0; i < nr; i++) {
            float angle = ((float)i / (float)nr - step) * 2.0f * 3.14159f;
            float rel   = fmodf(((float)i / (float)nr - step + 1.0f), 1.0f);
            int a = (int)(rel * rel * 220.0f);
            int dr = r - 4;
            int dx = (int)(cosf(angle) * (float)dr);
            int dy = (int)(sinf(angle) * (float)dr);
            int dot = 3 + (int)(rel * 3.0f);
            fill_rounded_rect_alpha(g, cx2+dx-dot, cy2+dy-dot, dot*2, dot*2, dot, col, a);
        }
    }
    advance(g, size, size);
}

/* ------------------------------------------------------------------ list item (Flutter ListTile) */

bool xgui_list_item(XGui *g, const char *title, const char *subtitle, const char *trailing) {
    if (!g || !title) return false;
    int usable = g->width - 2*g->margin;
    XftFont *ft = get_font(g, g->theme.label.font[0] ? g->theme.label.font : "sans",
                           g->theme.label.font_size > 0 ? g->theme.label.font_size : 13, 600);
    XftFont *fs = get_font(g, "sans", 11, 400);
    int _tw, th_t = 18, th_s = 14;
    if (ft) { text_size(g, ft, title, &_tw, &th_t); }
    int bh = subtitle ? (th_t + th_s + 14) : (th_t + 14);
    int x = g->cx, y = g->cy;
    bool hov = (g->mx >= x && g->mx < x+usable && g->my >= y && g->my < y+bh);
    bool clicked = hov && g->mouse_released;

    if (vis(g, y, bh)) {
        if (hov) fill_rounded_rect(g, x, y, usable, bh, 0,
                      g->theme.list_item_hover.background ? g->theme.list_item_hover.background : 0xf5f5f5);
        if (ft) draw_text(g, ft, title, x+12, y+7+ft->ascent,
                          g->theme.label.color ? g->theme.label.color : 0x222222);
        if (subtitle && fs)
            draw_text(g, fs, subtitle, x+12, y+7+th_t+4+fs->ascent,
                      g->theme.label.color ? (g->theme.label.color | 0x505050) : 0x888888);
        if (trailing && ft) {
            int trw, _trh; text_size(g, ft, trailing, &trw, &_trh);
            draw_text(g, ft, trailing, x+usable-trw-12, y+bh/2-th_t/2+ft->ascent,
                      g->theme.label.color ? g->theme.label.color : 0x888888);
        }
        /* divider */
        fill_rect(g, x+12, y+bh-1, usable-12, 1,
                  g->theme.separator.background ? g->theme.separator.background : 0xeeeeee);
    }
    advance(g, usable, bh);
    return clicked;
}

/* ------------------------------------------------------------------ card */

void xgui_card_begin(XGui *g) {
    if (!g) return;
    g->card_x0 = g->cx - g->theme.card.padding_x;
    g->card_y0 = g->cy - g->theme.card.padding_y;
    g->card_w  = g->width - 2*(g->theme.window.padding_x - g->theme.card.padding_x);
    if (g->card_w <= 0) g->card_w = g->width - 2*g->theme.window.padding_x;
    g->in_card = true;
    g->cy += g->theme.card.padding_y;
}

void xgui_card_end(XGui *g) {
    if (!g) return;
    int h = (g->cy - g->card_y0) + g->theme.card.padding_y;
    int r = g->theme.card.border_radius > 0 ? g->theme.card.border_radius : 10;
    if (vis(g, g->card_y0, h)) {
        draw_shadow(g, g->card_x0, g->card_y0, g->card_w, h, r);
        fill_rounded_rect(g, g->card_x0, g->card_y0, g->card_w, h, r,
                          g->theme.card.background ? g->theme.card.background : 0xffffff);
        if (g->theme.card.border_width > 0 || g->theme.card.border_color)
            draw_outline(g, g->card_x0, g->card_y0, g->card_w, h, r,
                         g->theme.card.border_width > 0 ? g->theme.card.border_width : 1,
                         g->theme.card.border_color ? g->theme.card.border_color : 0xdddddd);
    }
    g->cy += g->theme.card.padding_y + WIDGET_GAP;
    g->in_card = false;
}

/* ------------------------------------------------------------------ group box */

void xgui_group_begin(XGui *g, const char *title) {
    if (!g) return;
    int usable = g->width - 2*g->margin;
    if (vis(g, g->cy, 24) && title) {
        XftFont *f = get_font(g, "sans", 11, 600);
        if (f) {
            int tw, _th; text_size(g, f, title, &tw, &_th);
            uint32_t lc = g->theme.separator.background ? g->theme.separator.background : 0xcccccc;
            draw_outline(g, g->cx, g->cy+10, usable, g->height, 4, 1, lc);
            fill_rect(g, g->cx+12, g->cy+6, tw+8, 10,
                      g->theme.window.background ? g->theme.window.background : 0xf0f0f0);
            draw_text(g, f, title, g->cx+16, g->cy+6+f->ascent,
                      g->theme.label.color ? g->theme.label.color : 0x666666);
        }
    }
    g->cy += 18;
    g->cx += 8;
}

void xgui_group_end(XGui *g) {
    if (!g) return;
    g->cx -= 8;
    g->cy += 8;
}

/* ------------------------------------------------------------------ grid layout */

void xgui_grid_begin(XGui *g, int cols) {
    if (!g || cols <= 0 || cols > GRID_MAX_COLS) return;
    int usable = g->width - 2*g->margin;
    g->in_grid      = true;
    g->grid_cols    = cols;
    g->grid_col_w   = (usable - (cols-1)*WIDGET_GAP) / cols;
    g->grid_col     = 0;
    g->grid_row_y0  = g->cy;
    g->grid_row_h   = 0;
}

void xgui_grid_end(XGui *g) {
    if (!g) return;
    g->in_grid = false;
    if (g->grid_row_h > 0)
        g->cy = g->grid_row_y0 + g->grid_row_h + WIDGET_GAP;
    g->cx = g->theme.window.padding_x;
    g->grid_row_h = 0;
}

/* ------------------------------------------------------------------ row layout */

void xgui_row_begin(XGui *g) {
    if (!g) return;
    g->in_row = true; g->row_max_h = 0;
}

void xgui_row_end(XGui *g) {
    if (!g) return;
    g->in_row = false;
    g->cx     = g->theme.window.padding_x;
    g->cy    += g->row_max_h + WIDGET_GAP;
    g->row_max_h = 0;
}

/* ------------------------------------------------------------------ toast */

void xgui_show_toast(XGui *g, const char *text, int duration_frames) {
    if (!g || !text) return;
    snprintf(g->toast_text, sizeof(g->toast_text), "%s", text);
    g->toast_end_frame = g->frame + (duration_frames > 0 ? duration_frames : 90);
}

/* ------------------------------------------------------------------ tooltip */

void xgui_tooltip(XGui *g, const char *text) {
    if (!g || !text) return;
    snprintf(g->tip_text, sizeof(g->tip_text), "%s", text);
    g->tip_x = g->mx; g->tip_y = g->my;
    g->tip_show = true;
}

/* ------------------------------------------------------------------ raw drawing (game mode) */

void xgui_clear_bg(XGui *g, uint32_t color) {
    if (!g) return;
    fill_rect(g, 0, 0, g->width, g->height, color);
}

void xgui_fill_rect_at(XGui *g, int x, int y, int w, int h, int r, uint32_t color) {
    if (!g) return;
    fill_rounded_rect(g, x, y, w, h, r, color);
}

void xgui_fill_circle_at(XGui *g, int cx2, int cy2, int r, uint32_t color) {
    if (!g || r <= 0) return;
    fill_circle(g, cx2 - r, cy2 - r, r, color);
}

void xgui_draw_line_at(XGui *g, int x1, int y1, int x2, int y2,
                        int thickness, uint32_t color) {
    if (!g) return;
    XSetForeground(g->dpy, g->gc, alloc_color(g, color));
    XSetLineAttributes(g->dpy, g->gc, (unsigned)thickness,
                       LineSolid, CapRound, JoinRound);
    XDrawLine(g->dpy, g->backbuf, g->gc, x1, y1, x2, y2);
}

void xgui_draw_text_at(XGui *g, int x, int y, const char *text,
                        int size, uint32_t color) {
    if (!g || !text) return;
    XftFont *f = get_font(g, "sans", size > 0 ? size : 14, 400);
    if (!f) return;
    draw_text(g, f, text, x, y + f->ascent, color);
}

void xgui_draw_text_centered(XGui *g, int cx2, int cy2, const char *text,
                              int size, uint32_t color) {
    if (!g || !text) return;
    XftFont *f = get_font(g, "sans", size > 0 ? size : 14, 400);
    if (!f) return;
    int tw, th; text_size(g, f, text, &tw, &th);
    draw_text(g, f, text, cx2 - tw/2, cy2 - th/2 + f->ascent, color);
}

void xgui_draw_text_bold_at(XGui *g, int x, int y, const char *text,
                             int size, uint32_t color) {
    if (!g || !text) return;
    XftFont *f = get_font(g, "sans", size > 0 ? size : 14, 700);
    if (!f) return;
    draw_text(g, f, text, x, y + f->ascent, color);
}

void xgui_draw_text_bold_centered(XGui *g, int cx2, int cy2, const char *text,
                                   int size, uint32_t color) {
    if (!g || !text) return;
    XftFont *f = get_font(g, "sans", size > 0 ? size : 14, 700);
    if (!f) return;
    int tw, th; text_size(g, f, text, &tw, &th);
    draw_text(g, f, text, cx2 - tw/2, cy2 - th/2 + f->ascent, color);
}

/* ------------------------------------------------------------------ key queries (game mode) */

bool xgui_key_held_char(XGui *g, char c) {
    if (!g) return false;
    int idx = (int)(unsigned char)c;
    return (idx >= 0 && idx < 256) ? g->key_held[idx] : false;
}
bool xgui_key_w(XGui *g)      { return g && (g->key_held['w'] || g->key_held['W']); }
bool xgui_key_s(XGui *g)      { return g && (g->key_held['s'] || g->key_held['S']); }
bool xgui_key_a(XGui *g)      { return g && (g->key_held['a'] || g->key_held['A']); }
bool xgui_key_d(XGui *g)      { return g && (g->key_held['d'] || g->key_held['D']); }
bool xgui_key_up(XGui *g)     { return g && g->key_up;    }
bool xgui_key_down(XGui *g)   { return g && g->key_down;  }
bool xgui_key_left(XGui *g)   { return g && g->key_left;  }
bool xgui_key_right(XGui *g)  { return g && g->key_right; }
bool xgui_key_space(XGui *g)  { return g && g->key_space; }
bool xgui_key_escape(XGui *g) { return g && g->key_esc_this_frame; }
bool xgui_key_enter_held(XGui *g) { return g && g->key_enter; }
bool xgui_mouse_down(XGui *g) { return g && g->mouse_down; }
int  xgui_mouse_x(XGui *g)    { return g ? g->mx : 0; }
int  xgui_mouse_y(XGui *g)    { return g ? g->my : 0; }

/* ------------------------------------------------------------------ stubs for non-X11 */

#else /* !HAVE_X11 */

#include "xgui.h"
#include <stdio.h>
#include <stdlib.h>

static void no_x11(void) {
    fprintf(stderr, "[xgui] compiled without X11 support\n");
}

XGui       *xgui_init(int w, int h, const char *t)                     { (void)w;(void)h;(void)t; no_x11(); return NULL; }
void        xgui_load_style(XGui *g, const char *p)                    { (void)g;(void)p; }
void        xgui_set_dark(XGui *g, bool d)                             { (void)g;(void)d; }
bool        xgui_running(XGui *g)                                      { (void)g; return false; }
void        xgui_close(XGui *g)                                        { (void)g; }
void        xgui_destroy(XGui *g)                                      { (void)g; }
void        xgui_begin(XGui *g)                                        { (void)g; }
void        xgui_end(XGui *g)                                          { (void)g; }
void        xgui_label(XGui *g, const char *t)                         { (void)g;(void)t; }
bool        xgui_button(XGui *g, const char *t)                        { (void)g;(void)t; return false; }
bool        xgui_icon_button(XGui *g, const char *i, const char *l)    { (void)g;(void)i;(void)l; return false; }
const char *xgui_input(XGui *g, const char *i, const char *p)          { (void)g;(void)i;(void)p; return ""; }
const char *xgui_textarea(XGui *g, const char *i, const char *p)       { (void)g;(void)i;(void)p; return ""; }
void        xgui_spacer(XGui *g, int h)                                { (void)g;(void)h; }
void        xgui_row_begin(XGui *g)                                    { (void)g; }
void        xgui_row_end(XGui *g)                                      { (void)g; }
void        xgui_title(XGui *g, const char *t)                         { (void)g;(void)t; }
void        xgui_subtitle(XGui *g, const char *t)                      { (void)g;(void)t; }
void        xgui_separator(XGui *g)                                    { (void)g; }
void        xgui_section(XGui *g, const char *t)                       { (void)g;(void)t; }
bool        xgui_checkbox(XGui *g, const char *i, const char *l)       { (void)g;(void)i;(void)l; return false; }
bool        xgui_toggle(XGui *g, const char *i, bool v, const char *l) { (void)g;(void)i;(void)l; return v; }
void        xgui_progress(XGui *g, int v, int m)                       { (void)g;(void)v;(void)m; }
float       xgui_slider(XGui *g, const char *i, float mn, float mx, float c) { (void)g;(void)i;(void)mn;(void)mx; return c; }
void        xgui_badge(XGui *g, const char *t, uint32_t b)             { (void)g;(void)t;(void)b; }
bool        xgui_chip(XGui *g, const char *t, bool r)                  { (void)g;(void)t;(void)r; return false; }
int         xgui_tabs(XGui *g, const char *i, const char **l, int c)   { (void)g;(void)i;(void)l;(void)c; return 0; }
int         xgui_select(XGui *g, const char *i, const char **o, int c, int cur) { (void)g;(void)i;(void)o;(void)c; return cur; }
void        xgui_spinner(XGui *g, int s)                               { (void)g;(void)s; }
bool        xgui_list_item(XGui *g, const char *t, const char *s, const char *tr) { (void)g;(void)t;(void)s;(void)tr; return false; }
void        xgui_card_begin(XGui *g)                                   { (void)g; }
void        xgui_card_end(XGui *g)                                     { (void)g; }
void        xgui_group_begin(XGui *g, const char *t)                   { (void)g;(void)t; }
void        xgui_group_end(XGui *g)                                    { (void)g; }
void        xgui_grid_begin(XGui *g, int c)                            { (void)g;(void)c; }
void        xgui_grid_end(XGui *g)                                     { (void)g; }
void        xgui_tooltip(XGui *g, const char *t)                       { (void)g;(void)t; }
void        xgui_show_toast(XGui *g, const char *t, int d)             { (void)g;(void)t;(void)d; }
void        xgui_clear_bg(XGui *g, uint32_t c)                         { (void)g;(void)c; }
void        xgui_fill_rect_at(XGui *g, int x, int y, int w, int h, int r, uint32_t c) { (void)g;(void)x;(void)y;(void)w;(void)h;(void)r;(void)c; }
void        xgui_fill_circle_at(XGui *g, int x, int y, int r, uint32_t c) { (void)g;(void)x;(void)y;(void)r;(void)c; }
void        xgui_draw_line_at(XGui *g, int x1, int y1, int x2, int y2, int t, uint32_t c) { (void)g;(void)x1;(void)y1;(void)x2;(void)y2;(void)t;(void)c; }
void        xgui_draw_text_at(XGui *g, int x, int y, const char *t, int s, uint32_t c) { (void)g;(void)x;(void)y;(void)t;(void)s;(void)c; }
void        xgui_draw_text_centered(XGui *g, int x, int y, const char *t, int s, uint32_t c) { (void)g;(void)x;(void)y;(void)t;(void)s;(void)c; }
void        xgui_draw_text_bold_at(XGui *g, int x, int y, const char *t, int s, uint32_t c) { (void)g;(void)x;(void)y;(void)t;(void)s;(void)c; }
void        xgui_draw_text_bold_centered(XGui *g, int x, int y, const char *t, int s, uint32_t c) { (void)g;(void)x;(void)y;(void)t;(void)s;(void)c; }
void        xgui_set_scroll(XGui *g, int y) { (void)g;(void)y; }
int         xgui_get_scroll(XGui *g) { (void)g; return 0; }
void        xgui_set_hscroll(XGui *g, int x) { (void)g;(void)x; }
int         xgui_get_hscroll(XGui *g) { (void)g; return 0; }
void        xgui_scroll_to_bottom(XGui *g) { (void)g; }
void        xgui_ensure_visible(XGui *g, int x, int y, int w, int h) { (void)g;(void)x;(void)y;(void)w;(void)h; }
bool        xgui_key_held_char(XGui *g, char c)  { (void)g;(void)c; return false; }
bool        xgui_key_w(XGui *g)                  { (void)g; return false; }
bool        xgui_key_s(XGui *g)                  { (void)g; return false; }
bool        xgui_key_a(XGui *g)                  { (void)g; return false; }
bool        xgui_key_d(XGui *g)                  { (void)g; return false; }
bool        xgui_key_up(XGui *g)                 { (void)g; return false; }
bool        xgui_key_down(XGui *g)               { (void)g; return false; }
bool        xgui_key_left(XGui *g)               { (void)g; return false; }
bool        xgui_key_right(XGui *g)              { (void)g; return false; }
bool        xgui_key_space(XGui *g)              { (void)g; return false; }
bool        xgui_key_escape(XGui *g)             { (void)g; return false; }
bool        xgui_key_enter_held(XGui *g)         { (void)g; return false; }
bool        xgui_mouse_down(XGui *g)             { (void)g; return false; }
int         xgui_mouse_x(XGui *g)               { (void)g; return 0; }
int         xgui_mouse_y(XGui *g)               { (void)g; return 0; }
int         xgui_win_w(XGui *g)                 { (void)g; return 0; }
int         xgui_win_h(XGui *g)                 { (void)g; return 0; }
float       xgui_delta_ms(XGui *g)              { (void)g; return 16.0f; }
long long   xgui_clock_ms(XGui *g)              { (void)g; return 0LL; }
void        xgui_sleep_ms(XGui *g, int ms)      { (void)g;(void)ms; }

#endif /* HAVE_X11 */
