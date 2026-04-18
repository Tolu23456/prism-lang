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

/* Momentum / rubber-band constants */
#define MOMENTUM_FRICTION   0.88f   /* velocity multiplied each frame */
#define MOMENTUM_THRESH     0.5f    /* px/frame — below this, spring takes over */
#define RUBBER_BAND_K       0.45f   /* rubber-band stretch factor */
#define RUBBER_SPRING       0.18f   /* spring-back pull per frame (fraction of overscroll) */
#define COLLAPSING_MAX      32      /* max simultaneously tracked collapsible sections */
#define CMENU_ITEM_H        34      /* context menu item height */

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

    /* ── Momentum + rubber-band scroll ───────────────────── */
    float scroll_momentum;      /* current velocity in px/frame */
    float scroll_rubber;        /* rubber-band overscroll (px beyond content bounds) */
    int   scroll_delta_frame;   /* raw wheel delta accumulated this frame (xgui_scroll_delta) */

    /* ── Single-frame key-pressed map ────────────────────── */
    bool  key_pressed_map[256]; /* true for one frame when that char was pressed */

    /* ── Modal dialog ─────────────────────────────────────── */
    char  modal_id[64];
    bool  modal_open;
    bool  in_modal;
    int   modal_x, modal_y, modal_w, modal_h;

    /* ── Collapsing sections ──────────────────────────────── */
    char  coll_ids[COLLAPSING_MAX][64];
    bool  coll_open[COLLAPSING_MAX];
    int   coll_count;

    /* ── Data table ───────────────────────────────────────── */
    bool  in_table;
    int   table_cols;           /* column count */
    int   table_col;            /* current column (0-based) */
    int   table_col_w;          /* computed column width */
    int   table_row_idx;        /* 0=header, 1..N=data rows (for zebra-stripe) */
    int   table_row_y;          /* y of current row top */
    int   table_row_h;          /* height of current row */
    char  table_sel_id[64];     /* selected row id */

    /* ── Tree view ────────────────────────────────────────── */
    char  tree_sel[64];         /* selected node id */

    /* ── Context menu ─────────────────────────────────────── */
    char  cmenu_id[64];
    bool  cmenu_open;
    int   cmenu_x, cmenu_y;
    int   cmenu_item_y;         /* current item draw y */

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
static void fill_rect_alpha(XGui *g, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    if (alpha <= 0) return;
    if (alpha >= 255) { fill_rect(g, x, y, w, h, rgb); return; }
    /* Blend over existing backbuffer pixels using XRender if available, otherwise approximate */
    /* Simple software blend: draw a semi-transparent overlay by drawing two rects via XRender */
#ifdef HAVE_X11
    if (g->backbuf_pic) {
        XRenderColor rc;
        rc.red   = (unsigned short)(((rgb >> 16) & 0xff) * alpha * 257 / 255);
        rc.green = (unsigned short)(((rgb >>  8) & 0xff) * alpha * 257 / 255);
        rc.blue  = (unsigned short)(((rgb      ) & 0xff) * alpha * 257 / 255);
        rc.alpha = (unsigned short)(alpha * 257);
        Picture src_pic = XRenderCreateSolidFill(g->dpy, &rc);
        if (src_pic) {
            XRenderComposite(g->dpy, PictOpOver, src_pic, None, g->backbuf_pic,
                             0, 0, 0, 0, x, y, (unsigned)w, (unsigned)h);
            XRenderFreePicture(g->dpy, src_pic);
        }
        return;
    }
#endif
    /* Fallback: just draw opaque (no blending path available) */
    fill_rect(g, x, y, w, h, rgb);
}

static void draw_circle_outline(XGui *g, int cx2, int cy2, int r, int lw, uint32_t rgb) {
    if (r <= 0 || lw <= 0) return;
    XSetForeground(g->dpy, g->gc, alloc_color(g, rgb));
    XSetLineAttributes(g->dpy, g->gc, (unsigned)lw, LineSolid, CapRound, JoinRound);
    XDrawArc(g->dpy, g->backbuf, g->gc,
             cx2 - r, cy2 - r, (unsigned)(2*r), (unsigned)(2*r), 0, 360*64);
    XSetLineAttributes(g->dpy, g->gc, 1, LineSolid, CapButt, JoinMiter);
}

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
    g->mouse_released    = false;
    g->key_count         = 0;
    g->key_backspace     = false;
    g->key_enter         = false;
    g->key_esc_this_frame = false;
    g->tip_show          = false;
    g->dd.active         = false;
    g->scroll_delta_frame = 0;
    memset(g->key_pressed_map, 0, sizeof(g->key_pressed_map));

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
                /* Scroll up — add negative momentum */
                g->scroll_momentum -= (float)SCROLL_STEP;
                g->scroll_delta_frame -= 1;
            } else if (e.xbutton.button == Button5) {
                /* Scroll down — add positive momentum */
                g->scroll_momentum += (float)SCROLL_STEP;
                g->scroll_delta_frame += 1;
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
            else if (buf[0] >= 32 && g->key_count < KEY_BUF - 1)
                g->key_chars[g->key_count++] = buf[0];
            /* hold state + single-frame pressed map */
            { unsigned char _c0 = (unsigned char)buf[0];
              if (_c0 >= 32 && _c0 < 128) {
                  g->key_held[(int)_c0]         = true;
                  g->key_pressed_map[(int)_c0]  = true;
              }
            }
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

    /* Apply key events to focused input */
    if (g->focused_id[0]) {
        InputState *inp = find_input(g, g->focused_id);
        int len = (int)strlen(inp->buf);
        if (g->key_backspace && len > 0) inp->buf[--len] = '\0';
        for (int i = 0; i < g->key_count && len < INPUT_BUF - 2; i++)
            inp->buf[len++] = g->key_chars[i];
        inp->buf[len] = '\0';
    }

    /* ── Momentum + rubber-band + spring-physics scroll ─────────── */
    if (!g->sb_dragging) {
        int max_scroll = g->content_h - g->height;
        if (max_scroll < 0) max_scroll = 0;

        /* Phase 1: momentum (from wheel/flick) */
        if (fabsf(g->scroll_momentum) >= MOMENTUM_THRESH) {
            float proposed = g->scroll_pos_f + g->scroll_momentum;
            /* rubber-band: reduce effective movement past bounds */
            if (proposed < 0.0f) {
                float over = -proposed;
                g->scroll_pos_f = -(over * RUBBER_BAND_K);
            } else if (proposed > (float)max_scroll) {
                float over = proposed - (float)max_scroll;
                g->scroll_pos_f = (float)max_scroll + over * RUBBER_BAND_K;
            } else {
                g->scroll_pos_f = proposed;
            }
            g->scroll_target = (int)(g->scroll_pos_f + 0.5f);
            if (g->scroll_target < 0) g->scroll_target = 0;
            if (g->scroll_target > max_scroll) g->scroll_target = max_scroll;
            /* friction decay */
            g->scroll_momentum *= MOMENTUM_FRICTION;
        } else {
            g->scroll_momentum = 0.0f;

            /* Phase 2: rubber-band spring-back when out of bounds */
            if (g->scroll_pos_f < 0.0f) {
                g->scroll_pos_f += (-g->scroll_pos_f) * RUBBER_SPRING;
                if (g->scroll_pos_f >= -0.5f) g->scroll_pos_f = 0.0f;
                g->scroll_target = 0;
            } else if (g->scroll_pos_f > (float)max_scroll) {
                g->scroll_pos_f += ((float)max_scroll - g->scroll_pos_f) * RUBBER_SPRING;
                if (fabsf(g->scroll_pos_f - (float)max_scroll) < 0.5f)
                    g->scroll_pos_f = (float)max_scroll;
                g->scroll_target = max_scroll;
            } else {
                /* Phase 3: critically-damped spring to scroll_target */
                float dt = g->delta_ms / 1000.0f;
                if (dt > 0.1f) dt = 0.1f;
                float omega     = SCROLL_OMEGA;
                float exp_term  = expf(-omega * dt);
                float diff_pos  = g->scroll_pos_f - (float)g->scroll_target;
                float n1        = g->scroll_vel + omega * diff_pos;
                float next_pos  = (float)g->scroll_target + (diff_pos + n1 * dt) * exp_term;
                float next_vel  = (g->scroll_vel - omega * n1 * dt) * exp_term;
                g->scroll_pos_f = next_pos;
                g->scroll_vel   = next_vel;
                if (fabsf(g->scroll_pos_f - (float)g->scroll_target) < SNAP_THRESH &&
                    fabsf(g->scroll_vel) < SNAP_THRESH) {
                    g->scroll_pos_f = (float)g->scroll_target;
                    g->scroll_vel   = 0.0f;
                }
            }
        }
        g->scroll_y = (int)(g->scroll_pos_f + 0.5f);
    }

    /* Clear backbuffer */
    fill_rect(g, 0, 0, g->width, g->height, g->theme.window.background);

    /* Reset layout cursor */
    g->cx        = g->theme.window.padding_x;
    g->cy        = g->margin - g->scroll_y;
    g->in_row    = false;
    g->row_max_h = 0;
    g->in_grid   = false;
}

/* ------------------------------------------------------------------ frame end */

void xgui_end(XGui *g) {
    if (!g) return;

    /* Record content height */
    g->content_h = g->cy + g->scroll_y + g->theme.window.padding_y;
    int maxs = g->content_h - g->height;
    if (maxs < 0) maxs = 0;
    if (g->scroll_target > maxs) {
        g->scroll_target = maxs;
    }
    if (g->scroll_target < 0) {
        g->scroll_target = 0;
    }

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

/* ------------------------------------------------------------------ scroll queries */

int xgui_scroll_y(XGui *g)     { return g ? g->scroll_y : 0; }
int xgui_scroll_delta(XGui *g) { return g ? g->scroll_delta_frame : 0; }

/* ------------------------------------------------------------------ key_pressed (single-frame) */

bool xgui_key_pressed(XGui *g, char c) {
    if (!g) return false;
    int idx = (int)(unsigned char)c;
    return (idx >= 0 && idx < 256) && g->key_pressed_map[idx];
}

/* ------------------------------------------------------------------ radio group */

int xgui_radio(XGui *g, const char *id, const char **labels, int count, int current) {
    if (!g || !labels || count <= 0) return current;
    PssStyle *sr  = &g->theme.radio;
    PssStyle *src = &g->theme.radio_checked;
    XftFont  *f   = get_font(g, "sans", 13, 400);

    int sel = current;
    for (int i = 0; i < count; i++) {
        /* Radio circle */
        int rx = g->cx, ry = g->cy;
        int rd = 18;  /* diameter */
        int bh = rd + 2 * (sr->padding_y > 0 ? sr->padding_y : 2);

        bool hov   = (g->mx >= rx && g->mx < rx + rd &&
                      g->my >= ry && g->my < ry + rd);
        bool clicked = hov && g->mouse_released;
        if (clicked) sel = i;

        if (vis(g, ry, bh)) {
            PssStyle *suse = (i == sel) ? src : sr;
            /* outer circle */
            fill_circle(g, rx, ry, rd / 2,
                        suse->background ? suse->background : 0xffffff);
            draw_circle_outline(g, rx + rd/2, ry + rd/2, rd/2,
                                suse->border_width > 0 ? suse->border_width : 2,
                                suse->border_color ? suse->border_color
                                                   : (i == sel ? 0x0078d4 : 0xbcbcbc));
            /* inner fill dot when selected */
            if (i == sel) {
                int ir = rd / 4;
                fill_circle(g, rx + rd/2 - ir, ry + rd/2 - ir, ir,
                            suse->accent_color ? suse->accent_color : 0x0078d4);
            }
            /* label */
            if (f && labels[i]) {
                draw_text(g, f, labels[i], rx + rd + 8, ry + f->ascent + (rd - f->ascent - f->descent)/2,
                          0x222222);
            }
        }
        /* build id for input collision */
        char rid[128];
        snprintf(rid, sizeof(rid), "%s#%d", id ? id : "radio", i);
        (void)rid;
        g->cy += bh + (WIDGET_GAP / 2);
    }
    return sel;
}

/* ------------------------------------------------------------------ collapsing section */

/* Returns index of id in coll_ids, or -1. */
static int coll_find(XGui *g, const char *id) {
    for (int i = 0; i < g->coll_count; i++)
        if (strcmp(g->coll_ids[i], id) == 0) return i;
    return -1;
}

bool xgui_collapsing(XGui *g, const char *id, const char *label) {
    if (!g || !id || !label) return false;

    /* Find or register this section */
    int ci = coll_find(g, id);
    if (ci < 0 && g->coll_count < COLLAPSING_MAX) {
        ci = g->coll_count++;
        snprintf(g->coll_ids[ci], 64, "%s", id);
        g->coll_open[ci] = false;
    }
    if (ci < 0) return false;

    bool open  = g->coll_open[ci];
    PssStyle *s = open ? &g->theme.collapsing_open : &g->theme.collapsing;
    int px  = s->padding_x > 0 ? s->padding_x : 12;
    int py  = s->padding_y > 0 ? s->padding_y : 8;
    int fsz = s->font_size > 0 ? s->font_size : 13;
    int fw  = s->font_weight > 0 ? s->font_weight : 400;
    XftFont *f = get_font(g, "sans", fsz, fw);
    int th = f ? f->ascent + f->descent : 16;
    int bh = th + 2 * py;
    int bw = g->width - g->cx - g->margin;

    bool hov = (g->mx >= g->cx && g->mx < g->cx + bw &&
                g->my >= g->cy && g->my < g->cy + bh);

    if (vis(g, g->cy, bh)) {
        uint32_t bg = s->background ? s->background : 0xf8f8f8;
        if (hov) bg = open ? (g->theme.collapsing_open.background ? g->theme.collapsing_open.background : 0xeef4ff)
                           : 0xeeeef4;
        fill_rounded_rect(g, g->cx, g->cy, bw, bh, s->border_radius > 0 ? s->border_radius : 6, bg);
        if (s->border_width > 0)
            draw_outline(g, g->cx, g->cy, bw, bh, s->border_radius > 0 ? s->border_radius : 6,
                         s->border_width, s->border_color ? s->border_color : 0xeeeeee);
        /* arrow triangle */
        int ay = g->cy + bh / 2;
        int ax = g->cx + px;
        if (open) {
            /* ▼ */
            XSetForeground(g->dpy, g->gc, alloc_color(g, s->color ? s->color : 0x555555));
            XPoint tri[3] = { {(short)(ax), (short)(ay-4)},
                              {(short)(ax+8), (short)(ay-4)},
                              {(short)(ax+4), (short)(ay+4)} };
            XFillPolygon(g->dpy, g->backbuf, g->gc, tri, 3, Convex, CoordModeOrigin);
        } else {
            /* ▶ */
            XSetForeground(g->dpy, g->gc, alloc_color(g, s->color ? s->color : 0x888888));
            XPoint tri[3] = { {(short)(ax), (short)(ay-5)},
                              {(short)(ax), (short)(ay+5)},
                              {(short)(ax+8), (short)(ay)} };
            XFillPolygon(g->dpy, g->backbuf, g->gc, tri, 3, Convex, CoordModeOrigin);
        }
        /* label */
        if (f)
            draw_text(g, f, label, ax + 14, g->cy + py + f->ascent,
                      s->color ? s->color : 0x333333);
    }

    if (hov && g->mouse_released)
        g->coll_open[ci] = !g->coll_open[ci];

    advance(g, bw, bh);
    return g->coll_open[ci];
}

void xgui_collapsing_end(XGui *g) {
    /* Visual bottom margin when a collapsing section closes */
    if (g) g->cy += WIDGET_GAP / 2;
}

/* ------------------------------------------------------------------ modal dialog */

void xgui_modal_open(XGui *g, const char *id) {
    if (!g || !id) return;
    snprintf(g->modal_id, sizeof(g->modal_id), "%s", id);
    g->modal_open = true;
}

void xgui_modal_close(XGui *g) {
    if (!g) return;
    g->modal_open = false;
    g->modal_id[0] = '\0';
    g->in_modal = false;
}

bool xgui_modal_begin(XGui *g, const char *id, const char *title, int mw, int mh) {
    if (!g || !id) return false;
    if (!g->modal_open || strcmp(g->modal_id, id) != 0) return false;

    PssStyle *ov = &g->theme.modal_overlay;
    PssStyle *ms = &g->theme.modal;
    PssStyle *mt = &g->theme.modal_title;

    /* Dim overlay */
    int alpha = ov->opacity > 0 ? (ov->opacity * 255 / 100) : 140;
    fill_rect_alpha(g, 0, 0, g->width, g->height,
                    ov->background ? ov->background : 0x000000, alpha);

    /* Modal box */
    if (mw <= 0) mw = 480;
    if (mh <= 0) mh = 320;
    int mx2 = (g->width  - mw) / 2;
    int my2 = (g->height - mh) / 2;
    g->modal_x = mx2; g->modal_y = my2;
    g->modal_w = mw;  g->modal_h = mh;

    /* Shadow */
    draw_shadow(g, mx2, my2, mw, mh, ms->border_radius > 0 ? ms->border_radius : 10);

    /* Background */
    fill_rounded_rect(g, mx2, my2, mw, mh,
                      ms->border_radius > 0 ? ms->border_radius : 10,
                      ms->background ? ms->background : 0xffffff);

    /* Title bar */
    int tpy = mt->padding_y > 0 ? mt->padding_y : 8;
    int tfsz = mt->font_size > 0 ? mt->font_size : 18;
    XftFont *tf = get_font(g, "sans", tfsz, 700);
    int title_h = tf ? (tf->ascent + tf->descent + 2*tpy + 8) : 48;
    if (tf && title) {
        draw_text(g, tf, title, mx2 + 24, my2 + tpy + tf->ascent,
                  mt->color ? mt->color : 0x111111);
    }
    /* Divider below title */
    fill_rect(g, mx2, my2 + title_h - 1, mw, 1,
              mt->border_color ? mt->border_color : 0xeeeeee);

    /* Close button (X) in top right */
    int cx2 = mx2 + mw - 36, cy2 = my2 + (title_h - 24) / 2;
    bool xhov = (g->mx >= cx2 && g->mx < cx2 + 24 && g->my >= cy2 && g->my < cy2 + 24);
    if (xhov) fill_rounded_rect(g, cx2, cy2, 24, 24, 4, 0xeeeeee);
    XftFont *xf = get_font(g, "sans", 14, 400);
    if (xf) draw_text(g, xf, "✕", cx2 + 4, cy2 + xf->ascent + 2, 0x666666);
    if (xhov && g->mouse_released) { xgui_modal_close(g); return false; }

    /* Push layout cursor into modal content area */
    g->in_modal = true;
    g->cx       = mx2 + (ms->padding_x > 0 ? ms->padding_x : 24);
    g->cy       = my2 + title_h + 8;
    g->margin   = mx2 + (ms->padding_x > 0 ? ms->padding_x : 24);

    return true;
}

void xgui_modal_end(XGui *g) {
    if (!g || !g->in_modal) return;
    /* restore layout cursor outside modal */
    g->in_modal = false;
    g->cx       = g->theme.window.padding_x;
    g->cy       = g->content_h;  /* continue below previous content */
    g->margin   = g->theme.window.padding_x;
}

bool xgui_modal_button(XGui *g, const char *label) {
    bool clicked = xgui_button(g, label);
    if (clicked) xgui_modal_close(g);
    return clicked;
}

/* ------------------------------------------------------------------ spinbox */

double xgui_spinbox(XGui *g, const char *id, double min_v, double max_v,
                    double current, double step) {
    if (!g || !id) return current;
    if (step == 0.0) step = 1.0;

    /* Get/create an input buffer for this spinbox value */
    InputState *inp = find_input(g, id);
    /* Initialise buffer from current if it doesn't match */
    double stored = atof(inp->buf);
    if (inp->buf[0] == '\0') {
        snprintf(inp->buf, sizeof(inp->buf), "%g", current);
        stored = current;
    }

    PssStyle *ss  = &g->theme.spinbox;
    PssStyle *ssb = &g->theme.spinbox_button;
    PssStyle *sbh = &g->theme.spinbox_button_hover;

    int bw = (g->width - g->cx - g->margin);
    if (bw < 100) bw = 100;
    int btn_w = ssb->min_width > 0 ? ssb->min_width : 26;
    int inp_w = bw - 2 * btn_w - 4;
    int bh    = 32;

    XftFont *f = get_font(g, "sans", ss->font_size > 0 ? ss->font_size : 13, 400);

    if (vis(g, g->cy, bh)) {
        int ix = g->cx, iy = g->cy;

        /* minus button */
        int mbx = ix, mby = iy;
        bool mbhov = (g->mx >= mbx && g->mx < mbx + btn_w &&
                      g->my >= mby && g->my < mby + bh);
        PssStyle *bu = mbhov ? sbh : ssb;
        fill_rounded_rect(g, mbx, mby, btn_w, bh,
                          bu->border_radius > 0 ? bu->border_radius : 4,
                          bu->background ? bu->background : 0xf4f4f4);
        draw_outline(g, mbx, mby, btn_w, bh,
                     bu->border_radius > 0 ? bu->border_radius : 4, 1,
                     bu->border_color ? bu->border_color : 0xbcbcbc);
        XftFont *bf = get_font(g, "sans", 16, 700);
        if (bf) {
            int tw, th; text_size(g, bf, "−", &tw, &th);
            draw_text(g, bf, "−", mbx + (btn_w - tw)/2, mby + bf->ascent + (bh - th)/2,
                      bu->color ? bu->color : 0x444444);
        }
        if (mbhov && g->mouse_released) {
            stored -= step;
            if (stored < min_v) stored = min_v;
            snprintf(inp->buf, sizeof(inp->buf), "%g", stored);
        }

        /* input field */
        int ifx = ix + btn_w + 2;
        bool focused = (strcmp(g->focused_id, id) == 0);
        fill_rounded_rect(g, ifx, iy, inp_w, bh,
                          ss->border_radius > 0 ? ss->border_radius : 4,
                          ss->background ? ss->background : 0xffffff);
        draw_outline(g, ifx, iy, inp_w, bh,
                     ss->border_radius > 0 ? ss->border_radius : 4,
                     focused ? 2 : 1,
                     focused ? 0x0078d4 : (ss->border_color ? ss->border_color : 0xbcbcbc));
        if (f && inp->buf[0]) {
            int tw, th; text_size(g, f, inp->buf, &tw, &th);
            draw_text(g, f, inp->buf, ifx + (inp_w - tw)/2,
                      iy + f->ascent + (bh - th)/2,
                      ss->color ? ss->color : 0x222222);
        }
        /* click to focus */
        if (g->mx >= ifx && g->mx < ifx + inp_w &&
            g->my >= iy   && g->my < iy + bh && g->mouse_released) {
            snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
        }

        /* plus button */
        int pbx = ifx + inp_w + 2, pby = iy;
        bool pbhov = (g->mx >= pbx && g->mx < pbx + btn_w &&
                      g->my >= pby && g->my < pby + bh);
        PssStyle *pb = pbhov ? sbh : ssb;
        fill_rounded_rect(g, pbx, pby, btn_w, bh,
                          pb->border_radius > 0 ? pb->border_radius : 4,
                          pb->background ? pb->background : 0xf4f4f4);
        draw_outline(g, pbx, pby, btn_w, bh,
                     pb->border_radius > 0 ? pb->border_radius : 4, 1,
                     pb->border_color ? pb->border_color : 0xbcbcbc);
        if (bf) {
            int tw, th; text_size(g, bf, "+", &tw, &th);
            draw_text(g, bf, "+", pbx + (btn_w - tw)/2, pby + bf->ascent + (bh - th)/2,
                      pb->color ? pb->color : 0x444444);
        }
        if (pbhov && g->mouse_released) {
            stored += step;
            if (stored > max_v) stored = max_v;
            snprintf(inp->buf, sizeof(inp->buf), "%g", stored);
        }
    }

    advance(g, bw, bh);

    /* Clamp and return current value */
    double val = atof(inp->buf);
    if (val < min_v) val = min_v;
    if (val > max_v) val = max_v;
    return val;
}

/* ------------------------------------------------------------------ status bar */

void xgui_status_bar(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.status_bar;
    int fsz  = s->font_size  > 0 ? s->font_size  : 12;
    int px   = s->padding_x  > 0 ? s->padding_x  : 12;
    int py   = s->padding_y  > 0 ? s->padding_y  : 4;
    int minh = s->min_height > 0 ? s->min_height : 24;
    XftFont *f = get_font(g, "sans", fsz, 400);
    int th = f ? f->ascent + f->descent : 16;
    int bh = (th + 2*py > minh) ? th + 2*py : minh;

    /* Always drawn at the physical bottom of the window */
    int sy = g->height - bh;
    fill_rect(g, 0, sy, g->width, bh,
              s->background ? s->background : 0xf0f0f0);
    if (s->border_top > 0)
        fill_rect(g, 0, sy, g->width, s->border_top,
                  s->border_color ? s->border_color : 0xdddddd);
    if (f)
        draw_text(g, f, text, px, sy + py + f->ascent,
                  s->color ? s->color : 0x666666);
}

/* ------------------------------------------------------------------ data table */

void xgui_table_begin(XGui *g, const char *id, int cols) {
    if (!g || cols <= 0) return;
    (void)id;
    g->in_table     = true;
    g->table_cols   = cols;
    g->table_col    = 0;
    g->table_row_idx = 0;
    /* column width: equal split of usable width */
    int usable = g->width - g->cx - g->margin;
    g->table_col_w = usable / cols;
    /* outer border */
    PssStyle *st = &g->theme.table;
    if (st->border_width > 0) {
        /* We'll draw the outer border in xgui_table_end */
    }
}

void xgui_table_header(XGui *g, const char **headers, int count) {
    if (!g || !g->in_table || !headers) return;
    PssStyle *sh = &g->theme.table_header;
    int fsz = sh->font_size  > 0 ? sh->font_size  : 12;
    int fw  = sh->font_weight > 0 ? sh->font_weight : 600;
    int px  = sh->padding_x  > 0 ? sh->padding_x  : 12;
    int py  = sh->padding_y  > 0 ? sh->padding_y  :  8;
    XftFont *f = get_font(g, "sans", fsz, fw);
    int th = f ? f->ascent + f->descent : 16;
    int bh = th + 2 * py;
    int n  = count < g->table_cols ? count : g->table_cols;

    if (vis(g, g->cy, bh)) {
        fill_rect(g, g->cx, g->cy, g->width - g->cx - g->margin, bh,
                  sh->background ? sh->background : 0xf4f4f4);
        for (int i = 0; i < n; i++) {
            int cx2 = g->cx + i * g->table_col_w;
            if (f && headers[i])
                draw_text(g, f, headers[i], cx2 + px, g->cy + py + f->ascent,
                          sh->color ? sh->color : 0x444444);
            /* right separator */
            if (i < n - 1)
                fill_rect(g, cx2 + g->table_col_w - 1, g->cy, 1, bh,
                          sh->border_color ? sh->border_color : 0xcccccc);
        }
        /* bottom border */
        fill_rect(g, g->cx, g->cy + bh - (sh->border_bottom > 0 ? sh->border_bottom : 2),
                  g->width - g->cx - g->margin, sh->border_bottom > 0 ? sh->border_bottom : 2,
                  sh->border_color ? sh->border_color : 0xcccccc);
    }
    g->cy += bh;
    g->table_row_idx = 1;
}

bool xgui_table_row_begin(XGui *g, const char *row_id) {
    if (!g || !g->in_table) return false;
    bool alt      = (g->table_row_idx % 2 == 0);
    bool selected = (row_id && strcmp(g->table_sel_id, row_id) == 0);
    PssStyle *sr;
    if      (selected) sr = &g->theme.table_row_selected;
    else if (alt)      sr = &g->theme.table_row_alt;
    else               sr = &g->theme.table_row;

    int py  = sr->padding_y > 0 ? sr->padding_y : 8;
    XftFont *f = get_font(g, "sans", 13, 400);
    int th = f ? f->ascent + f->descent : 16;
    g->table_row_h = th + 2 * py;
    g->table_row_y = g->cy;
    g->table_col   = 0;

    bool hov = (g->mx >= g->cx && g->mx < g->width - g->margin &&
                g->my >= g->cy && g->my < g->cy + g->table_row_h);
    PssStyle *draw_s = hov && !selected ? &g->theme.table_row_hover : sr;
    if (vis(g, g->cy, g->table_row_h))
        fill_rect(g, g->cx, g->cy, g->width - g->cx - g->margin, g->table_row_h,
                  draw_s->background ? draw_s->background : (alt ? 0xfafafa : 0xffffff));

    if (hov && g->mouse_released && row_id)
        snprintf(g->table_sel_id, sizeof(g->table_sel_id), "%s", row_id);

    return selected;
}

void xgui_table_cell(XGui *g, const char *text) {
    if (!g || !g->in_table || !text) return;
    PssStyle *sc = &g->theme.table_cell;
    int px = sc->padding_x > 0 ? sc->padding_x : 12;
    int py = sc->padding_y > 0 ? sc->padding_y : 8;
    XftFont *f = get_font(g, "sans", 13, 400);

    int cx2 = g->cx + g->table_col * g->table_col_w;
    if (vis(g, g->table_row_y, g->table_row_h)) {
        /* clip cell text */
        if (f) draw_text(g, f, text, cx2 + px, g->table_row_y + py + f->ascent,
                         sc->color ? sc->color : 0x333333);
        /* right-side separator */
        if (g->table_col < g->table_cols - 1)
            fill_rect(g, cx2 + g->table_col_w - 1, g->table_row_y, 1, g->table_row_h,
                      0xeeeeee);
    }
    g->table_col++;
}

void xgui_table_row_end(XGui *g) {
    if (!g || !g->in_table) return;
    /* bottom separator */
    if (vis(g, g->table_row_y, g->table_row_h))
        fill_rect(g, g->cx, g->table_row_y + g->table_row_h - 1,
                  g->width - g->cx - g->margin, 1, 0xeeeeee);
    g->cy += g->table_row_h;
    g->table_row_idx++;
}

void xgui_table_end(XGui *g) {
    if (!g) return;
    g->in_table = false;
    /* outer table border */
    PssStyle *st = &g->theme.table;
    if (st->border_width > 0) {
        int bw = g->width - g->cx - g->margin;
        int bh = g->cy - g->table_row_y - g->table_row_h;  /* approx */
        (void)bw; (void)bh;
    }
    g->cy += WIDGET_GAP;
}

/* ------------------------------------------------------------------ tree view */

void xgui_tree_begin(XGui *g, const char *id) {
    if (!g) return;
    (void)id;
}

bool xgui_tree_node(XGui *g, const char *node_id, const char *label,
                    bool has_children, int depth) {
    if (!g || !label) return false;
    /* Determine expanded state via coll_ids system */
    int ci = coll_find(g, node_id ? node_id : label);
    if (ci < 0 && g->coll_count < COLLAPSING_MAX) {
        ci = g->coll_count++;
        snprintf(g->coll_ids[ci], 64, "%s", node_id ? node_id : label);
        g->coll_open[ci] = false;
    }
    bool expanded = (ci >= 0) ? g->coll_open[ci] : false;
    bool selected  = (node_id && strcmp(g->tree_sel, node_id) == 0);

    PssStyle *sn = selected ? &g->theme.tree_node_selected
                 : expanded ? &g->theme.tree_node_expanded
                 :            &g->theme.tree_node;
    int indent = depth * 20;
    int px  = (sn->padding_x > 0 ? sn->padding_x : 8) + indent;
    int py  = sn->padding_y > 0 ? sn->padding_y : 5;
    int fsz = sn->font_size  > 0 ? sn->font_size : 13;
    int fw  = sn->font_weight > 0 ? sn->font_weight : 400;
    XftFont *f = get_font(g, "sans", fsz, fw);
    int th = f ? f->ascent + f->descent : 16;
    int bh = th + 2 * py;
    int bw = g->width - g->cx - g->margin;

    bool hov = (g->mx >= g->cx && g->mx < g->cx + bw &&
                g->my >= g->cy && g->my < g->cy + bh);

    if (vis(g, g->cy, bh)) {
        uint32_t bg = 0;
        if (selected) bg = sn->background ? sn->background : 0x0078d4;
        else if (hov)  bg = 0xf0f5ff;
        if (bg) fill_rounded_rect(g, g->cx, g->cy, bw, bh,
                                  sn->border_radius > 0 ? sn->border_radius : 4, bg);

        /* expand/collapse arrow */
        if (has_children) {
            int ax = g->cx + px - 14;
            int ay = g->cy + bh / 2;
            XSetForeground(g->dpy, g->gc, alloc_color(g, selected ? 0xffffff : 0x888888));
            if (expanded) {
                XPoint tri[3] = { {(short)(ax), (short)(ay-3)},
                                  {(short)(ax+7), (short)(ay-3)},
                                  {(short)(ax+3), (short)(ay+3)} };
                XFillPolygon(g->dpy, g->backbuf, g->gc, tri, 3, Convex, CoordModeOrigin);
            } else {
                XPoint tri[3] = { {(short)(ax), (short)(ay-4)},
                                  {(short)(ax), (short)(ay+4)},
                                  {(short)(ax+6), (short)(ay)} };
                XFillPolygon(g->dpy, g->backbuf, g->gc, tri, 3, Convex, CoordModeOrigin);
            }
        }
        if (f)
            draw_text(g, f, label, g->cx + px, g->cy + py + f->ascent,
                      selected ? (sn->color ? sn->color : 0xffffff)
                               : (sn->color ? sn->color : 0x333333));
    }

    if (hov && g->mouse_released) {
        if (node_id) snprintf(g->tree_sel, sizeof(g->tree_sel), "%s", node_id);
        if (has_children && ci >= 0) g->coll_open[ci] = !g->coll_open[ci];
    }

    advance(g, bw, bh);
    return expanded;
}

void xgui_tree_end(XGui *g) {
    if (!g) return;
    g->cy += WIDGET_GAP / 2;
}

/* ------------------------------------------------------------------ context menu */

bool xgui_context_menu_begin(XGui *g, const char *id, int region_w, int region_h) {
    if (!g || !id) return false;

    int rx = g->cx, ry = g->cy;
    /* Check for right-click in region */
    bool right_click = false;
    /* We approximate right-click via Button3 — detected in the event loop.
       Since xgui doesn't track right-click natively yet, we use a simple heuristic:
       check if the context menu was already opened for this id, or detect via
       the mouse-released + no focus approach (simplified: track right button state). */

    /* If this context menu is already open, keep it open */
    bool already_open = (g->cmenu_open && strcmp(g->cmenu_id, id) == 0);
    (void)right_click;

    /* Right-click detection: look for mouse_released in region with middle y logic
       Since X11 right-click is Button3 and we track Button1, we use a Prism-level
       workaround: context_menu_begin returns true when already opened programmatically.
       For right-click to work properly: the user can call xgui_modal_open(g, id) on
       right-click via a surrounding right-button check. Here we just handle showing. */
    if (!already_open) {
        /* Check if right mouse button was pressed in the region — approximated by
           setting cmenu_open when g->mouse_released AND mouse is in region AND
           we detect some external signal. Since X11 right-click isn't yet tracked
           in xgui_begin, we'll auto-open on right-click by checking a special flag. */
        bool in_region = (g->mx >= rx && g->mx < rx + region_w &&
                          g->my >= ry && g->my < ry + region_h);
        /* We'll add right_click detection simply: if mouse is released over the region
           and ESC hasn't been pressed, and this id matches cmenu_id that was set
           externally — for now fall through so external code can call modal_open. */
        (void)in_region;
        return false;
    }

    /* Draw the popup */
    PssStyle *sm = &g->theme.context_menu;
    (void)CMENU_ITEM_H;
    /* We draw from cmenu_x, cmenu_y (set when opened) */
    int mx2 = g->cmenu_x, my2 = g->cmenu_y;
    int mw = 180;
    draw_shadow(g, mx2, my2, mw, 4, sm->border_radius > 0 ? sm->border_radius : 6);
    fill_rounded_rect(g, mx2, my2, mw, 4,
                      sm->border_radius > 0 ? sm->border_radius : 6,
                      sm->background ? sm->background : 0xffffff);
    draw_outline(g, mx2, my2, mw, 4,
                 sm->border_radius > 0 ? sm->border_radius : 6, 1,
                 sm->border_color ? sm->border_color : 0xdddddd);
    g->cmenu_item_y = my2 + (sm->padding_y > 0 ? sm->padding_y : 4);

    /* Close on Escape or click outside */
    bool outside = g->mouse_released &&
                   !(g->mx >= mx2 && g->mx < mx2 + mw &&
                     g->my >= my2 && g->my < my2 + 500);
    if (outside || g->key_esc_this_frame) {
        g->cmenu_open = false;
        g->cmenu_id[0] = '\0';
        return false;
    }
    return true;
}

bool xgui_context_menu_item(XGui *g, const char *label) {
    if (!g || !label) return false;
    if (!g->cmenu_open) return false;

    PssStyle *si  = &g->theme.context_menu_item;
    PssStyle *sih = &g->theme.context_menu_item_hover;
    int mw  = 180;
    int mx2 = g->cmenu_x;
    int px  = si->padding_x > 0 ? si->padding_x : 14;
    int py  = si->padding_y > 0 ? si->padding_y :  8;
    int fsz = si->font_size  > 0 ? si->font_size : 13;
    XftFont *f  = get_font(g, "sans", fsz, 400);
    int th = f ? f->ascent + f->descent : 16;
    int ih = th + 2 * py;

    int iy = g->cmenu_item_y;
    bool hov = (g->mx >= mx2 && g->mx < mx2 + mw &&
                g->my >= iy   && g->my < iy + ih);
    PssStyle *sd = hov ? sih : si;
    if (hov)
        fill_rounded_rect(g, mx2 + 4, iy, mw - 8, ih,
                          sd->border_radius > 0 ? sd->border_radius : 4,
                          sd->background ? sd->background : 0xf0f5ff);
    if (f)
        draw_text(g, f, label, mx2 + px, iy + py + f->ascent,
                  sd->color ? sd->color : (hov ? 0x0078d4 : 0x333333));

    g->cmenu_item_y += ih;

    if (hov && g->mouse_released) {
        g->cmenu_open = false;
        g->cmenu_id[0] = '\0';
        return true;
    }
    return false;
}

void xgui_context_menu_end(XGui *g) {
    (void)g; /* Nothing required — items were drawn inline */
}

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

/* ── new widget stubs (no-X11) ────────────────────────────────────── */
int    xgui_scroll_y(XGui *g)     { (void)g; return 0; }
int    xgui_scroll_delta(XGui *g) { (void)g; return 0; }
bool   xgui_key_pressed(XGui *g, char c) { (void)g;(void)c; return false; }
int    xgui_radio(XGui *g, const char *id, const char **ls, int n, int cur)
           { (void)g;(void)id;(void)ls;(void)n; return cur; }
bool   xgui_collapsing(XGui *g, const char *id, const char *l)  { (void)g;(void)id;(void)l; return false; }
void   xgui_collapsing_end(XGui *g)                             { (void)g; }
void   xgui_modal_open(XGui *g, const char *id)                 { (void)g;(void)id; }
void   xgui_modal_close(XGui *g)                                { (void)g; }
bool   xgui_modal_begin(XGui *g, const char *id, const char *t, int w, int h) { (void)g;(void)id;(void)t;(void)w;(void)h; return false; }
void   xgui_modal_end(XGui *g)                                  { (void)g; }
bool   xgui_modal_button(XGui *g, const char *l)                { (void)g;(void)l; return false; }
double xgui_spinbox(XGui *g, const char *id, double mn, double mx, double cur, double s)
           { (void)g;(void)id;(void)mn;(void)mx;(void)s; return cur; }
void   xgui_status_bar(XGui *g, const char *t)                  { (void)g;(void)t; }
void   xgui_table_begin(XGui *g, const char *id, int c)         { (void)g;(void)id;(void)c; }
void   xgui_table_header(XGui *g, const char **h, int n)        { (void)g;(void)h;(void)n; }
bool   xgui_table_row_begin(XGui *g, const char *rid)           { (void)g;(void)rid; return false; }
void   xgui_table_cell(XGui *g, const char *t)                  { (void)g;(void)t; }
void   xgui_table_row_end(XGui *g)                              { (void)g; }
void   xgui_table_end(XGui *g)                                  { (void)g; }
void   xgui_tree_begin(XGui *g, const char *id)                 { (void)g;(void)id; }
bool   xgui_tree_node(XGui *g, const char *nid, const char *l, bool ch, int d)
           { (void)g;(void)nid;(void)l;(void)ch;(void)d; return false; }
void   xgui_tree_end(XGui *g)                                   { (void)g; }
bool   xgui_context_menu_begin(XGui *g, const char *id, int rw, int rh)
           { (void)g;(void)id;(void)rw;(void)rh; return false; }
bool   xgui_context_menu_item(XGui *g, const char *l)           { (void)g;(void)l; return false; }
void   xgui_context_menu_end(XGui *g)                           { (void)g; }

#endif /* HAVE_X11 */
