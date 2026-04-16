#ifndef GUI_NATIVE_H
#define GUI_NATIVE_H

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>

typedef struct {
    int      width;
    int      height;
    char    *title;
    uint8_t *fb;        /* RGBA framebuffer, width*height*4 bytes */
    int      running;
    int      sock_fd;   /* listening socket fd, -1 if not started */
    int      port;
} GuiWindow;

/* Create a window (allocates framebuffer, starts HTTP server on port). */
GuiWindow *gui_window_create(int width, int height, const char *title, int port);

/* Destroy window and free resources. */
void gui_window_destroy(GuiWindow *w);

/* Clear entire framebuffer to colour. */
void gui_window_clear(GuiWindow *w, uint8_t r, uint8_t g, uint8_t b);

/* Set a single pixel. */
void gui_window_set_pixel(GuiWindow *w, int x, int y,
                          uint8_t r, uint8_t g, uint8_t b);

/* Draw a filled rectangle. */
void gui_window_rect(GuiWindow *w, int x, int y, int rw, int rh,
                     uint8_t r, uint8_t g, uint8_t b);

/* Draw a filled circle. */
void gui_window_circle(GuiWindow *w, int cx, int cy, int radius,
                       uint8_t r, uint8_t g, uint8_t b);

/* Draw anti-aliased line. */
void gui_window_line(GuiWindow *w, int x0, int y0, int x1, int y1,
                     uint8_t r, uint8_t g, uint8_t b);

/* Poll HTTP connections; serve one pending request if any.
 * Returns 1 if a client was served, 0 otherwise (non-blocking). */
int gui_window_poll(GuiWindow *w);

/* Print the URL where the GUI can be viewed. */
void gui_window_print_url(GuiWindow *w);

/* Global singleton accessor (set by gui_window_create). */
extern GuiWindow *gui_global_window;

/* Register all __gui_* native builtins into the interpreter global env. */
struct Env;
void gui_register_builtins(struct Env *global_env);

#endif
