#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gui_native.h"
#include "value.h"
#include "interpreter.h"

GuiWindow *gui_global_window = NULL;

typedef struct {
    char  title[256];
    int   width;
    int   height;
    char *body;
    int   body_len;
    int   body_cap;
    int   active;
} PguiState;

static PguiState g_pgui = {"PGUI Window", 720, 480, NULL, 0, 0, 0};

/* ------------------------------------------------------------------ HTML page */

static const char *HTML_PAGE =
    "<!DOCTYPE html>\n"
    "<html><head><title>Prism GUI</title>"
    "<style>body{margin:0;background:#111;display:flex;justify-content:center;"
    "align-items:center;height:100vh;}"
    "canvas{image-rendering:pixelated;image-rendering:crisp-edges;}"
    "</style></head><body>"
    "<canvas id='c'></canvas>"
    "<script>\n"
    "const c=document.getElementById('c');\n"
    "const ctx=c.getContext('2d');\n"
    "let W=0,H=0;\n"
    "async function poll(){\n"
    "  try{\n"
    "    const r=await fetch('/frame');\n"
    "    if(!r.ok){setTimeout(poll,100);return;}\n"
    "    const buf=await r.arrayBuffer();\n"
    "    const dv=new DataView(buf);\n"
    "    const nw=dv.getUint32(0,true),nh=dv.getUint32(4,true);\n"
    "    if(nw!==W||nh!==H){c.width=W=nw;c.height=H=nh;}\n"
    "    const px=new Uint8ClampedArray(buf,8);\n"
    "    const img=new ImageData(px,W,H);\n"
    "    ctx.putImageData(img,0,0);\n"
    "  }catch(e){}\n"
    "  setTimeout(poll,50);\n"
    "}\n"
    "poll();\n"
    "</script></body></html>\n";

/* ------------------------------------------------------------------ helpers */

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ------------------------------------------------------------------ create */

GuiWindow *gui_window_create(int width, int height, const char *title, int port) {
    GuiWindow *w = calloc(1, sizeof(GuiWindow));
    w->width   = width;
    w->height  = height;
    w->title   = strdup(title ? title : "Prism");
    w->running = 1;
    w->port    = port > 0 ? port : 8765;
    w->fb      = calloc((size_t)(width * height * 4), 1);

    /* Start TCP listener. */
    w->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (w->sock_fd < 0) {
        fprintf(stderr, "[gui] socket() failed: %s\n", strerror(errno));
        w->sock_fd = -1;
        gui_global_window = w;
        return w;
    }

    int opt = 1;
    setsockopt(w->sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(w->sock_fd);

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)w->port);

    if (bind(w->sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[gui] bind() failed on port %d: %s\n",
                w->port, strerror(errno));
        close(w->sock_fd);
        w->sock_fd = -1;
    } else {
        listen(w->sock_fd, 8);
    }

    gui_global_window = w;
    return w;
}

void gui_window_destroy(GuiWindow *w) {
    if (!w) return;
    if (w->sock_fd >= 0) close(w->sock_fd);
    free(w->fb);
    free(w->title);
    free(w);
    if (gui_global_window == w) gui_global_window = NULL;
}

void gui_window_print_url(GuiWindow *w) {
    if (!w) return;
    /* Detect Replit dev domain if available. */
    const char *replit_domain = getenv("REPLIT_DEV_DOMAIN");
    if (replit_domain && replit_domain[0]) {
        printf("[gui] Open: https://%s (proxied)\n", replit_domain);
    } else {
        printf("[gui] Open: http://localhost:%d\n", w->port);
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ drawing */

void gui_window_clear(GuiWindow *w, uint8_t r, uint8_t g, uint8_t b) {
    if (!w) return;
    int total = w->width * w->height;
    uint8_t *p = w->fb;
    for (int i = 0; i < total; i++) {
        *p++ = r; *p++ = g; *p++ = b; *p++ = 255;
    }
}

void gui_window_set_pixel(GuiWindow *w, int x, int y,
                          uint8_t r, uint8_t g, uint8_t b) {
    if (!w || x < 0 || y < 0 || x >= w->width || y >= w->height) return;
    uint8_t *p = w->fb + (y * w->width + x) * 4;
    p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
}

void gui_window_rect(GuiWindow *w, int x, int y, int rw, int rh,
                     uint8_t r, uint8_t g, uint8_t b) {
    if (!w) return;
    int x1 = x + rw, y1 = y + rh;
    if (x  < 0)        x  = 0;
    if (y  < 0)        y  = 0;
    if (x1 > w->width)  x1 = w->width;
    if (y1 > w->height) y1 = w->height;
    for (int iy = y; iy < y1; iy++) {
        uint8_t *row = w->fb + (iy * w->width + x) * 4;
        for (int ix = x; ix < x1; ix++) {
            *row++ = r; *row++ = g; *row++ = b; *row++ = 255;
        }
    }
}

void gui_window_circle(GuiWindow *w, int cx, int cy, int radius,
                       uint8_t r, uint8_t g, uint8_t b) {
    if (!w || radius <= 0) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= r2)
                gui_window_set_pixel(w, cx + dx, cy + dy, r, g, b);
        }
    }
}

void gui_window_line(GuiWindow *w, int x0, int y0, int x1, int y1,
                     uint8_t r, uint8_t g, uint8_t b) {
    if (!w) return;
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        gui_window_set_pixel(w, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ------------------------------------------------------------------ HTTP server */

static void send_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) break;
        buf += n; len -= (size_t)n;
    }
}

static void serve_client(GuiWindow *w, int cfd) {
    /* Read request (just enough to identify the path). */
    char req[512] = {0};
    ssize_t n = read(cfd, req, sizeof(req) - 1);
    if (n <= 0) { close(cfd); return; }

    int is_frame = (strncmp(req, "GET /frame", 10) == 0);
    int is_root  = (strncmp(req, "GET / ",    6)  == 0 ||
                    strncmp(req, "GET /\r",   6)  == 0 ||
                    strncmp(req, "GET /\n",   6)  == 0);

    if (is_frame) {
        /* Binary response: 4-byte LE width, 4-byte LE height, then RGBA pixels. */
        size_t pixel_bytes = (size_t)(w->width * w->height * 4);
        size_t body_len    = 8 + pixel_bytes;

        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\n"
            "\r\n", body_len);
        send_all(cfd, header, (size_t)hlen);

        /* Width + height as little-endian uint32. */
        uint8_t meta[8];
        uint32_t uw = (uint32_t)w->width, uh = (uint32_t)w->height;
        meta[0]=(uint8_t)(uw);  meta[1]=(uint8_t)(uw>>8);
        meta[2]=(uint8_t)(uw>>16); meta[3]=(uint8_t)(uw>>24);
        meta[4]=(uint8_t)(uh);  meta[5]=(uint8_t)(uh>>8);
        meta[6]=(uint8_t)(uh>>16); meta[7]=(uint8_t)(uh>>24);
        send_all(cfd, (char *)meta, 8);
        send_all(cfd, (char *)w->fb, pixel_bytes);
    } else if (is_root) {
        size_t page_len = strlen(HTML_PAGE);
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "\r\n", page_len);
        send_all(cfd, header, (size_t)hlen);
        send_all(cfd, HTML_PAGE, page_len);
    } else {
        const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(cfd, not_found, strlen(not_found));
    }
    close(cfd);
}

int gui_window_poll(GuiWindow *w) {
    if (!w || w->sock_fd < 0) return 0;

    struct sockaddr_in cli = {0};
    socklen_t len = sizeof(cli);
    int cfd = accept(w->sock_fd, (struct sockaddr *)&cli, &len);
    if (cfd < 0) return 0; /* EAGAIN / EWOULDBLOCK — no pending connection */
    serve_client(w, cfd);
    return 1;
}

/* ================================================================== Prism builtins */

static int int_arg(Value **args, int argc, int idx, int def) {
    if (idx >= argc) return def;
    Value *v = args[idx];
    if (v->type == VAL_INT)   return (int)v->int_val;
    if (v->type == VAL_FLOAT) return (int)v->float_val;
    return def;
}

static const char *str_arg(Value **args, int argc, int idx, const char *def) {
    if (idx >= argc || args[idx]->type != VAL_STRING) return def;
    return args[idx]->str_val;
}

static void pgui_body_reserve(int add) {
    if (!g_pgui.body) {
        g_pgui.body_cap = 4096;
        g_pgui.body = malloc((size_t)g_pgui.body_cap);
        g_pgui.body[0] = '\0';
        g_pgui.body_len = 0;
    }
    while (g_pgui.body_len + add + 1 >= g_pgui.body_cap) {
        g_pgui.body_cap *= 2;
        g_pgui.body = realloc(g_pgui.body, (size_t)g_pgui.body_cap);
    }
}

static void pgui_body_append_raw(const char *html) {
    int add = (int)strlen(html);
    pgui_body_reserve(add);
    memcpy(g_pgui.body + g_pgui.body_len, html, (size_t)add + 1);
    g_pgui.body_len += add;
}

static char *pgui_escape(const char *text) {
    size_t cap = strlen(text ? text : "") * 6 + 1;
    char *out = malloc(cap);
    size_t len = 0;
    for (const char *p = text ? text : ""; *p; p++) {
        const char *rep = NULL;
        switch (*p) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = "&quot;"; break;
            case '\'': rep = "&#39;"; break;
            default: break;
        }
        if (rep) {
            size_t n = strlen(rep);
            memcpy(out + len, rep, n);
            len += n;
        } else {
            out[len++] = *p;
        }
    }
    out[len] = '\0';
    return out;
}

static void pgui_append_widget(const char *kind, const char *text, const char *extra) {
    char *safe = pgui_escape(text);
    char buf[4096];
    if (strcmp(kind, "label") == 0) {
        snprintf(buf, sizeof(buf), "    <div class=\"pgui-label\">%s</div>\n", safe);
    } else if (strcmp(kind, "button") == 0) {
        snprintf(buf, sizeof(buf), "    <button class=\"pgui-button\" type=\"button\">%s</button>\n", safe);
    } else if (strcmp(kind, "input") == 0) {
        snprintf(buf, sizeof(buf), "    <input class=\"pgui-input\" placeholder=\"%s\" />\n", safe);
    } else if (strcmp(kind, "box") == 0) {
        snprintf(buf, sizeof(buf), "    <div class=\"pgui-box pgui-%s\"></div>\n", extra ? extra : "vertical");
    } else if (strcmp(kind, "separator") == 0) {
        snprintf(buf, sizeof(buf), "    <div class=\"pgui-separator\"></div>\n");
    } else {
        snprintf(buf, sizeof(buf), "    <div>%s</div>\n", safe);
    }
    pgui_body_append_raw(buf);
    free(safe);
}

static Value *bi_pgui_window(Value **args, int argc) {
    const char *title = "PGUI Window";
    int width = 720;
    int height = 480;
    if (argc >= 1 && args[0]->type == VAL_STRING) {
        title = args[0]->str_val;
        width = int_arg(args, argc, 1, width);
        height = int_arg(args, argc, 2, height);
    } else {
        width = int_arg(args, argc, 0, width);
        height = int_arg(args, argc, 1, height);
        title = str_arg(args, argc, 2, title);
    }
    snprintf(g_pgui.title, sizeof(g_pgui.title), "%s", title);
    g_pgui.width = width > 0 ? width : 720;
    g_pgui.height = height > 0 ? height : 480;
    g_pgui.active = 1;
    free(g_pgui.body);
    g_pgui.body = NULL;
    g_pgui.body_len = 0;
    g_pgui.body_cap = 0;
    return value_null();
}

static Value *bi_pgui_label(Value **args, int argc) {
    pgui_append_widget("label", str_arg(args, argc, 0, ""), NULL);
    return value_null();
}

static Value *bi_pgui_button(Value **args, int argc) {
    pgui_append_widget("button", str_arg(args, argc, 0, "Button"), NULL);
    return value_null();
}

static Value *bi_pgui_input(Value **args, int argc) {
    pgui_append_widget("input", str_arg(args, argc, 0, ""), NULL);
    return value_null();
}

static Value *bi_pgui_entry(Value **args, int argc) {
    return bi_pgui_input(args, argc);
}

static Value *bi_pgui_box(Value **args, int argc) {
    const char *orientation = str_arg(args, argc, 0, "vertical");
    if (strcmp(orientation, "horizontal") != 0 && strcmp(orientation, "row") != 0)
        orientation = "vertical";
    pgui_append_widget("box", "", strcmp(orientation, "row") == 0 ? "horizontal" : orientation);
    return value_null();
}

static Value *bi_pgui_separator(Value **args, int argc) {
    (void)args; (void)argc;
    pgui_append_widget("separator", "", NULL);
    return value_null();
}

static Value *bi_pgui_backend(Value **args, int argc) {
    (void)args; (void)argc;
    return value_string("PGUI native GTK3-style renderer (Prism C core, no GTK or third-party bindings)");
}

static Value *bi_pgui_version(Value **args, int argc) {
    (void)args; (void)argc;
    return value_string("PGUI 0.1.0");
}

static Value *bi_pgui_run(Value **args, int argc) {
    const char *path = str_arg(args, argc, 0, "pgui.html");
    if (!g_pgui.active) {
        bi_pgui_window(NULL, 0);
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[pgui] could not write %s\n", path);
        return value_null();
    }
    char *safe_title = pgui_escape(g_pgui.title);
    fprintf(f,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>%s</title>\n"
        "  <style>\n"
        "    :root { color-scheme: light; --pgui-bg: #f5f5f5; --pgui-panel: #ffffff; --pgui-border: #b8b8b8; --pgui-text: #242424; --pgui-accent: #3584e4; }\n"
        "    body { margin: 0; min-height: 100vh; background: var(--pgui-bg); color: var(--pgui-text); font-family: Cantarell, Segoe UI, sans-serif; display: grid; place-items: center; }\n"
        "    .pgui-window { width: %dpx; min-height: %dpx; background: var(--pgui-panel); border: 1px solid var(--pgui-border); border-radius: 9px; box-shadow: 0 18px 48px rgba(0,0,0,.22); overflow: hidden; }\n"
        "    .pgui-header { height: 42px; display: flex; align-items: center; justify-content: center; font-weight: 700; background: linear-gradient(#eeeeee,#d9d9d9); border-bottom: 1px solid var(--pgui-border); }\n"
        "    .pgui-content { padding: 18px; display: flex; flex-direction: column; gap: 12px; }\n"
        "    .pgui-label { font-size: 15px; line-height: 1.45; }\n"
        "    .pgui-button { align-self: flex-start; min-width: 88px; padding: 8px 18px; border-radius: 7px; border: 1px solid #1b63b7; background: linear-gradient(#4e9af2,#2f7bd8); color: white; font-weight: 700; }\n"
        "    .pgui-button:active { background: linear-gradient(#2f7bd8,#4e9af2); }\n"
        "    .pgui-input { padding: 9px 11px; border-radius: 6px; border: 1px solid var(--pgui-border); font: inherit; }\n"
        "    .pgui-box { border: 1px dashed #c7c7c7; border-radius: 7px; min-height: 28px; background: #fafafa; }\n"
        "    .pgui-horizontal { min-height: 42px; }\n"
        "    .pgui-separator { height: 1px; background: var(--pgui-border); margin: 4px 0; }\n"
        "    .pgui-footer { padding: 8px 12px; font-size: 12px; color: #666; border-top: 1px solid #ddd; background: #f1f1f1; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <main class=\"pgui-window\" role=\"application\" aria-label=\"%s\">\n"
        "    <header class=\"pgui-header\">%s</header>\n"
        "    <section class=\"pgui-content\">\n"
        "%s"
        "    </section>\n"
        "    <footer class=\"pgui-footer\">PGUI native GTK3-style toolkit — built into Prism, no GTK library or bindings required.</footer>\n"
        "  </main>\n"
        "</body>\n"
        "</html>\n",
        safe_title, g_pgui.width, g_pgui.height, safe_title, safe_title,
        g_pgui.body ? g_pgui.body : "");
    fclose(f);
    printf("[pgui] PGUI window written to %s\n", path);
    free(safe_title);
    return value_null();
}

static Value *bi_gui_create(Value **args, int argc) {
    int w = int_arg(args, argc, 0, 640);
    int h = int_arg(args, argc, 1, 480);
    const char *title = (argc >= 3 && args[2]->type == VAL_STRING)
                        ? args[2]->str_val : "Prism";
    int port = int_arg(args, argc, 3, 8765);
    GuiWindow *win = gui_window_create(w, h, title, port);
    gui_window_print_url(win);
    return value_null();
}

static Value *bi_gui_clear(Value **args, int argc) {
    uint8_t r = (uint8_t)int_arg(args, argc, 0, 0);
    uint8_t g = (uint8_t)int_arg(args, argc, 1, 0);
    uint8_t b = (uint8_t)int_arg(args, argc, 2, 0);
    gui_window_clear(gui_global_window, r, g, b);
    return value_null();
}

static Value *bi_gui_set_pixel(Value **args, int argc) {
    int x = int_arg(args, argc, 0, 0);
    int y = int_arg(args, argc, 1, 0);
    uint8_t r = (uint8_t)int_arg(args, argc, 2, 255);
    uint8_t g = (uint8_t)int_arg(args, argc, 3, 255);
    uint8_t b = (uint8_t)int_arg(args, argc, 4, 255);
    gui_window_set_pixel(gui_global_window, x, y, r, g, b);
    return value_null();
}

static Value *bi_gui_rect(Value **args, int argc) {
    int x  = int_arg(args, argc, 0, 0);
    int y  = int_arg(args, argc, 1, 0);
    int rw = int_arg(args, argc, 2, 10);
    int rh = int_arg(args, argc, 3, 10);
    uint8_t r = (uint8_t)int_arg(args, argc, 4, 255);
    uint8_t g = (uint8_t)int_arg(args, argc, 5, 255);
    uint8_t b = (uint8_t)int_arg(args, argc, 6, 255);
    gui_window_rect(gui_global_window, x, y, rw, rh, r, g, b);
    return value_null();
}

static Value *bi_gui_circle(Value **args, int argc) {
    int cx = int_arg(args, argc, 0, 0);
    int cy = int_arg(args, argc, 1, 0);
    int ra = int_arg(args, argc, 2, 10);
    uint8_t r = (uint8_t)int_arg(args, argc, 3, 255);
    uint8_t g = (uint8_t)int_arg(args, argc, 4, 255);
    uint8_t b = (uint8_t)int_arg(args, argc, 5, 255);
    gui_window_circle(gui_global_window, cx, cy, ra, r, g, b);
    return value_null();
}

static Value *bi_gui_line(Value **args, int argc) {
    int x0 = int_arg(args, argc, 0, 0);
    int y0 = int_arg(args, argc, 1, 0);
    int x1 = int_arg(args, argc, 2, 0);
    int y1 = int_arg(args, argc, 3, 0);
    uint8_t r = (uint8_t)int_arg(args, argc, 4, 255);
    uint8_t g = (uint8_t)int_arg(args, argc, 5, 255);
    uint8_t b = (uint8_t)int_arg(args, argc, 6, 255);
    gui_window_line(gui_global_window, x0, y0, x1, y1, r, g, b);
    return value_null();
}

static Value *bi_gui_poll(Value **args, int argc) {
    (void)args; (void)argc;
    /* Drain all pending connections. */
    if (gui_global_window) {
        int served = 0;
        while (gui_window_poll(gui_global_window))
            served++;
        (void)served;
        return value_bool(gui_global_window->running);
    }
    return value_bool(0);
}

static Value *bi_gui_running(Value **args, int argc) {
    (void)args; (void)argc;
    if (gui_global_window) return value_bool(gui_global_window->running);
    return value_bool(0);
}

static Value *bi_gui_print_url(Value **args, int argc) {
    (void)args; (void)argc;
    gui_window_print_url(gui_global_window);
    return value_null();
}

static Value *bi_gui_stop(Value **args, int argc) {
    (void)args; (void)argc;
    if (gui_global_window) gui_global_window->running = 0;
    return value_null();
}

void gui_register_builtins(Env *global_env) {
    struct { const char *name; BuiltinFn fn; } tbl[] = {
        {"__gui_create",    bi_gui_create},
        {"__gui_clear",     bi_gui_clear},
        {"__gui_set_pixel", bi_gui_set_pixel},
        {"__gui_rect",      bi_gui_rect},
        {"__gui_circle",    bi_gui_circle},
        {"__gui_line",      bi_gui_line},
        {"__gui_poll",      bi_gui_poll},
        {"__gui_running",   bi_gui_running},
        {"__gui_print_url", bi_gui_print_url},
        {"__gui_stop",      bi_gui_stop},
        {"pgui_window",     bi_pgui_window},
        {"pgui_label",      bi_pgui_label},
        {"pgui_button",     bi_pgui_button},
        {"pgui_input",      bi_pgui_input},
        {"pgui_entry",      bi_pgui_entry},
        {"pgui_box",        bi_pgui_box},
        {"pgui_separator",  bi_pgui_separator},
        {"pgui_run",        bi_pgui_run},
        {"pgui_backend",    bi_pgui_backend},
        {"pgui_version",    bi_pgui_version},
        {NULL, NULL}
    };
    for (int i = 0; tbl[i].name; i++) {
        Value *v = value_builtin(tbl[i].name, tbl[i].fn);
        env_set(global_env, tbl[i].name, v, true);
        value_release(v);
    }
}
