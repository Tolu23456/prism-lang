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

/* ------------------------------------------------------------------ constants */

#define MAX_INPUTS      64
#define INPUT_BUF       1024
#define KEY_BUF         64
#define WIDGET_GAP      10
#define FONT_CACHE_MAX  24
#define BLINK_HALF      28    /* frames per cursor blink half-period */
#define SCROLL_STEP     80    /* pixels per wheel click */
#define SCROLL_LERP     0.16f /* scroll interpolation factor per frame */
#define SB_MARGIN       4     /* scrollbar edge margin px */
#define SHADOW_LAYERS   6     /* card shadow layers */

/* ------------------------------------------------------------------ types */

typedef struct { char id[64]; char buf[INPUT_BUF]; } InputState;

typedef struct { char key[164]; XftFont *font; } FCacheEntry;

struct XGui {
    Display   *dpy;
    int        screen;
    Window     win;
    Pixmap     backbuf;
    GC         gc;
    XftDraw   *xft;
    Picture    backbuf_pic;
    Atom       wm_delete;
    int        width, height;

    PssTheme   theme;

    /* Layout cursor */
    int        cx, cy;
    int        margin;

    /* Row layout */
    bool       in_row;
    int        row_max_h;

    /* Smooth scroll */
    int        scroll_target;   /* destination scroll offset */
    float      scroll_pos_f;    /* interpolated (fractional) scroll position */
    int        scroll_y;        /* integer scroll offset used this frame */
    int        content_h;       /* total content height last frame */

    /* Scrollbar drag */
    bool       sb_dragging;
    int        sb_drag_y0;      /* mouse Y when drag started */
    int        sb_drag_s0;      /* scroll_target when drag started */

    /* Mouse */
    int        mx, my;
    bool       mouse_down;
    bool       mouse_released;

    /* Keys */
    char       key_chars[KEY_BUF];
    int        key_count;
    bool       key_backspace;
    bool       key_enter;

    /* Input widgets */
    InputState inputs[MAX_INPUTS];
    int        input_count;
    char       focused_id[64];

    /* Font cache */
    FCacheEntry fcache[FONT_CACHE_MAX];
    int         fcache_n;

    /* Animations */
    int        frame;           /* frame counter, wraps */

    /* Tooltip (shown at end of frame) */
    char       tip_text[256];
    int        tip_x, tip_y;
    bool       tip_show;

    /* Card stack */
    int        card_x0, card_y0, card_w;
    bool       in_card;

    bool       running;
};

/* ------------------------------------------------------------------ color helpers */

static unsigned long alloc_x11_color(XGui *g, uint32_t rgb) {
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
    XftColorAllocValue(g->dpy,
        DefaultVisual(g->dpy, g->screen),
        DefaultColormap(g->dpy, g->screen), &rc, out);
}

/* ------------------------------------------------------------------ font cache */

static XftFont *open_font_raw(XGui *g, const char *family, int size, int weight) {
    XftFont *f = XftFontOpen(g->dpy, g->screen,
        XFT_FAMILY,    XftTypeString,  family,
        XFT_SIZE,      XftTypeDouble,  (double)size,
        XFT_WEIGHT,    XftTypeInteger, weight,
        XFT_ANTIALIAS, XftTypeBool,    True,
        NULL);
    if (!f) {
        f = XftFontOpen(g->dpy, g->screen,
            XFT_FAMILY,    XftTypeString,  "sans",
            XFT_SIZE,      XftTypeDouble,  (double)size,
            XFT_ANTIALIAS, XftTypeBool,    True,
            NULL);
    }
    return f;
}

static XftFont *get_font(XGui *g, const char *family, int size, int weight) {
    char key[164];
    snprintf(key, sizeof(key), "%s:%d:%d", family, size, weight);
    for (int i = 0; i < g->fcache_n; i++)
        if (strcmp(g->fcache[i].key, key) == 0)
            return g->fcache[i].font;
    XftFont *f = open_font_raw(g, family, size, weight);
    if (!f) return NULL;
    if (g->fcache_n < FONT_CACHE_MAX) {
        strncpy(g->fcache[g->fcache_n].key, key, 163);
        g->fcache[g->fcache_n].font = f;
        g->fcache_n++;
    } else {
        /* Evict oldest (index 0) */
        XftFontClose(g->dpy, g->fcache[0].font);
        memmove(&g->fcache[0], &g->fcache[1],
                sizeof(FCacheEntry) * (FONT_CACHE_MAX - 1));
        strncpy(g->fcache[FONT_CACHE_MAX - 1].key, key, 163);
        g->fcache[FONT_CACHE_MAX - 1].font = f;
    }
    return f;
}

static void text_size(XGui *g, XftFont *font, const char *text, int *w, int *h) {
    XGlyphInfo ext;
    XftTextExtentsUtf8(g->dpy, font,
                       (const FcChar8 *)text, (int)strlen(text), &ext);
    if (w) *w = ext.xOff;
    if (h) *h = font->ascent + font->descent;
}

static void draw_text(XGui *g, XftFont *font, const char *text,
                      int x, int y, uint32_t rgb) {
    XftColor c;
    make_xft_color(g, rgb, &c);
    XftDrawStringUtf8(g->xft, &c, font,
                      x, y, (const FcChar8 *)text, (int)strlen(text));
    XftColorFree(g->dpy, DefaultVisual(g->dpy, g->screen),
                 DefaultColormap(g->dpy, g->screen), &c);
}

/* ------------------------------------------------------------------ XRender helpers */

static void xr_make_color(uint32_t rgb, unsigned short alpha_16, XRenderColor *out) {
    out->red   = (unsigned short)(((rgb >> 16) & 0xff) * 0x101);
    out->green = (unsigned short)(((rgb >>  8) & 0xff) * 0x101);
    out->blue  = (unsigned short)(( rgb        & 0xff) * 0x101);
    out->alpha = alpha_16;
}

/* Upload a byte array as an A8 pixmap and create an XRender Picture for it.
   Caller must XRenderFreePicture and XFreePixmap when done. */
static bool upload_alpha_mask(XGui *g, int w, int h,
                               const unsigned char *data, int stride,
                               Pixmap *pix_out, Picture *pic_out) {
    XRenderPictFormat *a8 = XRenderFindStandardFormat(g->dpy, PictStandardA8);
    if (!a8) return false;

    Pixmap pix = XCreatePixmap(g->dpy, g->win, (unsigned)w, (unsigned)h, 8);
    GC gc8 = XCreateGC(g->dpy, pix, 0, NULL);

    XImage xi;
    memset(&xi, 0, sizeof(xi));
    xi.width            = w;
    xi.height           = h;
    xi.format           = ZPixmap;
    xi.data             = (char *)data;
    xi.byte_order       = LSBFirst;
    xi.bitmap_bit_order = LSBFirst;
    xi.bitmap_pad       = 8;
    xi.depth            = 8;
    xi.bytes_per_line   = stride;
    xi.bits_per_pixel   = 8;
    XInitImage(&xi);
    XPutImage(g->dpy, pix, gc8, &xi, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XFreeGC(g->dpy, gc8);

    XRenderPictureAttributes pa;
    memset(&pa, 0, sizeof(pa));
    *pic_out = XRenderCreatePicture(g->dpy, pix, a8, 0, &pa);
    *pix_out = pix;
    return true;
}

/* ------------------------------------------------------------------ drawing primitives */

static void fill_rect(XGui *g, int x, int y, int w, int h, uint32_t rgb) {
    XSetForeground(g->dpy, g->gc, alloc_x11_color(g, rgb));
    XFillRectangle(g->dpy, g->backbuf, g->gc, x, y, (unsigned)w, (unsigned)h);
}

/* Compute per-pixel alpha for an anti-aliased rounded rect.
   Returns 0..255 float. Pixel center is at (px+0.5, py+0.5). */
static float rrect_alpha(int px, int py, int w, int h, int r) {
    if (r <= 0) return (px >= 0 && py >= 0 && px < w && py < h) ? 255.0f : 0.0f;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    float cx = (float)px + 0.5f;
    float cy = (float)py + 0.5f;

    /* Determine if we are in a corner quadrant */
    float corner_ox = -1, corner_oy = -1;
    if      (cx < r      && cy < r)      { corner_ox = r;     corner_oy = r;     }
    else if (cx >= w - r && cy < r)      { corner_ox = w - r; corner_oy = r;     }
    else if (cx < r      && cy >= h - r) { corner_ox = r;     corner_oy = h - r; }
    else if (cx >= w - r && cy >= h - r) { corner_ox = w - r; corner_oy = h - r; }

    if (corner_ox >= 0) {
        float dx   = cx - corner_ox;
        float dy   = cy - corner_oy;
        float dist = sqrtf(dx * dx + dy * dy);
        float edge = (float)r - dist + 0.5f;
        if (edge <= 0) return 0;
        if (edge >= 1) return 255.0f;
        return edge * 255.0f;
    }
    /* Non-corner interior */
    return (cx >= 0 && cy >= 0 && cx < w && cy < h) ? 255.0f : 0.0f;
}

/* Anti-aliased filled rounded rect composited via XRender */
static void fill_rrect_aa(XGui *g, int x, int y, int w, int h,
                           int r, uint32_t rgb, unsigned short opacity_16) {
    if (w <= 0 || h <= 0 || !g->backbuf_pic) {
        fill_rect(g, x, y, w, h, rgb);
        return;
    }
    if (r <= 0 && opacity_16 == 0xffff) {
        /* Fast path: plain opaque rect */
        fill_rect(g, x, y, w, h, rgb);
        return;
    }

    int stride = (w + 3) & ~3;
    unsigned char *data = (unsigned char *)calloc((size_t)(stride * h), 1);
    if (!data) return;

    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            float a = rrect_alpha(px, py, w, h, r);
            if (a > 0)
                data[py * stride + px] = (unsigned char)(a + 0.5f);
        }

    Pixmap mask_pix; Picture mask_pic;
    if (!upload_alpha_mask(g, w, h, data, stride, &mask_pix, &mask_pic)) {
        free(data);
        fill_rect(g, x, y, w, h, rgb);
        return;
    }
    free(data);

    XRenderColor src_col;
    xr_make_color(rgb, opacity_16, &src_col);
    Picture src_pic = XRenderCreateSolidFill(g->dpy, &src_col);

    XRenderComposite(g->dpy, PictOpOver, src_pic, mask_pic, g->backbuf_pic,
                     0, 0, 0, 0, x, y, (unsigned)w, (unsigned)h);

    XRenderFreePicture(g->dpy, src_pic);
    XRenderFreePicture(g->dpy, mask_pic);
    XFreePixmap(g->dpy, mask_pix);
}

/* Convenience wrappers */
static void fill_rounded_rect(XGui *g, int x, int y, int w, int h,
                               int r, uint32_t rgb) {
    fill_rrect_aa(g, x, y, w, h, r, rgb, 0xffff);
}

static void fill_rounded_rect_alpha(XGui *g, int x, int y, int w, int h,
                                    int r, uint32_t rgb, int alpha_0_255) {
    unsigned short a16 = (unsigned short)(alpha_0_255 * 0x101);
    fill_rrect_aa(g, x, y, w, h, r, rgb, a16);
}

/* Anti-aliased rounded outline via XRender (outer - inner mask) */
static void draw_rounded_outline(XGui *g, int x, int y, int w, int h,
                                  int r, int bw, uint32_t rgb) {
    if (bw <= 0 || w <= 0 || h <= 0) return;
    if (!g->backbuf_pic) {
        XSetForeground(g->dpy, g->gc, alloc_x11_color(g, rgb));
        XSetLineAttributes(g->dpy, g->gc, (unsigned)bw,
                           LineSolid, CapRound, JoinRound);
        XDrawRectangle(g->dpy, g->backbuf, g->gc, x, y,
                       (unsigned)(w - 1), (unsigned)(h - 1));
        return;
    }

    int stride = (w + 3) & ~3;
    unsigned char *data = (unsigned char *)calloc((size_t)(stride * h), 1);
    if (!data) return;

    int ir      = (r > bw) ? r - bw : 0;
    int ix0     = bw, iy0 = bw;
    int iw      = w - 2 * bw, ih = h - 2 * bw;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            float outer = rrect_alpha(px, py, w, h, r);
            if (outer <= 0) continue;
            float inner = 0;
            if (iw > 0 && ih > 0)
                inner = rrect_alpha(px - ix0, py - iy0, iw, ih, ir);
            float border = outer * (255.0f - inner) / 255.0f;
            if (border > 0)
                data[py * stride + px] = (unsigned char)(border + 0.5f);
        }
    }

    Pixmap mask_pix; Picture mask_pic;
    if (upload_alpha_mask(g, w, h, data, stride, &mask_pix, &mask_pic)) {
        XRenderColor src_col;
        xr_make_color(rgb, 0xffff, &src_col);
        Picture src_pic = XRenderCreateSolidFill(g->dpy, &src_col);
        XRenderComposite(g->dpy, PictOpOver, src_pic, mask_pic, g->backbuf_pic,
                         0, 0, 0, 0, x, y, (unsigned)w, (unsigned)h);
        XRenderFreePicture(g->dpy, src_pic);
        XRenderFreePicture(g->dpy, mask_pic);
        XFreePixmap(g->dpy, mask_pix);
    }
    free(data);
}

/* Anti-aliased filled circle */
static void fill_circle_aa(XGui *g, int x, int y, int r, uint32_t rgb) {
    int d = 2 * r;
    if (d <= 0) return;
    if (!g->backbuf_pic) {
        XSetForeground(g->dpy, g->gc, alloc_x11_color(g, rgb));
        XFillArc(g->dpy, g->backbuf, g->gc, x, y, (unsigned)d, (unsigned)d, 0, 360*64);
        return;
    }
    int stride = (d + 3) & ~3;
    unsigned char *data = (unsigned char *)calloc((size_t)(stride * d), 1);
    if (!data) return;
    float fr = (float)r;
    for (int py = 0; py < d; py++)
        for (int px = 0; px < d; px++) {
            float dx   = (float)px - fr + 0.5f;
            float dy   = (float)py - fr + 0.5f;
            float dist = sqrtf(dx*dx + dy*dy);
            float edge = fr - dist + 0.5f;
            if (edge >= 1.0f)      data[py * stride + px] = 255;
            else if (edge > 0.0f)  data[py * stride + px] = (unsigned char)(edge * 255.0f + 0.5f);
        }
    Pixmap mp; Picture pp;
    if (upload_alpha_mask(g, d, d, data, stride, &mp, &pp)) {
        XRenderColor sc;
        xr_make_color(rgb, 0xffff, &sc);
        Picture sp = XRenderCreateSolidFill(g->dpy, &sc);
        XRenderComposite(g->dpy, PictOpOver, sp, pp, g->backbuf_pic,
                         0, 0, 0, 0, x, y, (unsigned)d, (unsigned)d);
        XRenderFreePicture(g->dpy, sp);
        XRenderFreePicture(g->dpy, pp);
        XFreePixmap(g->dpy, mp);
    }
    free(data);
}

/* Soft drop shadow using layered semi-transparent rounded rects */
static void draw_soft_shadow(XGui *g, int x, int y, int w, int h, int r) {
    typedef struct { int exp, dy, alpha; } Layer;
    static const Layer layers[SHADOW_LAYERS] = {
        { 10, 8, 10 }, { 7, 6, 15 }, { 5, 4, 20 },
        { 3, 3, 25 },  { 2, 2, 28 }, { 1, 1, 30 },
    };
    for (int i = 0; i < SHADOW_LAYERS; i++) {
        fill_rounded_rect_alpha(g,
            x - layers[i].exp,
            y + layers[i].dy - layers[i].exp,
            w + 2 * layers[i].exp,
            h + 2 * layers[i].exp,
            r + layers[i].exp,
            0x000000, layers[i].alpha);
    }
}

/* ------------------------------------------------------------------ input state */

static InputState *find_or_create_input(XGui *g, const char *id) {
    for (int i = 0; i < g->input_count; i++)
        if (strcmp(g->inputs[i].id, id) == 0)
            return &g->inputs[i];
    if (g->input_count >= MAX_INPUTS) return &g->inputs[0];
    InputState *s = &g->inputs[g->input_count++];
    snprintf(s->id, sizeof(s->id), "%s", id);
    s->buf[0] = '\0';
    return s;
}

/* ------------------------------------------------------------------ scrollbar geometry (shared) */

static void scrollbar_geo(XGui *g,
                           int *sb_x, int *sb_y, int *sb_h,
                           int *thumb_y, int *thumb_h) {
    int sw = g->theme.scrollbar.min_width;
    if (sw < 6) sw = 8;
    *sb_x = g->width - sw - SB_MARGIN;
    *sb_y = SB_MARGIN;
    *sb_h = g->height - 2 * SB_MARGIN;

    float ratio = (g->content_h > 0)
                  ? (float)g->height / (float)g->content_h : 1.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    *thumb_h = (int)(ratio * (float)(*sb_h));
    if (*thumb_h < 24) *thumb_h = 24;

    int max_scroll = g->content_h - g->height;
    float t = (max_scroll > 0)
              ? (float)g->scroll_y / (float)max_scroll : 0.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    *thumb_y = *sb_y + (int)(t * (float)(*sb_h - *thumb_h));
}

/* ------------------------------------------------------------------ backbuf picture */

static void create_backbuf_pic(XGui *g) {
    XRenderPictFormat *vf =
        XRenderFindVisualFormat(g->dpy, DefaultVisual(g->dpy, g->screen));
    if (!vf) { g->backbuf_pic = None; return; }
    XRenderPictureAttributes pa;
    memset(&pa, 0, sizeof(pa));
    g->backbuf_pic = XRenderCreatePicture(g->dpy, g->backbuf, vf, 0, &pa);
}

/* ------------------------------------------------------------------ lifecycle */

XGui *xgui_init(int width, int height, const char *title) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "[xgui] cannot open X11 display\n"); return NULL; }

    XGui *g = (XGui *)calloc(1, sizeof(XGui));
    g->dpy    = dpy;
    g->screen = DefaultScreen(dpy);
    g->width  = width;
    g->height = height;
    g->running = true;

    pss_theme_default(&g->theme);

    g->win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(dpy, g->screen),
        alloc_x11_color(g, g->theme.window.background));

    XStoreName(dpy, g->win, title);
    XSelectInput(dpy, g->win,
        ExposureMask | KeyPressMask |
        ButtonPressMask | ButtonReleaseMask |
        Button4Mask | Button5Mask |
        PointerMotionMask | StructureNotifyMask);

    g->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, g->win, &g->wm_delete, 1);
    g->gc = XCreateGC(dpy, g->win, 0, NULL);

    g->backbuf = XCreatePixmap(dpy, g->win,
        (unsigned)width, (unsigned)height,
        (unsigned)DefaultDepth(dpy, g->screen));

    g->xft = XftDrawCreate(dpy, g->backbuf,
        DefaultVisual(dpy, g->screen),
        DefaultColormap(dpy, g->screen));

    create_backbuf_pic(g);

    XMapWindow(dpy, g->win);
    XFlush(dpy);

    g->margin = g->theme.window.padding_y;
    return g;
}

void xgui_load_style(XGui *g, const char *path) {
    if (!g) return;
    pss_theme_load(&g->theme, path);
    g->margin = g->theme.window.padding_y;
    fill_rect(g, 0, 0, g->width, g->height, g->theme.window.background);
}

void xgui_set_dark(XGui *g, bool dark) {
    if (!g) return;
    if (dark) {
        g->theme.window.background         = 0x1e1e2e;
        g->theme.label.color               = 0xcdd6f4;
        g->theme.label.background          = 0x1e1e2e;
        g->theme.title.color               = 0xcdd6f4;
        g->theme.subtitle.color            = 0x89b4fa;
        g->theme.button.background         = 0x89b4fa;
        g->theme.button.color              = 0x1e1e2e;
        g->theme.button.border_color       = 0x89b4fa;
        g->theme.button_hover.background   = 0x74c7ec;
        g->theme.button_hover.color        = 0x1e1e2e;
        g->theme.button_active.background  = 0x5ba3cc;
        g->theme.input.background          = 0x313244;
        g->theme.input.color               = 0xcdd6f4;
        g->theme.input.border_color        = 0x45475a;
        g->theme.input_focus.background    = 0x313244;
        g->theme.input_focus.color         = 0xcdd6f4;
        g->theme.input_focus.border_color  = 0x89b4fa;
        g->theme.textarea.background       = 0x313244;
        g->theme.textarea.color            = 0xcdd6f4;
        g->theme.textarea.border_color     = 0x45475a;
        g->theme.textarea_focus.background = 0x313244;
        g->theme.textarea_focus.color      = 0xcdd6f4;
        g->theme.textarea_focus.border_color = 0x89b4fa;
        g->theme.scrollbar.background      = 0x313244;
        g->theme.scrollbar_thumb.background = 0x585b70;
        g->theme.scrollbar_thumb_hover.background = 0x6c7086;
        g->theme.separator.background      = 0x45475a;
        g->theme.card.background           = 0x313244;
        g->theme.card.color                = 0xcdd6f4;
    } else {
        pss_theme_default(&g->theme);
    }
    g->margin = g->theme.window.padding_y;
}

bool xgui_running(XGui *g) { return g && g->running; }

void xgui_close(XGui *g) { if (g) g->running = false; }

void xgui_destroy(XGui *g) {
    if (!g) return;
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

/* ------------------------------------------------------------------ frame */

void xgui_begin(XGui *g) {
    if (!g) return;

    g->mouse_released = false;
    g->key_count      = 0;
    g->key_backspace  = false;
    g->key_enter      = false;
    g->tip_show       = false;
    g->frame          = (g->frame + 1) & 0x7fffffff;

    /* Scrollbar geometry for hit-testing */
    int sb_x, sb_y, sb_h, thumb_y, thumb_h;
    bool sb_visible = (g->content_h > g->height);
    if (sb_visible) scrollbar_geo(g, &sb_x, &sb_y, &sb_h, &thumb_y, &thumb_h);

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
                int ex = e.xbutton.x, ey = e.xbutton.y;
                g->mouse_down = true;
                g->mx = ex; g->my = ey;
                /* Start scrollbar drag */
                if (sb_visible &&
                    ex >= sb_x && ex < g->width - SB_MARGIN &&
                    ey >= thumb_y && ey < thumb_y + thumb_h) {
                    g->sb_dragging = true;
                    g->sb_drag_y0  = ey;
                    g->sb_drag_s0  = g->scroll_target;
                }
            } else if (e.xbutton.button == Button4) {
                /* Scroll up */
                g->scroll_target -= SCROLL_STEP;
                if (g->scroll_target < 0) g->scroll_target = 0;
            } else if (e.xbutton.button == Button5) {
                /* Scroll down */
                int max_s = g->content_h - g->height;
                if (max_s < 0) max_s = 0;
                g->scroll_target += SCROLL_STEP;
                if (g->scroll_target > max_s) g->scroll_target = max_s;
            }
            break;

        case ButtonRelease:
            if (e.xbutton.button == Button1) {
                g->mouse_down     = false;
                g->mouse_released = true;
                g->sb_dragging    = false;
                g->mx = e.xbutton.x;
                g->my = e.xbutton.y;
            }
            break;

        case MotionNotify:
            g->mx = e.xmotion.x;
            g->my = e.xmotion.y;
            if (g->sb_dragging && sb_visible) {
                int delta       = g->my - g->sb_drag_y0;
                int track_range = sb_h - thumb_h;
                if (track_range > 0) {
                    int max_s = g->content_h - g->height;
                    if (max_s < 0) max_s = 0;
                    int new_s = g->sb_drag_s0 +
                                (int)((float)delta / (float)track_range * (float)max_s);
                    if (new_s < 0) new_s = 0;
                    if (new_s > max_s) new_s = max_s;
                    g->scroll_target = new_s;
                    /* Snap immediately while dragging */
                    g->scroll_pos_f = (float)new_s;
                    g->scroll_y     = new_s;
                }
            }
            break;

        case KeyPress: {
            char buf[16] = {0};
            KeySym ks = 0;
            XLookupString(&e.xkey, buf, sizeof(buf) - 1, &ks, NULL);
            if (ks == XK_BackSpace)          g->key_backspace = true;
            else if (ks == XK_Return ||
                     ks == XK_KP_Enter)      g->key_enter = true;
            else if (ks == XK_Escape)        g->focused_id[0] = '\0';
            else if (buf[0] >= 32 &&
                     g->key_count < KEY_BUF - 1)
                g->key_chars[g->key_count++] = buf[0];
            break;
        }

        case ConfigureNotify:
            if (e.xconfigure.width  != g->width ||
                e.xconfigure.height != g->height) {
                g->width  = e.xconfigure.width;
                g->height = e.xconfigure.height;
                if (g->backbuf_pic) {
                    XRenderFreePicture(g->dpy, g->backbuf_pic);
                    g->backbuf_pic = None;
                }
                XFreePixmap(g->dpy, g->backbuf);
                g->backbuf = XCreatePixmap(g->dpy, g->win,
                    (unsigned)g->width, (unsigned)g->height,
                    (unsigned)DefaultDepth(g->dpy, g->screen));
                XftDrawChange(g->xft, g->backbuf);
                create_backbuf_pic(g);
            }
            break;

        default: break;
        }
    }

    /* Apply key events to focused input */
    if (g->focused_id[0]) {
        InputState *inp = find_or_create_input(g, g->focused_id);
        int len = (int)strlen(inp->buf);
        if (g->key_backspace && len > 0) inp->buf[--len] = '\0';
        for (int i = 0; i < g->key_count; i++)
            if (len < INPUT_BUF - 2)
                inp->buf[len++] = g->key_chars[i];
        inp->buf[len] = '\0';
    }

    /* Smooth scroll interpolation */
    if (!g->sb_dragging) {
        float diff = (float)g->scroll_target - g->scroll_pos_f;
        if (fabsf(diff) < 0.5f)
            g->scroll_pos_f = (float)g->scroll_target;
        else
            g->scroll_pos_f += diff * SCROLL_LERP;
        g->scroll_y = (int)(g->scroll_pos_f + 0.5f);
    }

    /* Clear backbuffer */
    fill_rect(g, 0, 0, g->width, g->height, g->theme.window.background);

    /* Reset layout cursor */
    g->cx        = g->theme.window.padding_x;
    g->cy        = g->margin - g->scroll_y;
    g->in_row    = false;
    g->row_max_h = 0;
}

void xgui_end(XGui *g) {
    if (!g) return;

    /* Record total content height */
    g->content_h = g->cy + g->scroll_y + g->theme.window.padding_y;

    /* Clamp scroll target after content is known */
    int max_s = g->content_h - g->height;
    if (max_s < 0) max_s = 0;
    if (g->scroll_target > max_s) g->scroll_target = max_s;
    if (g->scroll_target < 0) g->scroll_target = 0;

    /* Draw scrollbar when content overflows */
    if (g->content_h > g->height) {
        int sb_x, sb_y, sb_h, thumb_y, thumb_h;
        scrollbar_geo(g, &sb_x, &sb_y, &sb_h, &thumb_y, &thumb_h);
        int sw = g->width - sb_x - SB_MARGIN;

        /* Track */
        uint32_t track_col = g->theme.scrollbar.background;
        if (track_col == 0) track_col = 0xeeeeee;
        fill_rounded_rect(g, sb_x, sb_y, sw, sb_h, sw / 2, track_col);

        /* Thumb — hover detection */
        bool thumb_hov = (g->mx >= sb_x && g->mx < g->width - SB_MARGIN &&
                          g->my >= thumb_y && g->my < thumb_y + thumb_h);
        uint32_t thumb_col = thumb_hov || g->sb_dragging
            ? g->theme.scrollbar_thumb_hover.background
            : g->theme.scrollbar_thumb.background;
        if (thumb_col == 0) thumb_col = thumb_hov ? 0x888888 : 0xbbbbbb;
        fill_rounded_rect(g, sb_x + 1, thumb_y + 1, sw - 2, thumb_h - 2,
                          (sw - 2) / 2, thumb_col);
    }

    /* Tooltip */
    if (g->tip_show && g->tip_text[0]) {
        PssStyle *ts = &g->theme.tooltip;
        int fsize = ts->font_size > 0 ? ts->font_size : 11;
        XftFont *tf = get_font(g, ts->font[0] ? ts->font : "sans", fsize, 400);
        if (tf) {
            int tw, th;
            text_size(g, tf, g->tip_text, &tw, &th);
            int px = ts->padding_x > 0 ? ts->padding_x : 6;
            int py = ts->padding_y > 0 ? ts->padding_y : 3;
            int bw = tw + 2 * px, bh = th + 2 * py;
            int tx = g->tip_x + 12, ty = g->tip_y - bh - 4;
            if (tx + bw > g->width)  tx = g->width  - bw - 4;
            if (ty < 4)              ty = g->tip_y  + 18;
            fill_rounded_rect(g, tx, ty, bw, bh,
                              ts->border_radius > 0 ? ts->border_radius : 4,
                              ts->background ? ts->background : 0x333333);
            draw_text(g, tf, g->tip_text, tx + px, ty + py + tf->ascent,
                      ts->color ? ts->color : 0xffffff);
        }
    }

    XCopyArea(g->dpy, g->backbuf, g->win, g->gc,
              0, 0, (unsigned)g->width, (unsigned)g->height, 0, 0);
    XFlush(g->dpy);
}

/* ------------------------------------------------------------------ viewport clip helper */

/* Returns true if the widget rect [y, y+h) is (partially) visible */
static bool widget_visible(XGui *g, int y, int h) {
    return (y + h > 0) && (y < g->height);
}

/* Advance layout cursor without drawing */
static void skip_widget(XGui *g, int bw, int bh) {
    if (g->in_row) {
        g->cx += bw + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }
}

/* ------------------------------------------------------------------ widgets */

void xgui_label(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.label;
    XftFont *font = get_font(g, s->font[0] ? s->font : "sans",
                             s->font_size > 0 ? s->font_size : 13,
                             s->font_weight > 0 ? s->font_weight : 400);
    if (!font) return;

    int tw, th; text_size(g, font, text, &tw, &th);
    int bh = th + 2 * s->padding_y;

    if (widget_visible(g, g->cy, bh)) {
        draw_text(g, font, text,
                  g->cx + s->padding_x,
                  g->cy + s->padding_y + font->ascent,
                  s->color ? s->color : 0x222222);
    }
    skip_widget(g, tw + 2 * s->padding_x, bh);
}

bool xgui_button(XGui *g, const char *text) {
    if (!g || !text) return false;
    PssStyle *s = &g->theme.button;
    XftFont *font = get_font(g, s->font[0] ? s->font : "sans",
                             s->font_size > 0 ? s->font_size : 13,
                             s->font_weight > 0 ? s->font_weight : 400);
    if (!font) return false;

    int tw, th; text_size(g, font, text, &tw, &th);
    int bw = tw + 2 * s->padding_x;
    int bh = th + 2 * s->padding_y;
    int x = g->cx, y = g->cy;

    bool hovered = (g->mx >= x && g->mx < x + bw &&
                    g->my >= y && g->my < y + bh);
    bool pressed  = hovered && g->mouse_down;
    bool clicked  = hovered && g->mouse_released;

    if (widget_visible(g, y, bh)) {
        PssStyle *ds = pressed  ? &g->theme.button_active
                     : hovered  ? &g->theme.button_hover
                     : s;

        int r = ds->border_radius > 0 ? ds->border_radius : 5;
        fill_rounded_rect(g, x, y + (pressed ? 1 : 0), bw, bh, r, ds->background);
        if (ds->border_width > 0)
            draw_rounded_outline(g, x, y + (pressed ? 1 : 0),
                                 bw, bh, r, ds->border_width, ds->border_color);

        /* Subtle inner highlight when not pressed */
        if (!pressed) {
            fill_rounded_rect_alpha(g, x + 1, y + 1, bw - 2, bh / 3, r - 1,
                                    0xffffff, 18);
        }

        int text_x = x + s->padding_x;
        if (s->text_align == 1) text_x = x + (bw - tw) / 2;
        draw_text(g, font, text, text_x,
                  y + (pressed ? 1 : 0) + s->padding_y + font->ascent,
                  ds->color ? ds->color : 0xffffff);
    }

    skip_widget(g, bw, bh);
    return clicked;
}

const char *xgui_input(XGui *g, const char *id, const char *placeholder) {
    if (!g || !id) return "";
    InputState *inp = find_or_create_input(g, id);
    bool focused = (strcmp(g->focused_id, id) == 0);
    PssStyle *s  = &g->theme.input;
    PssStyle *ds = focused ? &g->theme.input_focus : s;

    int usable = g->width - 2 * g->theme.window.padding_x;
    int bw = usable;

    XftFont *font = get_font(g, s->font[0] ? s->font : "sans",
                             s->font_size > 0 ? s->font_size : 13, 400);
    if (!font) return inp->buf;

    int _tw, th; text_size(g, font, "A", &_tw, &th);
    int bh = th + 2 * s->padding_y;
    int x = g->cx, y = g->cy;

    if (widget_visible(g, y, bh)) {
        /* Click to focus */
        if (g->mouse_released &&
            g->mx >= x && g->mx < x + bw &&
            g->my >= y && g->my < y + bh) {
            snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
            focused = true;
            ds = &g->theme.input_focus;
        }

        int r = ds->border_radius > 0 ? ds->border_radius : 4;
        fill_rounded_rect(g, x, y, bw, bh, r, ds->background ? ds->background : 0xffffff);
        draw_rounded_outline(g, x, y, bw, bh, r,
                             ds->border_width > 0 ? ds->border_width : 1,
                             ds->border_color ? ds->border_color : 0xbcbcbc);

        const char *display  = inp->buf[0] ? inp->buf : placeholder;
        uint32_t    text_col = inp->buf[0] ? (ds->color ? ds->color : 0x222222) : 0x999999;
        draw_text(g, font, display,
                  x + s->padding_x, y + s->padding_y + font->ascent, text_col);

        /* Blinking cursor */
        if (focused && ((g->frame / BLINK_HALF) % 2 == 0)) {
            int cw, _ch; text_size(g, font, inp->buf, &cw, &_ch);
            int cx2 = x + s->padding_x + cw + 1;
            int cy2 = y + s->padding_y + 2;
            fill_rounded_rect(g, cx2, cy2, 2, th - 4, 0,
                              ds->border_color ? ds->border_color : 0x0078d4);
        }
    } else {
        /* Still check click even if off-screen? No — skip */
    }

    skip_widget(g, bw, bh);
    return inp->buf;
}

void xgui_spacer(XGui *g, int h) {
    if (!g) return;
    if (g->in_row) g->cx += h;
    else           g->cy += h;
}

void xgui_row_begin(XGui *g) {
    if (!g) return;
    g->in_row    = true;
    g->row_max_h = 0;
}

void xgui_row_end(XGui *g) {
    if (!g) return;
    g->in_row = false;
    g->cx     = g->theme.window.padding_x;
    g->cy    += g->row_max_h + WIDGET_GAP;
    g->row_max_h = 0;
}

/* ------------------------------------------------------------------ new widgets */

void xgui_title(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.title;
    int fsz = s->font_size > 0 ? s->font_size : 24;
    int fw  = s->font_weight > 0 ? s->font_weight : 700;
    XftFont *font = get_font(g, s->font[0] ? s->font : "sans", fsz, fw);
    if (!font) return;
    int tw, th; text_size(g, font, text, &tw, &th);
    if (widget_visible(g, g->cy, th))
        draw_text(g, font, text, g->cx, g->cy + font->ascent,
                  s->color ? s->color : 0x111111);
    skip_widget(g, tw, th + (s->padding_y > 0 ? 2 * s->padding_y : 8));
}

void xgui_subtitle(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.subtitle;
    int fsz = s->font_size > 0 ? s->font_size : 16;
    int fw  = s->font_weight > 0 ? s->font_weight : 500;
    XftFont *font = get_font(g, s->font[0] ? s->font : "sans", fsz, fw);
    if (!font) return;
    int tw, th; text_size(g, font, text, &tw, &th);
    if (widget_visible(g, g->cy, th))
        draw_text(g, font, text, g->cx, g->cy + font->ascent,
                  s->color ? s->color : 0x555555);
    skip_widget(g, tw, th + (s->padding_y > 0 ? 2 * s->padding_y : 6));
}

void xgui_separator(XGui *g) {
    if (!g) return;
    int usable = g->width - 2 * g->margin;
    uint32_t col = g->theme.separator.background ? g->theme.separator.background : 0xdddddd;
    if (widget_visible(g, g->cy + 6, 2))
        fill_rounded_rect(g, g->cx, g->cy + 6, usable, 1, 0, col);
    g->cy += 14 + WIDGET_GAP;
}

bool xgui_checkbox(XGui *g, const char *id, const char *label) {
    if (!g || !id || !label) return false;
    InputState *inp = find_or_create_input(g, id);
    bool checked = (inp->buf[0] == '1');

    int box = 20;
    int x = g->cx, y = g->cy;

    bool hovered = (g->mx >= x && g->mx < x + box &&
                    g->my >= y && g->my < y + box);
    if (hovered && g->mouse_released) {
        checked     = !checked;
        inp->buf[0] = checked ? '1' : '0';
        inp->buf[1] = '\0';
    }

    XftFont *font = get_font(g, g->theme.label.font[0] ? g->theme.label.font : "sans",
                             g->theme.label.font_size > 0 ? g->theme.label.font_size : 13, 400);
    int tw = 0, th = 14;
    if (font) text_size(g, font, label, &tw, &th);
    int total_h = box > th ? box : th;

    if (widget_visible(g, y, total_h)) {
        uint32_t bg = checked ? (g->theme.checkbox_checked.background ? g->theme.checkbox_checked.background : 0x0078d4)
                              : (g->theme.checkbox.background ? g->theme.checkbox.background : 0xffffff);
        uint32_t bc = checked ? bg : (g->theme.checkbox.border_color ? g->theme.checkbox.border_color : 0xbcbcbc);
        if (hovered && !checked) bc = 0x0078d4;

        fill_rounded_rect(g, x, y + (total_h - box) / 2, box, box, 4, bg);
        draw_rounded_outline(g, x, y + (total_h - box) / 2, box, box, 4, 2, bc);

        if (checked) {
            /* Smooth AA checkmark using filled rounded rects */
            int cy2 = y + (total_h - box) / 2;
            XSetForeground(g->dpy, g->gc, alloc_x11_color(g, 0xffffff));
            XSetLineAttributes(g->dpy, g->gc, 2, LineSolid, CapRound, JoinRound);
            XDrawLine(g->dpy, g->backbuf, g->gc,
                      x + 4, cy2 + 10, x + 8, cy2 + 14);
            XDrawLine(g->dpy, g->backbuf, g->gc,
                      x + 8, cy2 + 14, x + 16, cy2 + 6);
        }

        if (font)
            draw_text(g, font, label,
                      x + box + 8, y + (total_h - th) / 2 + font->ascent,
                      g->theme.label.color ? g->theme.label.color : 0x222222);
    }

    skip_widget(g, box + 8 + tw, total_h);
    return checked;
}

void xgui_progress(XGui *g, int value, int max_val) {
    if (!g || max_val <= 0) return;
    int usable = g->width - 2 * g->margin;
    int bh = g->theme.progressbar.min_height > 0 ? g->theme.progressbar.min_height : 10;
    if (bh < 6) bh = 10;
    int x = g->cx, y = g->cy;
    int r = g->theme.progressbar.border_radius > 0 ? g->theme.progressbar.border_radius : bh / 2;

    if (widget_visible(g, y, bh)) {
        uint32_t track = g->theme.progressbar.background ? g->theme.progressbar.background : 0xe8e8e8;
        uint32_t fill  = g->theme.progressbar_fill.background ? g->theme.progressbar_fill.background : 0x0078d4;
        fill_rounded_rect(g, x, y, usable, bh, r, track);
        int filled = (int)((long long)value * usable / max_val);
        if (filled > usable) filled = usable;
        if (filled > 0) {
            fill_rounded_rect(g, x, y, filled, bh, r, fill);
            /* Shine */
            fill_rounded_rect_alpha(g, x + 1, y + 1, filled - 2, bh / 2 - 1, r - 1,
                                    0xffffff, 25);
        }
    }
    skip_widget(g, usable, bh);
}

float xgui_slider(XGui *g, const char *id, float min_v, float max_v, float current) {
    if (!g || !id) return current;
    InputState *inp = find_or_create_input(g, id);
    float val = (inp->buf[0] != '\0') ? (float)atof(inp->buf) : current;
    if (inp->buf[0] == '\0')
        snprintf(inp->buf, sizeof(inp->buf), "%f", current);

    int usable  = g->width - 2 * g->margin;
    int track_h = 6;
    int thumb_r = 10;
    int x = g->cx, y = g->cy;
    int track_y = y + thumb_r - track_h / 2;
    float range = max_v - min_v;
    float t     = (range > 0) ? (val - min_v) / range : 0.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    int thumb_x = x + (int)(t * (float)(usable - 2 * thumb_r)) + thumb_r;
    int thumb_y = y + thumb_r;

    bool hovered = (g->mx >= x && g->mx < x + usable &&
                    g->my >= y && g->my < y + 2 * thumb_r);
    if (hovered && g->mouse_down) {
        t = (float)(g->mx - x) / (float)(usable - 2 * thumb_r);
        if (t < 0) t = 0; if (t > 1) t = 1;
        val     = min_v + t * range;
        thumb_x = x + (int)(t * (float)(usable - 2 * thumb_r)) + thumb_r;
        snprintf(inp->buf, sizeof(inp->buf), "%f", val);
    }

    int total_h = 2 * thumb_r;
    if (widget_visible(g, y, total_h)) {
        uint32_t track_col = 0xe0e0e0;
        uint32_t fill_col  = g->theme.progressbar_fill.background ? g->theme.progressbar_fill.background : 0x0078d4;
        uint32_t thumb_col = hovered ? 0x0065b8 : fill_col;

        fill_rounded_rect(g, x, track_y, usable, track_h, track_h / 2, track_col);
        fill_rounded_rect(g, x, track_y, thumb_x - x, track_h, track_h / 2, fill_col);

        /* AA thumb circles */
        fill_circle_aa(g, thumb_x - thumb_r, thumb_y - thumb_r, thumb_r, thumb_col);
        fill_circle_aa(g, thumb_x - thumb_r + 3, thumb_y - thumb_r + 3, thumb_r - 3, 0xffffff);
    }

    skip_widget(g, usable, total_h);
    return val;
}

const char *xgui_textarea(XGui *g, const char *id, const char *placeholder) {
    if (!g || !id) return "";
    InputState *inp = find_or_create_input(g, id);
    bool focused = (strcmp(g->focused_id, id) == 0);
    PssStyle *s  = &g->theme.textarea;
    PssStyle *ds = focused ? &g->theme.textarea_focus : s;
    int usable = g->width - 2 * g->margin;
    int bh     = 90;
    int x = g->cx, y = g->cy;

    if (widget_visible(g, y, bh)) {
        if (g->mouse_released &&
            g->mx >= x && g->mx < x + usable &&
            g->my >= y && g->my < y + bh) {
            snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
            focused = true;
            ds = &g->theme.textarea_focus;
        }

        int r = ds->border_radius > 0 ? ds->border_radius : 4;
        fill_rounded_rect(g, x, y, usable, bh, r, ds->background ? ds->background : 0xffffff);
        draw_rounded_outline(g, x, y, usable, bh, r,
                             ds->border_width > 0 ? ds->border_width : 1,
                             ds->border_color ? ds->border_color : 0xbcbcbc);

        XftFont *font = get_font(g, s->font[0] ? s->font : "sans",
                                 s->font_size > 0 ? s->font_size : 13, 400);
        if (font) {
            const char *display  = inp->buf[0] ? inp->buf : placeholder;
            uint32_t    text_col = inp->buf[0] ? (ds->color ? ds->color : 0x222222) : 0x999999;
            /* Simple word-wrapped draw (single line for now) */
            draw_text(g, font, display,
                      x + (s->padding_x > 0 ? s->padding_x : 10),
                      y + (s->padding_y > 0 ? s->padding_y : 8) + font->ascent,
                      text_col);
            /* Cursor */
            if (focused && ((g->frame / BLINK_HALF) % 2 == 0)) {
                int cw, _ch; text_size(g, font, inp->buf, &cw, &_ch);
                int px2 = x + (s->padding_x > 0 ? s->padding_x : 10) + cw + 1;
                int py2 = y + (s->padding_y > 0 ? s->padding_y : 8) + 2;
                int fh  = font->ascent + font->descent;
                fill_rounded_rect(g, px2, py2, 2, fh - 4, 0,
                                  ds->border_color ? ds->border_color : 0x0078d4);
            }
        }
    }

    /* Key input */
    if (focused) {
        int len = (int)strlen(inp->buf);
        if (g->key_backspace && len > 0) inp->buf[--len] = '\0';
        if (g->key_enter && len < INPUT_BUF - 2) {
            inp->buf[len++] = '\n'; inp->buf[len] = '\0';
        }
        for (int i = 0; i < g->key_count && len < INPUT_BUF - 1; i++)
            inp->buf[len++] = g->key_chars[i];
        inp->buf[len] = '\0';
    }

    skip_widget(g, usable, bh);
    return inp->buf;
}

void xgui_badge(XGui *g, const char *text, uint32_t bg_color) {
    if (!g || !text) return;
    PssStyle *s  = &g->theme.badge;
    int fsz = s->font_size > 0 ? s->font_size : 11;
    XftFont *font = get_font(g, s->font[0] ? s->font : "sans", fsz, 400);
    if (!font) return;
    int tw, th; text_size(g, font, text, &tw, &th);
    int px = s->padding_x > 0 ? s->padding_x : 8;
    int py = s->padding_y > 0 ? s->padding_y : 3;
    int bw = tw + 2 * px, bh = th + 2 * py;
    int r  = bh / 2;
    int x = g->cx, y = g->cy;

    if (widget_visible(g, y, bh)) {
        fill_rounded_rect(g, x, y, bw, bh, r, bg_color);
        fill_rounded_rect_alpha(g, x + 1, y + 1, bw - 2, bh / 2, r - 1, 0xffffff, 20);
        draw_text(g, font, text, x + px, y + py + font->ascent, 0xffffff);
    }
    skip_widget(g, bw, bh);
}

/* ------------------------------------------------------------------ card widget */

void xgui_card_begin(XGui *g) {
    if (!g) return;
    g->card_x0 = g->cx - g->theme.card.padding_x;
    g->card_y0 = g->cy - g->theme.card.padding_y;
    g->card_w  = g->width - 2 * (g->theme.window.padding_x - g->theme.card.padding_x);
    if (g->card_w < 0) g->card_w = g->width - 2 * g->theme.window.padding_x;
    g->in_card = true;
    g->cy += g->theme.card.padding_y;
}

void xgui_card_end(XGui *g) {
    if (!g) return;
    int card_h = (g->cy - g->card_y0) + g->theme.card.padding_y;
    int r = g->theme.card.border_radius > 0 ? g->theme.card.border_radius : 8;

    if (widget_visible(g, g->card_y0, card_h)) {
        draw_soft_shadow(g, g->card_x0, g->card_y0, g->card_w, card_h, r);
        fill_rounded_rect(g, g->card_x0, g->card_y0, g->card_w, card_h, r,
                          g->theme.card.background ? g->theme.card.background : 0xffffff);
        if (g->theme.card.border_width > 0)
            draw_rounded_outline(g, g->card_x0, g->card_y0, g->card_w, card_h, r,
                                 g->theme.card.border_width, g->theme.card.border_color);
    }

    g->cy += g->theme.card.padding_y + WIDGET_GAP;
    g->in_card = false;
}

/* ------------------------------------------------------------------ tooltip */

void xgui_tooltip(XGui *g, const char *text) {
    if (!g || !text) return;
    /* Show tooltip if mouse has been hovering — caller sets this each frame */
    snprintf(g->tip_text, sizeof(g->tip_text), "%s", text);
    g->tip_x    = g->mx;
    g->tip_y    = g->my;
    g->tip_show = true;
}

/* ------------------------------------------------------------------ existing extras */

void xgui_set_dark_func(XGui *g, bool dark) { xgui_set_dark(g, dark); }

#else /* !HAVE_X11 */

#include "xgui.h"
#include <stdio.h>
#include <stdlib.h>

static void no_x11(void) { fprintf(stderr, "[xgui] Prism was compiled without X11 support\n"); }

XGui       *xgui_init(int w, int h, const char *t)        { (void)w;(void)h;(void)t; no_x11(); return NULL; }
void        xgui_load_style(XGui *g, const char *p)        { (void)g;(void)p; }
void        xgui_set_dark(XGui *g, bool d)                 { (void)g;(void)d; }
bool        xgui_running(XGui *g)                          { (void)g; return false; }
void        xgui_close(XGui *g)                            { (void)g; }
void        xgui_destroy(XGui *g)                          { (void)g; }
void        xgui_begin(XGui *g)                            { (void)g; }
void        xgui_end(XGui *g)                              { (void)g; }
void        xgui_label(XGui *g, const char *t)             { (void)g;(void)t; }
bool        xgui_button(XGui *g, const char *t)            { (void)g;(void)t; return false; }
const char *xgui_input(XGui *g, const char *i, const char *p) { (void)g;(void)i;(void)p; return ""; }
void        xgui_spacer(XGui *g, int h)                    { (void)g;(void)h; }
void        xgui_row_begin(XGui *g)                        { (void)g; }
void        xgui_row_end(XGui *g)                          { (void)g; }
void        xgui_title(XGui *g, const char *t)             { (void)g;(void)t; }
void        xgui_subtitle(XGui *g, const char *t)          { (void)g;(void)t; }
void        xgui_separator(XGui *g)                        { (void)g; }
bool        xgui_checkbox(XGui *g, const char *i, const char *l) { (void)g;(void)i;(void)l; return false; }
void        xgui_progress(XGui *g, int v, int m)           { (void)g;(void)v;(void)m; }
float       xgui_slider(XGui *g, const char *i, float mn, float mx, float c) { (void)g;(void)i;(void)mn;(void)mx; return c; }
const char *xgui_textarea(XGui *g, const char *i, const char *p) { (void)g;(void)i;(void)p; return ""; }
void        xgui_badge(XGui *g, const char *t, uint32_t b) { (void)g;(void)t;(void)b; }
void        xgui_card_begin(XGui *g)                       { (void)g; }
void        xgui_card_end(XGui *g)                         { (void)g; }
void        xgui_tooltip(XGui *g, const char *t)           { (void)g;(void)t; }

#endif
