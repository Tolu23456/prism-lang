#ifndef PSS_H
#define PSS_H

#include <stdint.h>
#include <stdbool.h>

/* Parsed style for one widget state */
typedef struct {
    uint32_t background;    /* 0xRRGGBB */
    uint32_t color;         /* 0xRRGGBB  (foreground / text) */
    uint32_t border_color;  /* 0xRRGGBB */
    int      border_width;
    int      border_radius;
    int      padding_x;
    int      padding_y;
    int      font_size;
    char     font[64];      /* font family name */
} PssStyle;

/* Full parsed theme */
typedef struct {
    PssStyle window;
    PssStyle label;
    PssStyle button;
    PssStyle button_hover;
    PssStyle input;
    PssStyle input_focus;
} PssTheme;

/* Fill *t with sensible defaults */
void pss_theme_default(PssTheme *t);

/* Parse a .pss file into *t (returns false on open error) */
bool pss_theme_load(PssTheme *t, const char *path);

#endif
