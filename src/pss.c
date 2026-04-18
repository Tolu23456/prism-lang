#include "pss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ defaults */

static void style_default_base(PssStyle *s, uint32_t bg, uint32_t fg) {
    memset(s, 0, sizeof(*s));
    s->background    = bg;
    s->color         = fg;
    s->opacity       = 100;
    s->font_size     = 13;
    s->font_weight   = 400;
    s->line_height   = 0;
    snprintf(s->font,   sizeof(s->font),   "sans");
    snprintf(s->cursor, sizeof(s->cursor), "default");
}

void pss_theme_default(PssTheme *t) {
    memset(t, 0, sizeof(*t));

    style_default_base(&t->window,    0xf0f0f0, 0x222222);
    t->window.padding_x = 16; t->window.padding_y = 16;

    style_default_base(&t->label,     0xf0f0f0, 0x222222);
    t->label.padding_y = 2;

    style_default_base(&t->title,     0xf0f0f0, 0x111111);
    t->title.font_size   = 22;
    t->title.font_weight = 700;
    t->title.padding_y   = 6;

    style_default_base(&t->subtitle,  0xf0f0f0, 0x555555);
    t->subtitle.font_size   = 16;
    t->subtitle.font_weight = 600;
    t->subtitle.padding_y   = 4;

    style_default_base(&t->text,      0xf0f0f0, 0x333333);
    t->text.padding_y = 2;

    /* ── Button ─────────────────────────────────────────── */
    style_default_base(&t->button,    0x0078d4, 0xffffff);
    t->button.border_radius = 5;
    t->button.padding_x     = 18;
    t->button.padding_y     = 8;
    t->button.border_color  = 0x005fa3;
    snprintf(t->button.cursor, sizeof(t->button.cursor), "pointer");

    t->button_hover         = t->button;
    t->button_hover.background = 0x106ebe;

    t->button_active        = t->button;
    t->button_active.background = 0x005fa3;

    t->button_disabled      = t->button;
    t->button_disabled.background = 0xaaaaaa;
    t->button_disabled.color      = 0xeeeeee;
    snprintf(t->button_disabled.cursor, sizeof(t->button_disabled.cursor), "not-allowed");

    /* ── Input ──────────────────────────────────────────── */
    style_default_base(&t->input,     0xffffff, 0x222222);
    t->input.border_color  = 0xbcbcbc;
    t->input.border_width  = 1;
    t->input.border_radius = 4;
    t->input.padding_x     = 10;
    t->input.padding_y     = 6;
    snprintf(t->input.cursor, sizeof(t->input.cursor), "text");

    t->input_focus         = t->input;
    t->input_focus.border_color = 0x0078d4;
    t->input_focus.border_width = 2;

    t->input_disabled      = t->input;
    t->input_disabled.background = 0xeeeeee;
    t->input_disabled.color      = 0x888888;
    snprintf(t->input_disabled.cursor, sizeof(t->input_disabled.cursor), "not-allowed");

    /* ── Textarea ────────────────────────────────────────── */
    t->textarea            = t->input;
    t->textarea.line_height = 20;
    t->textarea_focus      = t->input_focus;
    t->textarea_focus.line_height = 20;

    /* ── Checkbox ────────────────────────────────────────── */
    style_default_base(&t->checkbox,  0xffffff, 0x222222);
    t->checkbox.border_color  = 0xbcbcbc;
    t->checkbox.border_width  = 1;
    t->checkbox.border_radius = 3;
    t->checkbox.padding_x     = 2;
    t->checkbox.padding_y     = 2;
    snprintf(t->checkbox.cursor, sizeof(t->checkbox.cursor), "pointer");

    t->checkbox_checked        = t->checkbox;
    t->checkbox_checked.background    = 0x0078d4;
    t->checkbox_checked.border_color  = 0x0078d4;
    t->checkbox_checked.accent_color  = 0xffffff;

    t->checkbox_disabled       = t->checkbox;
    t->checkbox_disabled.background   = 0xeeeeee;
    t->checkbox_disabled.border_color = 0xcccccc;
    snprintf(t->checkbox_disabled.cursor, sizeof(t->checkbox_disabled.cursor), "not-allowed");

    /* ── Progressbar ─────────────────────────────────────── */
    style_default_base(&t->progressbar,      0xe0e0e0, 0x000000);
    t->progressbar.border_radius = 4;
    t->progressbar.min_height    = 8;

    style_default_base(&t->progressbar_fill, 0x0078d4, 0xffffff);
    t->progressbar_fill.border_radius = 4;

    /* ── Scrollbar ───────────────────────────────────────── */
    style_default_base(&t->scrollbar,              0xf0f0f0, 0x000000);
    t->scrollbar.min_width  = 12;
    t->scrollbar.border_radius = 0;

    style_default_base(&t->scrollbar_thumb,        0xbcbcbc, 0x000000);
    t->scrollbar_thumb.border_radius = 6;

    style_default_base(&t->scrollbar_thumb_hover,  0x909090, 0x000000);
    t->scrollbar_thumb_hover.border_radius = 6;

    /* ── Layout / Navigation ─────────────────────────────── */
    style_default_base(&t->header,    0x0078d4, 0xffffff);
    t->header.padding_x = 16; t->header.padding_y = 12;
    t->header.font_weight = 600;

    style_default_base(&t->sidebar,   0xe8e8e8, 0x333333);
    t->sidebar.padding_x = 8; t->sidebar.padding_y = 8;
    t->sidebar.min_width = 200;

    style_default_base(&t->panel,     0xfafafa, 0x333333);
    t->panel.border_color = 0xdddddd;
    t->panel.border_width = 1;
    t->panel.border_radius = 4;
    t->panel.padding_x = 12; t->panel.padding_y = 12;

    style_default_base(&t->card,      0xffffff, 0x333333);
    t->card.border_radius = 8;
    t->card.padding_x     = 16;
    t->card.padding_y     = 16;
    t->card.shadow_color  = 0x000000;
    t->card.shadow_blur   = 8;
    t->card.shadow_offset_y = 2;

    style_default_base(&t->separator, 0xdddddd, 0x000000);
    t->separator.min_height = 1;
    t->separator.margin_y   = 8;

    style_default_base(&t->overlay,   0x000000, 0xffffff);
    t->overlay.opacity = 50;

    style_default_base(&t->dialog,    0xffffff, 0x333333);
    t->dialog.border_radius = 8;
    t->dialog.padding_x     = 24;
    t->dialog.padding_y     = 24;
    t->dialog.shadow_color  = 0x000000;
    t->dialog.shadow_blur   = 24;

    /* ── List / Menu ─────────────────────────────────────── */
    style_default_base(&t->list,              0xffffff, 0x333333);
    t->list.border_color = 0xdddddd;
    t->list.border_width = 1;
    t->list.border_radius = 4;

    style_default_base(&t->list_item,         0xffffff, 0x333333);
    t->list_item.padding_x = 12; t->list_item.padding_y = 8;
    snprintf(t->list_item.cursor, sizeof(t->list_item.cursor), "pointer");

    style_default_base(&t->list_item_hover,   0xf0f5ff, 0x333333);
    t->list_item_hover.padding_x = 12; t->list_item_hover.padding_y = 8;

    style_default_base(&t->list_item_selected, 0x0078d4, 0xffffff);
    t->list_item_selected.padding_x = 12; t->list_item_selected.padding_y = 8;

    style_default_base(&t->menu,              0xffffff, 0x333333);
    t->menu.border_color  = 0xdddddd;
    t->menu.border_width  = 1;
    t->menu.border_radius = 4;
    t->menu.shadow_color  = 0x000000;
    t->menu.shadow_blur   = 8;

    style_default_base(&t->menu_item,         0xffffff, 0x333333);
    t->menu_item.padding_x = 16; t->menu_item.padding_y = 8;
    snprintf(t->menu_item.cursor, sizeof(t->menu_item.cursor), "pointer");

    style_default_base(&t->menu_item_hover,   0xf0f5ff, 0x0078d4);
    t->menu_item_hover.padding_x = 16; t->menu_item_hover.padding_y = 8;

    /* ── Tabs ────────────────────────────────────────────── */
    style_default_base(&t->tab,       0xf0f0f0, 0x555555);
    t->tab.padding_x = 16; t->tab.padding_y = 10;
    t->tab.border_radius = 4;
    snprintf(t->tab.cursor, sizeof(t->tab.cursor), "pointer");

    style_default_base(&t->tab_active, 0xffffff, 0x0078d4);
    t->tab_active.padding_x  = 16; t->tab_active.padding_y = 10;
    t->tab_active.border_radius = 4;
    t->tab_active.font_weight   = 600;
    t->tab_active.border_bottom = 2;
    t->tab_active.border_color  = 0x0078d4;

    style_default_base(&t->tab_hover, 0xe8e8e8, 0x333333);
    t->tab_hover.padding_x = 16; t->tab_hover.padding_y = 10;
    t->tab_hover.border_radius = 4;

    /* ── Link ────────────────────────────────────────────── */
    style_default_base(&t->link,         0xfafafa, 0x0078d4);
    t->link.text_decoration = 1;
    snprintf(t->link.cursor, sizeof(t->link.cursor), "pointer");

    style_default_base(&t->link_hover,   0xfafafa, 0x106ebe);
    t->link_hover.text_decoration = 1;
    snprintf(t->link_hover.cursor, sizeof(t->link_hover.cursor), "pointer");

    style_default_base(&t->link_visited, 0xfafafa, 0x800080);
    t->link_visited.text_decoration = 1;

    /* ── Tooltip ─────────────────────────────────────────── */
    style_default_base(&t->tooltip,  0x333333, 0xffffff);
    t->tooltip.border_radius = 4;
    t->tooltip.padding_x     = 8;
    t->tooltip.padding_y     = 4;
    t->tooltip.font_size     = 11;

    /* ── Badges ──────────────────────────────────────────── */
    style_default_base(&t->badge,          0x555555, 0xffffff);
    t->badge.border_radius = 10; t->badge.padding_x = 8; t->badge.padding_y = 2;
    t->badge.font_size = 11;

    t->badge_success           = t->badge;
    t->badge_success.background = 0x28a745;

    t->badge_warning           = t->badge;
    t->badge_warning.background = 0xffc107;
    t->badge_warning.color      = 0x333333;

    t->badge_error             = t->badge;
    t->badge_error.background   = 0xdc3545;
}

/* ------------------------------------------------------------------ helpers */

static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const char *skip_ws_comments(const char *p) {
    for (;;) {
        p = skip_ws(p);
        /* block comment: slash-star ... star-slash */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
        /* line comment: // ... */
        } else if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n') p++;
        } else {
            break;
        }
    }
    return p;
}

/* Read a word (alphanumeric + - _ ) into buf. */
static const char *read_word(const char *p, char *buf, int bufsz) {
    int i = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_') && i < bufsz-1)
        buf[i++] = *p++;
    buf[i] = '\0';
    return p;
}

/* Read a token until whitespace / ; / }  — also handles rgb(...) */
static const char *read_value_token(const char *p, char *buf, int bufsz) {
    p = skip_ws(p);
    int i = 0;
    if (*p == 'r' && p[1] == 'g' && p[2] == 'b') {
        /* rgb(...) or rgba(...) — read up to matching ')' */
        while (*p && *p != ')' && i < bufsz-2) buf[i++] = *p++;
        if (*p == ')') buf[i++] = *p++;
        buf[i] = '\0';
        return p;
    }
    while (*p && *p != ';' && *p != '}' && *p != '\n'
           && !isspace((unsigned char)*p) && i < bufsz-1)
        buf[i++] = *p++;
    buf[i] = '\0';
    return p;
}

/* ------------------------------------------------------------------ color parsing */

typedef struct { const char *name; uint32_t hex; } NamedColor;
static const NamedColor s_colors[] = {
    {"white",        0xffffff}, {"black",        0x000000},
    {"red",          0xff0000}, {"green",        0x00aa00},
    {"blue",         0x0000ff}, {"gray",         0x808080},
    {"grey",         0x808080}, {"silver",       0xc0c0c0},
    {"orange",       0xff8800}, {"yellow",       0xffdd00},
    {"purple",       0x800080}, {"pink",         0xff69b4},
    {"cyan",         0x00ffff}, {"magenta",      0xff00ff},
    {"lime",         0x00ff00}, {"navy",         0x000080},
    {"teal",         0x008080}, {"coral",        0xff7f50},
    {"salmon",       0xfa8072}, {"crimson",      0xdc143c},
    {"indigo",       0x4b0082}, {"violet",       0xee82ee},
    {"tan",          0xd2b48c}, {"beige",        0xf5f5dc},
    {"maroon",       0x800000}, {"olive",        0x808000},
    {"aqua",         0x00ffff}, {"azure",        0xf0ffff},
    {"ivory",        0xfffff0}, {"lavender",     0xe6e6fa},
    {"chocolate",    0xd2691e}, {"tomato",       0xff6347},
    {"gold",         0xffd700}, {"khaki",        0xf0e68c},
    {"plum",         0xdda0dd}, {"lightgray",    0xd3d3d3},
    {"lightgrey",    0xd3d3d3}, {"darkgray",     0xa9a9a9},
    {"darkgrey",     0xa9a9a9}, {"lightblue",    0xadd8e6},
    {"darkblue",     0x00008b}, {"lightgreen",   0x90ee90},
    {"darkgreen",    0x006400}, {"darkred",      0x8b0000},
    {"wheat",        0xf5deb3}, {"linen",        0xfaf0e6},
    {"mint",         0x98ff98}, {"peach",        0xffdab9},
    {"rose",         0xff007f}, {"sky",          0x87ceeb},
    {"slate",        0x708090}, {"steel",        0x4682b4},
    {"hotpink",      0xff69b4}, {"deeppink",     0xff1493},
    {"orchid",       0xda70d6}, {"turquoise",    0x40e0d0},
    {"seagreen",     0x2e8b57}, {"forestgreen",  0x228b22},
    {"firebrick",    0xb22222}, {"sienna",       0xa0522d},
    {"peru",         0xcd853f}, {"sandybrown",   0xf4a460},
    {"burlywood",    0xdeb887}, {"moccasin",     0xffe4b5},
    {"mistyrose",    0xffe4e1}, {"aliceblue",    0xf0f8ff},
    {"honeydew",     0xf0fff0}, {"mintcream",    0xf5fffa},
    {"snow",         0xfffafa}, {"ghostwhite",   0xf8f8ff},
    {"transparent",  0x000000}, /* special: use opacity=0 */
    {NULL, 0}
};

static uint32_t parse_rgb(const char *s) {
    /* rgb(r,g,b) or rgb(r, g, b) */
    const char *p = s + 3;
    if (*p == 'a') p++;   /* rgba */
    if (*p == '(') p++;
    int r = atoi(p);
    while (*p && *p != ',') p++;
    if (*p) p++;
    int g = atoi(p);
    while (*p && *p != ',') p++;
    if (*p) p++;
    int b = atoi(p);
    return (uint32_t)(((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff));
}

static uint32_t parse_color_str(const char *s) {
    if (!s || !*s) return 0x000000;

    /* rgb() / rgba() */
    if ((s[0] == 'r' && s[1] == 'g' && s[2] == 'b'))
        return parse_rgb(s);

    /* hex: #rrggbb or #rgb */
    if (s[0] == '#') {
        s++;
        size_t len = strlen(s);
        unsigned long v = strtoul(s, NULL, 16);
        if (len == 3) {
            int r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
            return (uint32_t)((r*17 << 16) | (g*17 << 8) | (b*17));
        }
        if (len == 4) { /* #rgba → ignore alpha */
            int r = (v >> 12) & 0xf, g = (v >> 8) & 0xf, b = (v >> 4) & 0xf;
            return (uint32_t)((r*17 << 16) | (g*17 << 8) | (b*17));
        }
        return (uint32_t)(v & 0xffffff);
    }

    /* named color */
    for (const NamedColor *nc = s_colors; nc->name; nc++) {
        if (strcmp(s, nc->name) == 0) return nc->hex;
    }
    return 0x000000;
}

/* ------------------------------------------------------------------ CSS variable lookup */

const char *pss_var_get(const PssTheme *t, const char *name) {
    for (int i = 0; i < t->var_count; i++)
        if (strcmp(t->vars[i][0], name) == 0)
            return t->vars[i][1];
    return NULL;
}

/* Resolve a value token — if it's var(--name) substitute from table */
static uint32_t resolve_color(const PssTheme *t, const char *tok) {
    if (strncmp(tok, "var(", 4) == 0) {
        /* extract --name */
        const char *name = tok + 4;
        while (*name == ' ' || *name == '-') {
            if (name[0] == '-' && name[1] == '-') { name += 2; break; }
            name++;
        }
        char varname[128] = {0};
        int i = 0;
        while (*name && *name != ')' && i < 127) varname[i++] = *name++;
        const char *val = pss_var_get(t, varname);
        if (val) return parse_color_str(val);
        return 0x000000;
    }
    return parse_color_str(tok);
}

/* ------------------------------------------------------------------ apply property to a style */

static void apply_property(PssTheme *t, PssStyle *s, const char *prop, const char *src) {
    char tok1[256], tok2[256];
    const char *p = src;

    p = read_value_token(p, tok1, sizeof(tok1));

    /* ── Color properties ─────────────────────────────────── */
    if (strcmp(prop, "background") == 0 || strcmp(prop, "background-color") == 0) {
        s->background = resolve_color(t, tok1);
        if (strcmp(tok1, "transparent") == 0) s->opacity = 0;
    } else if (strcmp(prop, "color") == 0) {
        s->color = resolve_color(t, tok1);
    } else if (strcmp(prop, "border-color") == 0) {
        s->border_color = resolve_color(t, tok1);
    } else if (strcmp(prop, "outline-color") == 0) {
        s->outline_color = resolve_color(t, tok1);
    } else if (strcmp(prop, "shadow-color") == 0) {
        s->shadow_color = resolve_color(t, tok1);
    } else if (strcmp(prop, "accent") == 0 || strcmp(prop, "accent-color") == 0) {
        s->accent_color = resolve_color(t, tok1);

    /* ── Border ───────────────────────────────────────────── */
    } else if (strcmp(prop, "border-radius") == 0) {
        s->border_radius = atoi(tok1);
    } else if (strcmp(prop, "border-width") == 0) {
        s->border_width = atoi(tok1);
    } else if (strcmp(prop, "outline-width") == 0) {
        s->outline_width = atoi(tok1);
    } else if (strcmp(prop, "border") == 0) {
        /* border: width [color] */
        s->border_width = atoi(tok1);
        p = skip_ws(p);
        p = read_value_token(p, tok2, sizeof(tok2));
        if (tok2[0]) s->border_color = resolve_color(t, tok2);
    } else if (strcmp(prop, "outline") == 0) {
        s->outline_width = atoi(tok1);
        p = skip_ws(p);
        p = read_value_token(p, tok2, sizeof(tok2));
        if (tok2[0]) s->outline_color = resolve_color(t, tok2);
    } else if (strcmp(prop, "border-top") == 0) {
        s->border_top    = atoi(tok1);
    } else if (strcmp(prop, "border-right") == 0) {
        s->border_right  = atoi(tok1);
    } else if (strcmp(prop, "border-bottom") == 0) {
        s->border_bottom = atoi(tok1);
    } else if (strcmp(prop, "border-left") == 0) {
        s->border_left   = atoi(tok1);

    /* ── Spacing ───────────────────────────────────────────── */
    } else if (strcmp(prop, "padding") == 0) {
        /* padding: N  or  padding: N N  (vertical horizontal) */
        s->padding_y = atoi(tok1);
        p = skip_ws(p);
        read_value_token(p, tok2, sizeof(tok2));
        s->padding_x = tok2[0] ? atoi(tok2) : s->padding_y;
    } else if (strcmp(prop, "padding-x") == 0 || strcmp(prop, "padding-left") == 0 || strcmp(prop, "padding-right") == 0) {
        s->padding_x = atoi(tok1);
    } else if (strcmp(prop, "padding-y") == 0 || strcmp(prop, "padding-top") == 0 || strcmp(prop, "padding-bottom") == 0) {
        s->padding_y = atoi(tok1);
    } else if (strcmp(prop, "margin") == 0) {
        s->margin_y = atoi(tok1);
        p = skip_ws(p);
        read_value_token(p, tok2, sizeof(tok2));
        s->margin_x = tok2[0] ? atoi(tok2) : s->margin_y;
    } else if (strcmp(prop, "margin-x") == 0 || strcmp(prop, "margin-left") == 0 || strcmp(prop, "margin-right") == 0) {
        s->margin_x = atoi(tok1);
    } else if (strcmp(prop, "margin-y") == 0 || strcmp(prop, "margin-top") == 0 || strcmp(prop, "margin-bottom") == 0) {
        s->margin_y = atoi(tok1);

    /* ── Size ─────────────────────────────────────────────── */
    } else if (strcmp(prop, "min-width") == 0) {
        s->min_width  = atoi(tok1);
    } else if (strcmp(prop, "min-height") == 0) {
        s->min_height = atoi(tok1);
    } else if (strcmp(prop, "max-width") == 0) {
        s->max_width  = atoi(tok1);
    } else if (strcmp(prop, "max-height") == 0) {
        s->max_height = atoi(tok1);

    /* ── Typography ───────────────────────────────────────── */
    } else if (strcmp(prop, "font-size") == 0) {
        s->font_size = atoi(tok1);
    } else if (strcmp(prop, "font") == 0 || strcmp(prop, "font-family") == 0) {
        const char *name = tok1;
        if (*name == '"' || *name == '\'') name++;
        strncpy(s->font, name, sizeof(s->font) - 1);
        s->font[sizeof(s->font) - 1] = '\0';
        size_t l = strlen(s->font);
        if (l > 0 && (s->font[l-1] == '"' || s->font[l-1] == '\'')) s->font[l-1] = '\0';
    } else if (strcmp(prop, "font-weight") == 0) {
        if (strcmp(tok1, "bold")   == 0) s->font_weight = 700;
        else if (strcmp(tok1, "normal") == 0) s->font_weight = 400;
        else s->font_weight = atoi(tok1);
    } else if (strcmp(prop, "font-style") == 0) {
        s->font_italic = (strcmp(tok1, "italic") == 0) ? 1 : 0;
    } else if (strcmp(prop, "line-height") == 0) {
        s->line_height = atoi(tok1);
    } else if (strcmp(prop, "letter-spacing") == 0) {
        s->letter_spacing = atoi(tok1);
    } else if (strcmp(prop, "text-align") == 0) {
        if (strcmp(tok1, "center") == 0)      s->text_align = 1;
        else if (strcmp(tok1, "right") == 0)  s->text_align = 2;
        else                                   s->text_align = 0;
    } else if (strcmp(prop, "text-decoration") == 0) {
        if (strcmp(tok1, "underline") == 0)      s->text_decoration = 1;
        else if (strcmp(tok1, "line-through") == 0) s->text_decoration = 2;
        else                                         s->text_decoration = 0;

    /* ── Effects ──────────────────────────────────────────── */
    } else if (strcmp(prop, "opacity") == 0) {
        s->opacity = atoi(tok1);
    } else if (strcmp(prop, "shadow") == 0) {
        /* shadow: offset-x offset-y [blur] [color] */
        s->shadow_offset_x = atoi(tok1);
        p = skip_ws(p);
        p = read_value_token(p, tok2, sizeof(tok2));
        s->shadow_offset_y = tok2[0] ? atoi(tok2) : 0;
        p = skip_ws(p);
        p = read_value_token(p, tok2, sizeof(tok2));
        if (tok2[0] && (isdigit((unsigned char)tok2[0]) || tok2[0] == '-')) {
            s->shadow_blur = atoi(tok2);
            p = skip_ws(p);
            p = read_value_token(p, tok2, sizeof(tok2));
            if (tok2[0]) s->shadow_color = resolve_color(t, tok2);
        } else if (tok2[0]) {
            s->shadow_color = resolve_color(t, tok2);
        }
    } else if (strcmp(prop, "shadow-blur") == 0) {
        s->shadow_blur = atoi(tok1);
    } else if (strcmp(prop, "shadow-x") == 0 || strcmp(prop, "shadow-offset-x") == 0) {
        s->shadow_offset_x = atoi(tok1);
    } else if (strcmp(prop, "shadow-y") == 0 || strcmp(prop, "shadow-offset-y") == 0) {
        s->shadow_offset_y = atoi(tok1);

    /* ── Misc ─────────────────────────────────────────────── */
    } else if (strcmp(prop, "cursor") == 0) {
        strncpy(s->cursor, tok1, sizeof(s->cursor) - 1);
        s->cursor[sizeof(s->cursor) - 1] = '\0';
    }
    /* Unknown properties are silently ignored for forward-compatibility */
    (void)t;
}

/* ------------------------------------------------------------------ selector → style pointer */

typedef struct { const char *sel; const char *pseudo; int offset; } SelectorMap;

#define SMAP(sel, pseudo, field) \
    { sel, pseudo, (int)offsetof(PssTheme, field) }

#include <stddef.h>

static const SelectorMap s_map[] = {
    SMAP("window",        "",         window),
    SMAP("label",         "",         label),
    SMAP("title",         "",         title),
    SMAP("subtitle",      "",         subtitle),
    SMAP("text",          "",         text),
    SMAP("button",        "",         button),
    SMAP("button",        "hover",    button_hover),
    SMAP("button",        "active",   button_active),
    SMAP("button",        "disabled", button_disabled),
    SMAP("input",         "",         input),
    SMAP("input",         "focus",    input_focus),
    SMAP("input",         "disabled", input_disabled),
    SMAP("textarea",      "",         textarea),
    SMAP("textarea",      "focus",    textarea_focus),
    SMAP("checkbox",      "",         checkbox),
    SMAP("checkbox",      "checked",  checkbox_checked),
    SMAP("checkbox",      "disabled", checkbox_disabled),
    SMAP("progressbar",   "",         progressbar),
    SMAP("progressbar",   "fill",     progressbar_fill),
    SMAP("scrollbar",     "",         scrollbar),
    SMAP("scrollbar",     "thumb",    scrollbar_thumb),
    SMAP("scrollbar",     "hover",    scrollbar_thumb_hover),
    SMAP("header",        "",         header),
    SMAP("sidebar",       "",         sidebar),
    SMAP("panel",         "",         panel),
    SMAP("card",          "",         card),
    SMAP("separator",     "",         separator),
    SMAP("overlay",       "",         overlay),
    SMAP("dialog",        "",         dialog),
    SMAP("list",          "",         list),
    SMAP("list-item",     "",         list_item),
    SMAP("list-item",     "hover",    list_item_hover),
    SMAP("list-item",     "selected", list_item_selected),
    SMAP("menu",          "",         menu),
    SMAP("menu-item",     "",         menu_item),
    SMAP("menu-item",     "hover",    menu_item_hover),
    SMAP("tab",           "",         tab),
    SMAP("tab",           "active",   tab_active),
    SMAP("tab",           "hover",    tab_hover),
    SMAP("link",          "",         link),
    SMAP("link",          "hover",    link_hover),
    SMAP("link",          "visited",  link_visited),
    SMAP("tooltip",       "",         tooltip),
    SMAP("badge",         "",         badge),
    SMAP("badge",         "success",  badge_success),
    SMAP("badge",         "warning",  badge_warning),
    SMAP("badge",         "error",    badge_error),
    {NULL, NULL, 0}
};

static PssStyle *resolve_selector(PssTheme *t, const char *sel, const char *pseudo) {
    for (const SelectorMap *m = s_map; m->sel; m++) {
        if (strcmp(m->sel, sel) == 0 && strcmp(m->pseudo, pseudo) == 0)
            return (PssStyle *)((char *)t + m->offset);
    }
    return NULL;
}

/* ------------------------------------------------------------------ parser */

bool pss_theme_load(PssTheme *t, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *src = malloc(sz + 1);
    if (!src) { fclose(f); return false; }
    size_t nr = fread(src, 1, sz, f);
    src[nr] = '\0';
    fclose(f);

    const char *p = src;
    char selector[64], pseudo[64], prop[64], val[512];

    while (*p) {
        p = skip_ws_comments(p);
        if (!*p) break;

        /* ── CSS custom property block: :root { --name: value; } ── */
        if (*p == ':') {
            p++;
            char kw[32];
            p = read_word(p, kw, sizeof(kw));
            p = skip_ws_comments(p);
            if (*p == '{') p++;
            while (*p) {
                p = skip_ws_comments(p);
                if (!*p || *p == '}') break;
                if (p[0] == '-' && p[1] == '-') {
                    /* --varname: value; */
                    p += 2;
                    char varname[128] = {0};
                    int vi = 0;
                    while (*p && *p != ':' && vi < 127) varname[vi++] = *p++;
                    if (*p == ':') p++;
                    p = skip_ws(p);
                    char varval[256] = {0};
                    int vvi = 0;
                    while (*p && *p != ';' && *p != '}' && vvi < 255) varval[vvi++] = *p++;
                    if (*p == ';') p++;
                    /* store in table */
                    if (t->var_count < PSS_VAR_MAX) {
                        /* trim trailing whitespace from varname */
                        int nl = (int)strlen(varname);
                        while (nl > 0 && isspace((unsigned char)varname[nl-1])) varname[--nl] = '\0';
                        { size_t n_ = strnlen(varname, 127); memcpy(t->vars[t->var_count][0], varname, n_); t->vars[t->var_count][0][n_] = '\0'; }
                        { size_t n_ = strnlen(varval,  127); memcpy(t->vars[t->var_count][1], varval,  n_); t->vars[t->var_count][1][n_] = '\0'; }
                        t->var_count++;
                    }
                } else {
                    /* skip unknown :root content */
                    while (*p && *p != ';' && *p != '}') p++;
                    if (*p == ';') p++;
                }
            }
            if (*p == '}') p++;
            continue;
        }

        /* Read selector word */
        p = read_word(p, selector, sizeof(selector));
        if (!selector[0]) { p++; continue; }

        p = skip_ws(p);
        pseudo[0] = '\0';
        if (*p == ':') {
            p++;
            p = read_word(p, pseudo, sizeof(pseudo));
        }

        p = skip_ws_comments(p);
        if (*p != '{') { /* malformed — skip line */
            while (*p && *p != '\n') p++;
            continue;
        }
        p++; /* consume '{' */

        PssStyle *style = resolve_selector(t, selector, pseudo);

        /* Parse properties until '}' */
        while (*p) {
            p = skip_ws_comments(p);
            if (!*p || *p == '}') break;

            /* CSS custom property inside a rule block */
            if (p[0] == '-' && p[1] == '-') {
                p += 2;
                while (*p && *p != ';' && *p != '}') p++;
                if (*p == ';') p++;
                continue;
            }

            p = read_word(p, prop, sizeof(prop));
            if (!prop[0]) { p++; continue; }

            p = skip_ws(p);
            if (*p == ':') p++;
            p = skip_ws(p);

            /* Read full value (until ';' or '}') */
            int vi = 0;
            while (*p && *p != ';' && *p != '}' && vi < (int)sizeof(val)-1)
                val[vi++] = *p++;
            val[vi] = '\0';
            /* trim trailing whitespace from val */
            while (vi > 0 && isspace((unsigned char)val[vi-1])) val[--vi] = '\0';
            if (*p == ';') p++;

            if (style) apply_property(t, style, prop, val);
        }
        if (*p == '}') p++;
    }

    free(src);
    return true;
}
