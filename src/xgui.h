/* xgui.h — Prism native X11 GUI toolkit v4.0
   Features: spring scroll, IQ SDF, 64-entry mask cache, 24-entry font cache,
   15+ widgets (toggle, chip, tabs, select, spinner, toast, grid, etc.),
   raw game-mode drawing, key-hold state, per-frame delta time.           */
#ifndef XGUI_H
#define XGUI_H

#include <stdbool.h>
#include <stdint.h>
#include "pss.h"

typedef struct XGui XGui;

/* ── lifecycle ──────────────────────────────────────────────────── */
XGui       *xgui_init(int width, int height, const char *title);
void        xgui_load_style(XGui *g, const char *pss_path);
void        xgui_set_dark(XGui *g, bool dark);
bool        xgui_running(XGui *g);
void        xgui_close(XGui *g);
void        xgui_destroy(XGui *g);

/* ── frame ──────────────────────────────────────────────────────── */
void        xgui_begin(XGui *g);
void        xgui_end(XGui *g);

/* ── text / display ─────────────────────────────────────────────── */
void        xgui_label(XGui *g, const char *text);
void        xgui_markdown(XGui *g, const char *text);
void        xgui_title(XGui *g, const char *text);
void        xgui_subtitle(XGui *g, const char *text);
void        xgui_separator(XGui *g);
void        xgui_section(XGui *g, const char *title);

/* ── interactive ────────────────────────────────────────────────── */
bool        xgui_button(XGui *g, const char *text);
bool        xgui_icon_button(XGui *g, const char *icon, const char *label);
const char *xgui_input(XGui *g, const char *id, const char *placeholder);
const char *xgui_textarea(XGui *g, const char *id, const char *placeholder);
bool        xgui_checkbox(XGui *g, const char *id, const char *label);
bool        xgui_toggle(XGui *g, const char *id, bool initial, const char *label);
void        xgui_progress(XGui *g, int value, int max_value);
float       xgui_slider(XGui *g, const char *id, float min, float max, float current);

/* ── new widgets ────────────────────────────────────────────────── */
void        xgui_badge(XGui *g, const char *text, uint32_t bg_color);
bool        xgui_chip(XGui *g, const char *text, bool removable);
int         xgui_tabs(XGui *g, const char *id, const char **labels, int count);
int         xgui_select(XGui *g, const char *id, const char **options, int count, int current);
void        xgui_spinner(XGui *g, int size);
bool        xgui_list_item(XGui *g, const char *title, const char *subtitle, const char *trailing);
void        xgui_table_begin(XGui *g, const char *id, int cols, const char **headers);
void        xgui_table_row(XGui *g, int count, const char **cells);
void        xgui_table_end(XGui *g);

/* ── overlay ────────────────────────────────────────────────────── */
void        xgui_show_toast(XGui *g, const char *text, int duration_frames);
void        xgui_tooltip(XGui *g, const char *text);

/* ── layout ─────────────────────────────────────────────────────── */
void        xgui_spacer(XGui *g, int pixels);
void        xgui_row_begin(XGui *g);
void        xgui_row_end(XGui *g);
void        xgui_flex_begin(XGui *g, bool horizontal, int gap);
void        xgui_flex_end(XGui *g);

/* ── advanced scroll ────────────────────────────────────────────── */
void        xgui_set_scroll(XGui *g, int y);
int         xgui_get_scroll(XGui *g);
void        xgui_set_hscroll(XGui *g, int x);
int         xgui_get_hscroll(XGui *g);
void        xgui_scroll_to_bottom(XGui *g);
void        xgui_ensure_visible(XGui *g, int x, int y, int w, int h);
void        xgui_grid_begin(XGui *g, int cols);
void        xgui_grid_end(XGui *g);
void        xgui_card_begin(XGui *g);
void        xgui_card_end(XGui *g);
void        xgui_group_begin(XGui *g, const char *title);
void        xgui_group_end(XGui *g);

/* ── raw drawing (game mode) ────────────────────────────────────── */
void        xgui_clear_bg(XGui *g, uint32_t color);
void        xgui_fill_rect_at(XGui *g, int x, int y, int w, int h, int r, uint32_t color);
void        xgui_fill_rect_grad_at(XGui *g, int x, int y, int w, int h, uint32_t c1, uint32_t c2, bool vertical);
void        xgui_fill_circle_at(XGui *g, int cx, int cy, int radius, uint32_t color);
void        xgui_draw_line_at(XGui *g, int x1, int y1, int x2, int y2, int thickness, uint32_t color);
void        xgui_draw_text_at(XGui *g, int x, int y, const char *text, int size, uint32_t color);
void        xgui_draw_text_centered(XGui *g, int cx, int cy, const char *text, int size, uint32_t color);
void        xgui_draw_text_bold_at(XGui *g, int x, int y, const char *text, int size, uint32_t color);
void        xgui_draw_text_bold_centered(XGui *g, int cx, int cy, const char *text, int size, uint32_t color);
void        xgui_draw_icon(XGui *g, const char *name, int x, int y, int size, uint32_t color);

/* ── key-hold state (game mode) ─────────────────────────────────── */
bool        xgui_key_held_char(XGui *g, char c);
bool        xgui_key_w(XGui *g);
bool        xgui_key_s(XGui *g);
bool        xgui_key_a(XGui *g);
bool        xgui_key_d(XGui *g);
bool        xgui_key_up(XGui *g);
bool        xgui_key_down(XGui *g);
bool        xgui_key_left(XGui *g);
bool        xgui_key_right(XGui *g);
bool        xgui_key_space(XGui *g);
bool        xgui_key_escape(XGui *g);
bool        xgui_key_enter_held(XGui *g);

/* ── mouse queries ──────────────────────────────────────────────── */
bool        xgui_mouse_down(XGui *g);
int         xgui_mouse_x(XGui *g);
int         xgui_mouse_y(XGui *g);

/* ── window / time ──────────────────────────────────────────────── */
int         xgui_win_w(XGui *g);
int         xgui_win_h(XGui *g);
float       xgui_delta_ms(XGui *g);
long long   xgui_clock_ms(XGui *g);
void        xgui_sleep_ms(XGui *g, int milliseconds);

#endif /* XGUI_H */
