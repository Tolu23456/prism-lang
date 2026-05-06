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

/* ── overlay ────────────────────────────────────────────────────── */
void        xgui_show_toast(XGui *g, const char *text, int duration_frames);
void        xgui_tooltip(XGui *g, const char *text);

/* ── layout ─────────────────────────────────────────────────────── */
void        xgui_spacer(XGui *g, int pixels);
void        xgui_row_begin(XGui *g);
void        xgui_row_end(XGui *g);
void        xgui_grid_begin(XGui *g, int cols);
void        xgui_grid_end(XGui *g);
void        xgui_card_begin(XGui *g);
void        xgui_card_end(XGui *g);
void        xgui_group_begin(XGui *g, const char *title);
void        xgui_group_end(XGui *g);

/* ── raw drawing (game mode) ────────────────────────────────────── */
void        xgui_clear_bg(XGui *g, uint32_t color);
void        xgui_fill_rect_at(XGui *g, int x, int y, int w, int h, int r, uint32_t color);
void        xgui_fill_circle_at(XGui *g, int cx, int cy, int radius, uint32_t color);
void        xgui_draw_line_at(XGui *g, int x1, int y1, int x2, int y2, int thickness, uint32_t color);
void        xgui_draw_text_at(XGui *g, int x, int y, const char *text, int size, uint32_t color);
void        xgui_draw_text_centered(XGui *g, int cx, int cy, const char *text, int size, uint32_t color);
void        xgui_draw_text_bold_at(XGui *g, int x, int y, const char *text, int size, uint32_t color);
void        xgui_draw_text_bold_centered(XGui *g, int cx, int cy, const char *text, int size, uint32_t color);

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

/* ── NEW: Advanced Data Widgets ─────────────────────────────────── */
void        xgui_data_grid_begin(XGui *g, int rows, int cols);
void        xgui_data_grid_end(XGui *g);
void        xgui_data_grid_cell(XGui *g, const char *content);
void        xgui_tree_view_begin(XGui *g, const char *root_label);
void        xgui_tree_view_end(XGui *g);
bool        xgui_tree_node(XGui *g, const char *label, int depth);
void        xgui_table_begin(XGui *g, const char **headers, int col_count);
void        xgui_table_end(XGui *g);
void        xgui_table_row(XGui *g, const char **cells, int col_count);

/* ── NEW: Menu and Navigation ───────────────────────────────────── */
void        xgui_menu_bar_begin(XGui *g);
void        xgui_menu_bar_end(XGui *g);
bool        xgui_menu_item(XGui *g, const char *label);
void        xgui_submenu_begin(XGui *g, const char *label);
void        xgui_submenu_end(XGui *g);
void        xgui_breadcrumb_begin(XGui *g);
void        xgui_breadcrumb_end(XGui *g);
bool        xgui_breadcrumb_item(XGui *g, const char *label);

/* ── NEW: Input and Selection ───────────────────────────────────── */
int         xgui_color_picker(XGui *g, const char *id, uint32_t initial);
const char *xgui_date_picker(XGui *g, const char *id);
const char *xgui_time_picker(XGui *g, const char *id);
int         xgui_multi_select(XGui *g, const char *id, const char **options, int count);
float       xgui_range_slider(XGui *g, const char *id, float min, float max);
const char *xgui_search_input(XGui *g, const char *id, const char *placeholder);
bool        xgui_file_picker(XGui *g, const char *id, const char *filter);

/* ── NEW: Advanced Layout ───────────────────────────────────────── */
void        xgui_flex_begin(XGui *g, bool row_direction);
void        xgui_flex_end(XGui *g);
void        xgui_flex_item(XGui *g, int flex_grow, int flex_shrink);
void        xgui_responsive_grid_begin(XGui *g, int min_cols, int max_cols);
void        xgui_responsive_grid_end(XGui *g);
void        xgui_sidebar_begin(XGui *g, int width);
void        xgui_sidebar_end(XGui *g);
void        xgui_modal_begin(XGui *g, const char *title);
void        xgui_modal_end(XGui *g);
bool        xgui_modal_close_button(XGui *g);

/* ── NEW: Visualization ─────────────────────────────────────────── */
void        xgui_progress_ring(XGui *g, float percent, int size);
void        xgui_gauge(XGui *g, const char *label, float value, float max_value);
void        xgui_mini_chart(XGui *g, const int *data, int count, int width, int height);
void        xgui_heatmap_cell(XGui *g, float intensity);
void        xgui_star_rating(XGui *g, const char *id, int rating, int max_stars);

/* ── NEW: Styling and Themes ────────────────────────────────────── */
void        xgui_set_theme(XGui *g, const char *theme_name);
void        xgui_push_style_color(XGui *g, int style_var, uint32_t color);
void        xgui_pop_style_color(XGui *g);
void        xgui_push_style_float(XGui *g, int style_var, float value);
void        xgui_pop_style_float(XGui *g);
uint32_t    xgui_get_color(XGui *g, const char *color_name);

/* ── NEW: Animations and Transitions ────────────────────────────── */
void        xgui_animate_value(XGui *g, float *value, float target, float speed);
void        xgui_animate_color(XGui *g, uint32_t *color, uint32_t target, float speed);
void        xgui_slide_in_animation(XGui *g, bool *active, float duration);
void        xgui_fade_animation(XGui *g, float *alpha, float target, float duration);

/* ── NEW: Touch and Gesture ─────────────────────────────────────── */
bool        xgui_swipe_left(XGui *g);
bool        xgui_swipe_right(XGui *g);
bool        xgui_swipe_up(XGui *g);
bool        xgui_swipe_down(XGui *g);
float       xgui_pinch_zoom(XGui *g);
int         xgui_touch_x(XGui *g);
int         xgui_touch_y(XGui *g);

/* ── NEW: Developer Tools ───────────────────────────────────────── */
void        xgui_show_inspector(XGui *g, bool show);
void        xgui_show_metrics(XGui *g, bool show);
void        xgui_show_style_editor(XGui *g, bool show);
void        xgui_log_message(XGui *g, const char *level, const char *message);
void        xgui_profiler_begin(XGui *g, const char *label);
void        xgui_profiler_end(XGui *g);

/* ── NEW: Hot Reload Support ────────────────────────────────────── */
bool        xgui_watch_style_file(XGui *g, const char *pss_path);
void        xgui_reload_styles(XGui *g);
bool        xgui_has_style_changed(XGui *g);

#endif /* XGUI_H */
