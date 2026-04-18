#ifdef HAVE_X11

#include "xgui.h"
#include "pss.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ types */

#define MAX_INPUTS   32
#define INPUT_BUF   512
#define KEY_BUF      64
#define WIDGET_GAP    8   /* pixels between widgets */

typedef struct {
    char id[64];
    char buf[INPUT_BUF];
} InputState;

struct XGui {
    Display   *dpy;
    int        screen;
    Window     win;
    Pixmap     backbuf;   /* double-buffer pixmap */
    GC         gc;
    XftDraw   *xft;       /* Xft draw context for backbuf */
    Atom       wm_delete;
    int        width, height;

    PssTheme   theme;

    /* Layout cursor */
    int        cx, cy;        /* current x, y */
    int        margin;        /* left/top margin from theme.window.padding */

    /* Row layout */
    bool       in_row;
    int        row_max_h;

    /* Scroll state */
    int        scroll_y;      /* current vertical scroll offset in pixels */
    int        content_h;     /* total content height recorded last frame */

    /* Mouse state */
    int        mx, my;
    bool       mouse_down;
    bool       mouse_released; /* true for one frame only */

    /* Key events collected this frame */
    char       key_chars[KEY_BUF];
    int        key_count;
    bool       key_backspace;
    bool       key_enter;

    /* Text input state */
    InputState inputs[MAX_INPUTS];
    int        input_count;
    char       focused_id[64]; /* "" = none focused */

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
                       DefaultColormap(g->dpy, g->screen),
                       &rc, out);
}

/* ------------------------------------------------------------------ font helpers */

static XftFont *open_font(XGui *g, const char *family, int size) {
    XftFont *f = XftFontOpen(g->dpy, g->screen,
        XFT_FAMILY,  XftTypeString, family,
        XFT_SIZE,    XftTypeDouble, (double)size,
        XFT_ANTIALIAS, XftTypeBool, True,
        NULL);
    if (!f) {
        /* fallback */
        f = XftFontOpen(g->dpy, g->screen,
            XFT_FAMILY,  XftTypeString, "monospace",
            XFT_SIZE,    XftTypeDouble, (double)size,
            NULL);
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
                      x, y,
                      (const FcChar8 *)text, (int)strlen(text));
    XftColorFree(g->dpy,
                 DefaultVisual(g->dpy, g->screen),
                 DefaultColormap(g->dpy, g->screen), &c);
}

/* ------------------------------------------------------------------ drawing primitives */

static void fill_rect(XGui *g, int x, int y, int w, int h, uint32_t rgb) {
    XSetForeground(g->dpy, g->gc, alloc_x11_color(g, rgb));
    XFillRectangle(g->dpy, g->backbuf, g->gc, x, y, (unsigned)w, (unsigned)h);
}

/* Rounded filled rectangle */
static void fill_rounded_rect(XGui *g, int x, int y, int w, int h,
                               int r, uint32_t rgb) {
    if (r <= 0 || r * 2 > w || r * 2 > h) {
        fill_rect(g, x, y, w, h, rgb);
        return;
    }
    unsigned long px = alloc_x11_color(g, rgb);
    XSetForeground(g->dpy, g->gc, px);

    /* Center + side strips */
    XFillRectangle(g->dpy, g->backbuf, g->gc, x + r, y,      (unsigned)(w - 2*r), (unsigned)h);
    XFillRectangle(g->dpy, g->backbuf, g->gc, x,     y + r,  (unsigned)r, (unsigned)(h - 2*r));
    XFillRectangle(g->dpy, g->backbuf, g->gc, x+w-r, y + r,  (unsigned)r, (unsigned)(h - 2*r));

    /* Four corner arcs */
    int d = 2 * r;
    XFillArc(g->dpy, g->backbuf, g->gc, x,       y,       (unsigned)d, (unsigned)d,  90*64,  90*64);
    XFillArc(g->dpy, g->backbuf, g->gc, x+w-d,   y,       (unsigned)d, (unsigned)d,   0,     90*64);
    XFillArc(g->dpy, g->backbuf, g->gc, x,       y+h-d,   (unsigned)d, (unsigned)d, 180*64,  90*64);
    XFillArc(g->dpy, g->backbuf, g->gc, x+w-d,   y+h-d,   (unsigned)d, (unsigned)d, 270*64,  90*64);
}

/* Rounded outline rectangle */
static void draw_rounded_outline(XGui *g, int x, int y, int w, int h,
                                  int r, int lw, uint32_t rgb) {
    if (lw <= 0) return;
    XSetForeground(g->dpy, g->gc, alloc_x11_color(g, rgb));
    XSetLineAttributes(g->dpy, g->gc, (unsigned)lw,
                       LineSolid, CapRound, JoinRound);
    if (r <= 0 || r * 2 > w || r * 2 > h) {
        int half = lw / 2;
        XDrawRectangle(g->dpy, g->backbuf, g->gc,
                       x + half, y + half,
                       (unsigned)(w - lw), (unsigned)(h - lw));
        return;
    }
    int d = 2 * r;
    /* Four corner arcs */
    XDrawArc(g->dpy, g->backbuf, g->gc, x,       y,       (unsigned)d, (unsigned)d,  90*64,  90*64);
    XDrawArc(g->dpy, g->backbuf, g->gc, x+w-d,   y,       (unsigned)d, (unsigned)d,   0,     90*64);
    XDrawArc(g->dpy, g->backbuf, g->gc, x,       y+h-d,   (unsigned)d, (unsigned)d, 180*64,  90*64);
    XDrawArc(g->dpy, g->backbuf, g->gc, x+w-d,   y+h-d,   (unsigned)d, (unsigned)d, 270*64,  90*64);
    /* Four connecting lines */
    XDrawLine(g->dpy, g->backbuf, g->gc, x+r,   y,     x+w-r, y);       /* top */
    XDrawLine(g->dpy, g->backbuf, g->gc, x+r,   y+h-1, x+w-r, y+h-1);   /* bottom */
    XDrawLine(g->dpy, g->backbuf, g->gc, x,     y+r,   x,     y+h-r);   /* left */
    XDrawLine(g->dpy, g->backbuf, g->gc, x+w-1, y+r,   x+w-1, y+h-r);   /* right */
}

/* ------------------------------------------------------------------ input state helpers */

static InputState *find_or_create_input(XGui *g, const char *id) {
    for (int i = 0; i < g->input_count; i++) {
        if (strcmp(g->inputs[i].id, id) == 0)
            return &g->inputs[i];
    }
    if (g->input_count >= MAX_INPUTS) return &g->inputs[0];
    InputState *s = &g->inputs[g->input_count++];
    snprintf(s->id, sizeof(s->id), "%s", id);
    s->buf[0] = '\0';
    return s;
}

/* ------------------------------------------------------------------ lifecycle */

XGui *xgui_init(int width, int height, const char *title) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "[xgui] cannot open X11 display\n");
        return NULL;
    }

    XGui *g = calloc(1, sizeof(XGui));
    g->dpy    = dpy;
    g->screen = DefaultScreen(dpy);
    g->width  = width;
    g->height = height;
    g->running = true;

    pss_theme_default(&g->theme);

    /* Create window */
    g->win = XCreateSimpleWindow(
        dpy, DefaultRootWindow(dpy),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(dpy, g->screen),
        alloc_x11_color(g, g->theme.window.background));

    XStoreName(dpy, g->win, title);

    /* Subscribe to events */
    XSelectInput(dpy, g->win,
        ExposureMask | KeyPressMask |
        ButtonPressMask | ButtonReleaseMask |
        Button4Mask | Button5Mask |
        PointerMotionMask | StructureNotifyMask);

    /* Handle window close */
    g->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, g->win, &g->wm_delete, 1);

    /* GC */
    g->gc = XCreateGC(dpy, g->win, 0, NULL);

    /* Backing pixmap */
    g->backbuf = XCreatePixmap(dpy, g->win,
        (unsigned)width, (unsigned)height,
        (unsigned)DefaultDepth(dpy, g->screen));

    /* Xft draw context on the pixmap */
    g->xft = XftDrawCreate(dpy, g->backbuf,
        DefaultVisual(dpy, g->screen),
        DefaultColormap(dpy, g->screen));

    XMapWindow(dpy, g->win);
    XFlush(dpy);

    g->margin = g->theme.window.padding_y;

    return g;
}

void xgui_load_style(XGui *g, const char *path) {
    if (!g) return;
    pss_theme_load(&g->theme, path);
    g->margin = g->theme.window.padding_y;
    /* Rebuild background */
    fill_rect(g, 0, 0, g->width, g->height, g->theme.window.background);
}

void xgui_set_dark(XGui *g, bool dark) {
    if (!g) return;
    if (dark) {
        g->theme.window.background       = 0x1e1e2e;
        g->theme.label.color             = 0xcdd6f4;
        g->theme.label.background        = 0x1e1e2e;
        g->theme.button.background       = 0x313244;
        g->theme.button.color            = 0xcdd6f4;
        g->theme.button.border_color     = 0x45475a;
        g->theme.button_hover.background = 0x45475a;
        g->theme.button_hover.color      = 0xcdd6f4;
        g->theme.button_hover.border_color = 0x89b4fa;
        g->theme.input.background        = 0x313244;
        g->theme.input.color             = 0xcdd6f4;
        g->theme.input.border_color      = 0x45475a;
        g->theme.input_focus.background  = 0x313244;
        g->theme.input_focus.color       = 0xcdd6f4;
        g->theme.input_focus.border_color = 0x89b4fa;
        g->theme.textarea.background     = 0x313244;
        g->theme.textarea.color          = 0xcdd6f4;
        g->theme.textarea.border_color   = 0x45475a;
        g->theme.textarea_focus.background = 0x313244;
        g->theme.textarea_focus.color    = 0xcdd6f4;
        g->theme.textarea_focus.border_color = 0x89b4fa;
    } else {
        /* Restore light defaults */
        pss_theme_default(&g->theme);
    }
    g->margin = g->theme.window.padding_y;
}

bool xgui_running(XGui *g) {
    return g && g->running;
}

void xgui_destroy(XGui *g) {
    if (!g) return;
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

    /* Reset per-frame state */
    g->mouse_released = false;
    g->key_count      = 0;
    g->key_backspace  = false;
    g->key_enter      = false;

    /* Process all pending X events */
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
                g->mx = e.xbutton.x;
                g->my = e.xbutton.y;
            } else if (e.xbutton.button == Button4) { /* scroll up */
                g->scroll_y -= 30;
                if (g->scroll_y < 0) g->scroll_y = 0;
            } else if (e.xbutton.button == Button5) { /* scroll down */
                int max_scroll = g->content_h - g->height;
                if (max_scroll < 0) max_scroll = 0;
                g->scroll_y += 30;
                if (g->scroll_y > max_scroll) g->scroll_y = max_scroll;
            }
            break;

        case ButtonRelease:
            if (e.xbutton.button == Button1) {
                g->mouse_down     = false;
                g->mouse_released = true;
                g->mx = e.xbutton.x;
                g->my = e.xbutton.y;
            }
            break;

        case MotionNotify:
            g->mx = e.xmotion.x;
            g->my = e.xmotion.y;
            break;

        case KeyPress: {
            char buf[16] = {0};
            KeySym ks = 0;
            XLookupString(&e.xkey, buf, sizeof(buf)-1, &ks, NULL);
            if (ks == XK_BackSpace) {
                g->key_backspace = true;
            } else if (ks == XK_Return || ks == XK_KP_Enter) {
                g->key_enter = true;
            } else if (ks == XK_Escape) {
                g->focused_id[0] = '\0';
            } else if (buf[0] >= 32 && g->key_count < KEY_BUF - 1) {
                g->key_chars[g->key_count++] = buf[0];
            }
            break;
        }

        case ConfigureNotify:
            if (e.xconfigure.width  != g->width ||
                e.xconfigure.height != g->height) {
                g->width  = e.xconfigure.width;
                g->height = e.xconfigure.height;
                XFreePixmap(g->dpy, g->backbuf);
                g->backbuf = XCreatePixmap(g->dpy, g->win,
                    (unsigned)g->width, (unsigned)g->height,
                    (unsigned)DefaultDepth(g->dpy, g->screen));
                XftDrawChange(g->xft, g->backbuf);
            }
            break;

        default: break;
        }
    }

    /* Apply key events to focused input */
    if (g->focused_id[0]) {
        InputState *inp = find_or_create_input(g, g->focused_id);
        int len = (int)strlen(inp->buf);
        if (g->key_backspace && len > 0)
            inp->buf[len - 1] = '\0';
        for (int i = 0; i < g->key_count; i++) {
            if (len < INPUT_BUF - 2) {
                inp->buf[len++] = g->key_chars[i];
                inp->buf[len]   = '\0';
            }
        }
    }

    /* Clear backing buffer */
    fill_rect(g, 0, 0, g->width, g->height, g->theme.window.background);

    /* Reset layout cursor — offset by current scroll position */
    g->cx     = g->theme.window.padding_x;
    g->cy     = g->margin - g->scroll_y;
    g->in_row = false;
    g->row_max_h = 0;
}

void xgui_end(XGui *g) {
    if (!g) return;
    /* Record total content height for scroll clamping next frame */
    g->content_h = g->cy + g->scroll_y + g->theme.window.padding_y;
    XCopyArea(g->dpy, g->backbuf, g->win, g->gc,
              0, 0, (unsigned)g->width, (unsigned)g->height, 0, 0);
    XFlush(g->dpy);
}

/* ------------------------------------------------------------------ widgets */

void xgui_label(XGui *g, const char *text) {
    if (!g || !text) return;
    PssStyle *s = &g->theme.label;

    XftFont *font = open_font(g, s->font, s->font_size);
    if (!font) return;

    int tw, th;
    text_size(g, font, text, &tw, &th);

    int bh = th + 2 * s->padding_y;

    draw_text(g, font, text,
              g->cx + s->padding_x,
              g->cy + s->padding_y + font->ascent,
              s->color);

    if (g->in_row) {
        g->cx += tw + 2 * s->padding_x + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }

    XftFontClose(g->dpy, font);
}

bool xgui_button(XGui *g, const char *text) {
    if (!g || !text) return false;

    bool hovered = false;
    bool clicked = false;

    /* Determine hover before drawing */
    /* We'll compute geometry first, then check */
    PssStyle *s = &g->theme.button;

    XftFont *font = open_font(g, s->font, s->font_size);
    if (!font) return false;

    int tw, th;
    text_size(g, font, text, &tw, &th);

    int bw = tw + 2 * s->padding_x;
    int bh = th + 2 * s->padding_y;

    int x = g->cx;
    int y = g->cy;

    hovered = (g->mx >= x && g->mx < x + bw &&
               g->my >= y && g->my < y + bh);
    clicked  = hovered && g->mouse_released;

    PssStyle *ds = hovered ? &g->theme.button_hover : s;

    fill_rounded_rect(g, x, y, bw, bh, ds->border_radius, ds->background);
    if (ds->border_width > 0)
        draw_rounded_outline(g, x, y, bw, bh,
                             ds->border_radius, ds->border_width, ds->border_color);

    draw_text(g, font,  text,
              x + s->padding_x,
              y + s->padding_y + font->ascent,
              ds->color);

    if (g->in_row) {
        g->cx += bw + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }

    XftFontClose(g->dpy, font);
    return clicked;
}

const char *xgui_input(XGui *g, const char *id, const char *placeholder) {
    if (!g || !id) return "";

    PssStyle *s  = &g->theme.input;
    bool focused = (strcmp(g->focused_id, id) == 0);
    PssStyle *ds = focused ? &g->theme.input_focus : s;

    InputState *inp = find_or_create_input(g, id);

    XftFont *font = open_font(g, s->font, s->font_size);
    if (!font) return inp->buf;

    /* Fixed width: full usable width of window */
    int usable = g->width - 2 * g->theme.window.padding_x;
    int bw = usable;

    int _tw, th;
    text_size(g, font, "A", &_tw, &th);
    int bh = th + 2 * s->padding_y;

    int x = g->cx;
    int y = g->cy;

    /* Click to focus */
    if (g->mouse_released &&
        g->mx >= x && g->mx < x + bw &&
        g->my >= y && g->my < y + bh) {
        snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
        focused = true;
        ds = &g->theme.input_focus;
    }

    /* Draw box */
    fill_rounded_rect(g, x, y, bw, bh, ds->border_radius, ds->background);
    draw_rounded_outline(g, x, y, bw, bh,
                         ds->border_radius, ds->border_width, ds->border_color);

    /* Draw text or placeholder */
    const char *display = inp->buf[0] ? inp->buf : placeholder;
    uint32_t    text_col = inp->buf[0] ? s->color : 0x999999;
    draw_text(g, font, display,
              x + s->padding_x,
              y + s->padding_y + font->ascent,
              text_col);

    /* Draw cursor when focused */
    if (focused) {
        int cw, _ch;
        text_size(g, font, inp->buf, &cw, &_ch);
        int cx2 = x + s->padding_x + cw + 1;
        int cy2 = y + s->padding_y + 2;
        fill_rect(g, cx2, cy2, 2, th - 2, ds->border_color);
    }

    if (g->in_row) {
        g->cx += bw + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }

    XftFontClose(g->dpy, font);
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
    XftFont *font = open_font(g, g->theme.label.font, g->theme.label.font_size + 8);
    if (!font) return;
    int tw, th;
    text_size(g, font, text, &tw, &th);
    draw_text(g, font, text, g->cx, g->cy + font->ascent, g->theme.label.color);
    if (g->in_row) {
        g->cx += tw + WIDGET_GAP;
        if (th > g->row_max_h) g->row_max_h = th;
    } else {
        g->cy += th + WIDGET_GAP;
    }
    XftFontClose(g->dpy, font);
}

void xgui_subtitle(XGui *g, const char *text) {
    if (!g || !text) return;
    XftFont *font = open_font(g, g->theme.label.font, g->theme.label.font_size + 3);
    if (!font) return;
    int tw, th;
    text_size(g, font, text, &tw, &th);
    draw_text(g, font, text, g->cx, g->cy + font->ascent, 0x666666);
    if (g->in_row) {
        g->cx += tw + WIDGET_GAP;
        if (th > g->row_max_h) g->row_max_h = th;
    } else {
        g->cy += th + WIDGET_GAP;
    }
    XftFontClose(g->dpy, font);
}

void xgui_separator(XGui *g) {
    if (!g) return;
    int usable = g->width - 2 * g->margin;
    fill_rect(g, g->cx, g->cy + 4, usable, 1, 0xdddddd);
    g->cy += 10 + WIDGET_GAP;
}

bool xgui_checkbox(XGui *g, const char *id, const char *label) {
    if (!g || !id || !label) return false;
    InputState *inp = find_or_create_input(g, id);
    /* state: buf[0]='1' means checked */
    bool checked = (inp->buf[0] == '1');

    int box = 18;
    int x = g->cx, y = g->cy;

    bool hovered = (g->mx >= x && g->mx < x + box &&
                    g->my >= y && g->my < y + box);
    if (hovered && g->mouse_released) {
        checked     = !checked;
        inp->buf[0] = checked ? '1' : '0';
        inp->buf[1] = '\0';
    }

    fill_rounded_rect(g, x, y, box, box, 3, checked ? 0x4a90e2 : 0xffffff);
    draw_rounded_outline(g, x, y, box, box, 3, 1, 0xaaaaaa);
    if (checked) {
        /* draw a simple check mark */
        XSetForeground(g->dpy, g->gc, alloc_x11_color(g, 0xffffff));
        XDrawLine(g->dpy, g->backbuf, g->gc, x+3, y+9, x+7, y+13);
        XDrawLine(g->dpy, g->backbuf, g->gc, x+7, y+13, x+15, y+5);
    }

    XftFont *font = open_font(g, g->theme.label.font, g->theme.label.font_size);
    if (font) {
        int tw, th; text_size(g, font, label, &tw, &th);
        draw_text(g, font, label, x + box + 6, y + font->ascent, g->theme.label.color);
        int total_h = box > th ? box : th;
        if (g->in_row) {
            g->cx += box + 6 + tw + WIDGET_GAP;
            if (total_h > g->row_max_h) g->row_max_h = total_h;
        } else {
            g->cy += total_h + WIDGET_GAP;
        }
        XftFontClose(g->dpy, font);
    } else {
        if (g->in_row) g->cx += box + WIDGET_GAP;
        else           g->cy += box + WIDGET_GAP;
    }
    return checked;
}

void xgui_progress(XGui *g, int value, int max_val) {
    if (!g || max_val <= 0) return;
    int usable = g->width - 2 * g->margin;
    int bh = 16;
    int x = g->cx, y = g->cy;

    fill_rounded_rect(g, x, y, usable, bh, 4, 0xe0e0e0);

    int filled = (int)((long long)value * usable / max_val);
    if (filled > usable) filled = usable;
    if (filled > 0)
        fill_rounded_rect(g, x, y, filled, bh, 4, 0x4a90e2);

    if (g->in_row) {
        g->cx += usable + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }
}

float xgui_slider(XGui *g, const char *id, float min_v, float max_v, float current) {
    if (!g || !id) return current;
    InputState *inp = find_or_create_input(g, id);

    float val = current;
    if (inp->buf[0] != '\0') {
        val = (float)atof(inp->buf);
    } else {
        snprintf(inp->buf, sizeof(inp->buf), "%f", current);
    }

    int usable = g->width - 2 * g->margin;
    int track_h = 6;
    int thumb_r = 9;
    int x = g->cx, y = g->cy;
    int track_y = y + thumb_r - track_h / 2;

    fill_rounded_rect(g, x, track_y, usable, track_h, 3, 0xe0e0e0);

    float range = max_v - min_v;
    float t = (range > 0) ? (val - min_v) / range : 0.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    int thumb_x = x + (int)(t * (usable - 2 * thumb_r)) + thumb_r;
    int thumb_y = y + thumb_r;

    /* drag */
    bool hovered = (g->mx >= x && g->mx < x + usable &&
                    g->my >= y && g->my < y + 2 * thumb_r);
    if (hovered && g->mouse_down) {
        t = (float)(g->mx - x) / (float)(usable - 2 * thumb_r);
        if (t < 0) t = 0; if (t > 1) t = 1;
        val = min_v + t * range;
        thumb_x = x + (int)(t * (usable - 2 * thumb_r)) + thumb_r;
        snprintf(inp->buf, sizeof(inp->buf), "%f", val);
    }

    fill_rounded_rect(g, x, track_y, thumb_x - x, track_h, 3, 0x4a90e2);

    XSetForeground(g->dpy, g->gc, alloc_x11_color(g, hovered ? 0x2970c2 : 0x4a90e2));
    XFillArc(g->dpy, g->backbuf, g->gc,
             thumb_x - thumb_r, thumb_y - thumb_r,
             2 * thumb_r, 2 * thumb_r, 0, 360 * 64);
    XSetForeground(g->dpy, g->gc, alloc_x11_color(g, 0xffffff));
    XFillArc(g->dpy, g->backbuf, g->gc,
             thumb_x - thumb_r + 3, thumb_y - thumb_r + 3,
             2 * (thumb_r - 3), 2 * (thumb_r - 3), 0, 360 * 64);

    int total_h = 2 * thumb_r;
    if (g->in_row) {
        g->cx += usable + WIDGET_GAP;
        if (total_h > g->row_max_h) g->row_max_h = total_h;
    } else {
        g->cy += total_h + WIDGET_GAP;
    }
    return val;
}

const char *xgui_textarea(XGui *g, const char *id, const char *placeholder) {
    if (!g || !id) return "";
    InputState *inp = find_or_create_input(g, id);

    bool focused = (strcmp(g->focused_id, id) == 0);
    PssStyle *s  = focused ? &g->theme.input_focus : &g->theme.input;
    int usable   = g->width - 2 * g->margin;
    int bh       = 80;
    int x = g->cx, y = g->cy;

    if (g->mouse_released &&
        g->mx >= x && g->mx < x + usable &&
        g->my >= y && g->my < y + bh) {
        snprintf(g->focused_id, sizeof(g->focused_id), "%s", id);
        focused = true;
        s = &g->theme.input_focus;
    }

    fill_rounded_rect(g, x, y, usable, bh, s->border_radius, s->background);
    draw_rounded_outline(g, x, y, usable, bh, s->border_radius, s->border_width, s->border_color);

    XftFont *font = open_font(g, s->font, s->font_size);
    if (font) {
        const char *display = inp->buf[0] ? inp->buf : placeholder;
        uint32_t    text_col = inp->buf[0] ? s->color : 0x999999;
        draw_text(g, font, display, x + s->padding_x, y + s->padding_y + font->ascent, text_col);
        XftFontClose(g->dpy, font);
    }

    /* Handle key input same as regular input when focused */
    if (focused) {
        int len = (int)strlen(inp->buf);
        if (g->key_backspace && len > 0) inp->buf[--len] = '\0';
        if (g->key_enter && len < INPUT_BUF - 2) {
            inp->buf[len]   = '\n';
            inp->buf[len+1] = '\0';
        }
        for (int i = 0; i < g->key_count && len < INPUT_BUF - 1; i++) {
            inp->buf[len++] = g->key_chars[i];
            inp->buf[len]   = '\0';
        }
    }

    if (g->in_row) {
        g->cx += usable + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }
    return inp->buf;
}

void xgui_badge(XGui *g, const char *text, uint32_t bg_color) {
    if (!g || !text) return;
    XftFont *font = open_font(g, g->theme.label.font, g->theme.label.font_size - 2);
    if (!font) return;
    int tw, th;
    text_size(g, font, text, &tw, &th);
    int pad_x = 8, pad_y = 3;
    int bw = tw + 2 * pad_x;
    int bh = th + 2 * pad_y;
    int x = g->cx, y = g->cy;

    fill_rounded_rect(g, x, y, bw, bh, bh / 2, bg_color);
    draw_text(g, font, text, x + pad_x, y + pad_y + font->ascent, 0xffffff);

    if (g->in_row) {
        g->cx += bw + WIDGET_GAP;
        if (bh > g->row_max_h) g->row_max_h = bh;
    } else {
        g->cy += bh + WIDGET_GAP;
    }
    XftFontClose(g->dpy, font);
}

#else /* !HAVE_X11 */

#include "xgui.h"
#include <stdio.h>
#include <stdlib.h>

static void no_x11(void) {
    fprintf(stderr, "[xgui] Prism was compiled without X11 support\n");
}

XGui       *xgui_init(int w, int h, const char *t)    { (void)w;(void)h;(void)t; no_x11(); return NULL; }
void        xgui_load_style(XGui *g, const char *p)   { (void)g;(void)p; }
bool        xgui_running(XGui *g)                      { (void)g; return false; }
void        xgui_destroy(XGui *g)                      { (void)g; }
void        xgui_begin(XGui *g)                        { (void)g; }
void        xgui_end(XGui *g)                          { (void)g; }
void        xgui_label(XGui *g, const char *t)         { (void)g;(void)t; }
bool        xgui_button(XGui *g, const char *t)        { (void)g;(void)t; return false; }
const char *xgui_input(XGui *g, const char *i,
                        const char *p)                  { (void)g;(void)i;(void)p; return ""; }
void        xgui_spacer(XGui *g, int h)                { (void)g;(void)h; }
void        xgui_row_begin(XGui *g)                    { (void)g; }
void        xgui_row_end(XGui *g)                      { (void)g; }
void        xgui_title(XGui *g, const char *t)         { (void)g;(void)t; }
void        xgui_subtitle(XGui *g, const char *t)      { (void)g;(void)t; }
void        xgui_separator(XGui *g)                    { (void)g; }
bool        xgui_checkbox(XGui *g, const char *i,
                           const char *l)               { (void)g;(void)i;(void)l; return false; }
void        xgui_progress(XGui *g, int v, int m)       { (void)g;(void)v;(void)m; }
float       xgui_slider(XGui *g, const char *i,
                         float mn, float mx, float c)   { (void)g;(void)i;(void)mn;(void)mx; return c; }
const char *xgui_textarea(XGui *g, const char *i,
                            const char *p)              { (void)g;(void)i;(void)p; return ""; }
void        xgui_badge(XGui *g, const char *t,
                        uint32_t bg)                    { (void)g;(void)t;(void)bg; }

#endif
