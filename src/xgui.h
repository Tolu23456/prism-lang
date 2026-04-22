/* xgui.h — Prism native X11 GUI toolkit v5.0
   Features: momentum scroll + rubber-band, IQ SDF, 64-entry mask cache,
   24-entry font cache, 23+ widgets (radio, modal, collapsing, spinbox,
   status_bar, table, tree, context_menu + existing), raw game-mode drawing,
   key-hold state, per-frame delta time, PSS theming.               */
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

/* ── existing extra widgets ─────────────────────────────────────── */
void        xgui_badge(XGui *g, const char *text, uint32_t bg_color);
bool        xgui_chip(XGui *g, const char *text, bool removable);
int         xgui_tabs(XGui *g, const char *id, const char **labels, int count);
int         xgui_select(XGui *g, const char *id, const char **options, int count, int current);
void        xgui_spinner(XGui *g, int size);
bool        xgui_list_item(XGui *g, const char *title, const char *subtitle, const char *trailing);

/* ── NEW: radio button group ─────────────────────────────────────── */
/* Returns the selected index (0-based). Pass initial as current selection. */
int         xgui_radio(XGui *g, const char *id, const char **labels, int count, int current);

/* ── NEW: modal dialog ───────────────────────────────────────────── */
/* Returns true if the modal is currently open (call begin to start frame). */
bool        xgui_modal_begin(XGui *g, const char *id, const char *title, int width, int height);
void        xgui_modal_end(XGui *g);
void        xgui_modal_open(XGui *g, const char *id);
void        xgui_modal_close(XGui *g);
/* Convenience: draws a button inside a modal that also closes it on click. */
bool        xgui_modal_button(XGui *g, const char *label);

/* ── NEW: collapsing section ─────────────────────────────────────── */
/* Returns true when expanded. Toggle on header click. */
bool        xgui_collapsing(XGui *g, const char *id, const char *label);
void        xgui_collapsing_end(XGui *g);

/* ── NEW: numeric spinbox ────────────────────────────────────────── */
/* Returns current value. step=0 defaults to 1. */
double      xgui_spinbox(XGui *g, const char *id, double min_v, double max_v,
                          double current, double step);

/* ── NEW: status bar (fixed bottom strip) ───────────────────────── */
void        xgui_status_bar(XGui *g, const char *text);

/* ── NEW: data table ─────────────────────────────────────────────── */
void        xgui_table_begin(XGui *g, const char *id, int cols);
void        xgui_table_header(XGui *g, const char **headers, int count);
bool        xgui_table_row_begin(XGui *g, const char *row_id);
void        xgui_table_cell(XGui *g, const char *text);
void        xgui_table_row_end(XGui *g);
void        xgui_table_end(XGui *g);

/* ── NEW: tree view ──────────────────────────────────────────────── */
void        xgui_tree_begin(XGui *g, const char *id);
/* Returns true if the node is expanded and children should be drawn.
 * indent: depth level (0 = root). Call xgui_tree_indent/unindent manually. */
bool        xgui_tree_node(XGui *g, const char *node_id, const char *label,
                            bool has_children, int depth);
void        xgui_tree_end(XGui *g);

/* ── NEW: context menu ───────────────────────────────────────────── */
/* Call xgui_context_menu_begin() in the region that should respond to right-click.
 * Returns true when the menu is open. */
bool        xgui_context_menu_begin(XGui *g, const char *id, int region_w, int region_h);
/* Returns true when the item is clicked (closes menu automatically). */
bool        xgui_context_menu_item(XGui *g, const char *label);
void        xgui_context_menu_end(XGui *g);

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

/* ── NEW: single-frame key-pressed (any printable char) ────────── */
/* Returns true if the character was pressed this frame. */
bool        xgui_key_pressed(XGui *g, char c);

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

/* ── NEW: scroll queries ─────────────────────────────────────────── */
/* Returns the scroll offset applied this frame (positive = scrolled down). */
int         xgui_scroll_y(XGui *g);
/* Returns the scroll delta (wheel ticks) since last frame. Negative = up. */
int         xgui_scroll_delta(XGui *g);

#endif /* XGUI_H */
