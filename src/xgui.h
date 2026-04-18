#ifndef XGUI_H
#define XGUI_H

#include <stdbool.h>
#include <stdint.h>
#include "pss.h"

typedef struct XGui XGui;

/* ── Lifecycle ─────────────────────────────────────────────────────── */
XGui       *xgui_init(int width, int height, const char *title);
void        xgui_load_style(XGui *g, const char *pss_path);
void        xgui_set_dark(XGui *g, bool dark);
bool        xgui_running(XGui *g);
void        xgui_close(XGui *g);
void        xgui_destroy(XGui *g);

/* ── Per-frame ─────────────────────────────────────────────────────── */
void        xgui_begin(XGui *g);
void        xgui_end(XGui *g);

/* ── Core widgets ──────────────────────────────────────────────────── */
void        xgui_label(XGui *g, const char *text);
bool        xgui_button(XGui *g, const char *text);
const char *xgui_input(XGui *g, const char *id, const char *placeholder);
void        xgui_spacer(XGui *g, int height);

/* ── Layout helpers ────────────────────────────────────────────────── */
void        xgui_row_begin(XGui *g);
void        xgui_row_end(XGui *g);

/* ── Text / display ────────────────────────────────────────────────── */
void        xgui_title(XGui *g, const char *text);
void        xgui_subtitle(XGui *g, const char *text);
void        xgui_separator(XGui *g);
void        xgui_badge(XGui *g, const char *text, uint32_t bg_color);

/* ── Interactive ───────────────────────────────────────────────────── */
bool        xgui_checkbox(XGui *g, const char *id, const char *label);
void        xgui_progress(XGui *g, int value, int max_val);
float       xgui_slider(XGui *g, const char *id, float min_v, float max_v, float current);
const char *xgui_textarea(XGui *g, const char *id, const char *placeholder);

/* ── Card container (begin/end pair) ───────────────────────────────── */
void        xgui_card_begin(XGui *g);
void        xgui_card_end(XGui *g);

/* ── Tooltip (call each frame while hovered) ───────────────────────── */
void        xgui_tooltip(XGui *g, const char *text);

#endif
