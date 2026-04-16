#ifndef XGUI_H
#define XGUI_H

#include <stdbool.h>
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

#endif
