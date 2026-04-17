#ifndef PSS_H
#define PSS_H

#include <stdint.h>
#include <stdbool.h>

/* Parsed style for one widget state */
typedef struct {
    /* ── Colors ─────────────────────────────────────────── */
    uint32_t background;        /* 0xRRGGBB */
    uint32_t color;             /* 0xRRGGBB  (foreground / text) */
    uint32_t border_color;      /* 0xRRGGBB */
    uint32_t outline_color;     /* 0xRRGGBB */
    uint32_t shadow_color;      /* 0xRRGGBB */
    uint32_t accent_color;      /* 0xRRGGBB  (checkbox tick, progress fill, etc.) */

    /* ── Border ─────────────────────────────────────────── */
    int      border_width;
    int      border_radius;
    int      border_top;        /* individual side widths (0 = inherit border_width) */
    int      border_right;
    int      border_bottom;
    int      border_left;
    int      outline_width;

    /* ── Spacing ─────────────────────────────────────────── */
    int      padding_x;
    int      padding_y;
    int      margin_x;
    int      margin_y;

    /* ── Size constraints ────────────────────────────────── */
    int      min_width;
    int      min_height;
    int      max_width;
    int      max_height;

    /* ── Typography ─────────────────────────────────────── */
    int      font_size;
    char     font[128];         /* font family name */
    int      font_weight;       /* 400 = normal, 700 = bold */
    int      font_italic;       /* 0 = normal, 1 = italic */
    int      line_height;       /* px, 0 = auto */
    int      letter_spacing;    /* px */
    int      text_align;        /* 0 = left, 1 = center, 2 = right */
    int      text_decoration;   /* 0 = none, 1 = underline, 2 = line-through */

    /* ── Effects ─────────────────────────────────────────── */
    int      opacity;           /* 0–100, 100 = fully opaque */
    int      shadow_blur;
    int      shadow_offset_x;
    int      shadow_offset_y;

    /* ── Misc ────────────────────────────────────────────── */
    char     cursor[32];        /* default | pointer | text | crosshair | not-allowed */
} PssStyle;

/* Max CSS custom properties per theme */
#define PSS_VAR_MAX 64

/* Full parsed theme */
typedef struct {
    /* ── Core widgets ────────────────────────────────────── */
    PssStyle window;

    PssStyle label;
    PssStyle title;
    PssStyle subtitle;
    PssStyle text;

    PssStyle button;
    PssStyle button_hover;
    PssStyle button_active;
    PssStyle button_disabled;

    PssStyle input;
    PssStyle input_focus;
    PssStyle input_disabled;

    PssStyle textarea;
    PssStyle textarea_focus;

    /* ── Interactive / state widgets ─────────────────────── */
    PssStyle checkbox;
    PssStyle checkbox_checked;
    PssStyle checkbox_disabled;

    PssStyle progressbar;
    PssStyle progressbar_fill;

    PssStyle scrollbar;
    PssStyle scrollbar_thumb;
    PssStyle scrollbar_thumb_hover;

    /* ── Navigation / layout widgets ─────────────────────── */
    PssStyle header;
    PssStyle sidebar;
    PssStyle panel;
    PssStyle card;
    PssStyle separator;
    PssStyle overlay;
    PssStyle dialog;

    /* ── List / menu widgets ─────────────────────────────── */
    PssStyle list;
    PssStyle list_item;
    PssStyle list_item_hover;
    PssStyle list_item_selected;

    PssStyle menu;
    PssStyle menu_item;
    PssStyle menu_item_hover;

    /* ── Tab widget ──────────────────────────────────────── */
    PssStyle tab;
    PssStyle tab_active;
    PssStyle tab_hover;

    /* ── Misc inline widgets ─────────────────────────────── */
    PssStyle link;
    PssStyle link_hover;
    PssStyle link_visited;

    PssStyle tooltip;

    PssStyle badge;
    PssStyle badge_success;
    PssStyle badge_warning;
    PssStyle badge_error;

    /* ── CSS custom properties (--name: value) ───────────── */
    char vars[PSS_VAR_MAX][2][128]; /* vars[i][0]=name, vars[i][1]=value */
    int  var_count;
} PssTheme;

/* Fill *t with sensible defaults */
void pss_theme_default(PssTheme *t);

/* Parse a .pss file into *t (returns false on open error) */
bool pss_theme_load(PssTheme *t, const char *path);

/* Look up a CSS custom property by name (returns NULL if not found) */
const char *pss_var_get(const PssTheme *t, const char *name);

#endif
