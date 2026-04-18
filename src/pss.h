#ifndef PSS_H
#define PSS_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum shadow layers per widget state */
#define PSS_MAX_SHADOWS 4

/* One shadow layer */
typedef struct {
    int      x, y, blur;
    uint32_t color;
} PssShadowLayer;

/* Parsed style for one widget state */
typedef struct {
    /* ── Colors ─────────────────────────────────────────── */
    uint32_t background;        /* 0xRRGGBB */
    uint32_t color;             /* 0xRRGGBB  (foreground / text) */
    uint32_t border_color;      /* 0xRRGGBB */
    uint32_t outline_color;     /* 0xRRGGBB */
    uint32_t shadow_color;      /* 0xRRGGBB  (layer-0 compat) */
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
    int      gap;               /* gap between children in row/grid layouts */

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

    /* Legacy single-shadow (layer 0) — kept for backward compat */
    int      shadow_blur;
    int      shadow_offset_x;
    int      shadow_offset_y;

    /* ── Multi-layer shadows ──────────────────────────────── */
    int           shadow_count;                 /* 0 = none */
    PssShadowLayer shadows[PSS_MAX_SHADOWS];    /* shadows[0] == legacy fields */

    /* ── Gradient background ──────────────────────────────── */
    uint32_t gradient_end;      /* 0 = no gradient; start color = background */
    int      gradient_dir;      /* 0=top→bottom, 1=left→right, 2=TL→BR, 3=TR→BL */

    /* ── Misc ────────────────────────────────────────────── */
    char     cursor[32];        /* default | pointer | text | crosshair | not-allowed */
} PssStyle;

/* Max CSS custom properties per theme */
#define PSS_VAR_MAX 128

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

    /* ── Toggle switch ───────────────────────────────────── */
    PssStyle toggle;
    PssStyle toggle_on;
    PssStyle toggle_off;

    /* ── Slider ──────────────────────────────────────────── */
    PssStyle slider;
    PssStyle slider_thumb;
    PssStyle slider_track;

    /* ── Select / dropdown ───────────────────────────────── */
    PssStyle select;
    PssStyle select_open;
    PssStyle select_item;
    PssStyle select_item_hover;

    /* ── Chip ─────────────────────────────────────────────── */
    PssStyle chip;
    PssStyle chip_hover;
    PssStyle chip_active;

    /* ── Spinner ──────────────────────────────────────────── */
    PssStyle spinner;

    /* ── Section divider ──────────────────────────────────── */
    PssStyle section;

    /* ── Group box ────────────────────────────────────────── */
    PssStyle group;
    PssStyle group_title;

    /* ── Toast notification ───────────────────────────────── */
    PssStyle toast;
    PssStyle toast_success;
    PssStyle toast_warning;
    PssStyle toast_error;

    /* ── Radio button ─────────────────────────────────────── */
    PssStyle radio;
    PssStyle radio_checked;
    PssStyle radio_disabled;

    /* ── Menu bar ─────────────────────────────────────────── */
    PssStyle menu_bar;
    PssStyle menu_bar_item;
    PssStyle menu_bar_item_hover;
    PssStyle menu_bar_item_active;

    /* ── Context menu ─────────────────────────────────────── */
    PssStyle context_menu;
    PssStyle context_menu_item;
    PssStyle context_menu_item_hover;

    /* ── Table ────────────────────────────────────────────── */
    PssStyle table;
    PssStyle table_header;
    PssStyle table_row;
    PssStyle table_row_alt;
    PssStyle table_row_hover;
    PssStyle table_row_selected;
    PssStyle table_cell;

    /* ── Tree view ────────────────────────────────────────── */
    PssStyle tree;
    PssStyle tree_node;
    PssStyle tree_node_expanded;
    PssStyle tree_node_selected;

    /* ── Collapsing section ───────────────────────────────── */
    PssStyle collapsing;
    PssStyle collapsing_open;

    /* ── Modal / dialog ───────────────────────────────────── */
    PssStyle modal;
    PssStyle modal_overlay;
    PssStyle modal_title;

    /* ── Spinbox ──────────────────────────────────────────── */
    PssStyle spinbox;
    PssStyle spinbox_button;
    PssStyle spinbox_button_hover;

    /* ── Status bar ───────────────────────────────────────── */
    PssStyle status_bar;

    /* ── Splitter ─────────────────────────────────────────── */
    PssStyle splitter;
    PssStyle splitter_handle;
    PssStyle splitter_handle_hover;

    /* ── Icon button ──────────────────────────────────────── */
    PssStyle icon_button;
    PssStyle icon_button_hover;
    PssStyle icon_button_active;

    /* ── Scroll area ──────────────────────────────────────── */
    PssStyle scroll_area;

    /* ── Drag handle ──────────────────────────────────────── */
    PssStyle drag_control;
    PssStyle drag_control_hover;

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

    /* ── Active theme variant ─────────────────────────────── */
    char active_theme[32];      /* "light" | "dark" | "" */
} PssTheme;

/* Fill *t with sensible defaults */
void pss_theme_default(PssTheme *t);

/* Parse a .pss file into *t (returns false on open error) */
bool pss_theme_load(PssTheme *t, const char *path);

/* Parse PSS source string directly into *t */
void pss_theme_load_str(PssTheme *t, const char *src);

/* Look up a CSS custom property by name (returns NULL if not found) */
const char *pss_var_get(const PssTheme *t, const char *name);

#endif
