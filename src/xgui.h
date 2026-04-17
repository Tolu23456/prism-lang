#ifndef XGUI_H
#define XGUI_H

#include <stdbool.h>
#include <stdint.h>
#include "pss.h"

typedef struct XGui XGui;

/* Lifecycle */
XGui       *xgui_init(int width, int height, const char *title);
void        xgui_load_style(XGui *g, const char *pss_path);
bool        xgui_running(XGui *g);
void        xgui_destroy(XGui *g);

/* Per-frame */
void        xgui_begin(XGui *g);
void        xgui_end(XGui *g);

/* Widgets */
void        xgui_label(XGui *g, const char *text);
bool        xgui_button(XGui *g, const char *text);
const char *xgui_input(XGui *g, const char *id, const char *placeholder);
void        xgui_spacer(XGui *g, int height);

/* Layout helpers */
void        xgui_row_begin(XGui *g);
void        xgui_row_end(XGui *g);

/* New widgets */
void        xgui_title(XGui *g, const char *text);
void        xgui_subtitle(XGui *g, const char *text);
void        xgui_separator(XGui *g);
bool        xgui_checkbox(XGui *g, const char *id, const char *label);
void        xgui_progress(XGui *g, int value, int max_val);
float       xgui_slider(XGui *g, const char *id, float min_v, float max_v, float current);
const char *xgui_textarea(XGui *g, const char *id, const char *placeholder);
void        xgui_badge(XGui *g, const char *text, uint32_t bg_color);

#endif
