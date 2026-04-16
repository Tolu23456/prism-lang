#include "pss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ defaults */

void pss_theme_default(PssTheme *t) {
    memset(t, 0, sizeof(*t));

    /* window */
    t->window.background   = 0xf0f0f0;
    t->window.color        = 0x222222;
    t->window.padding_x    = 16;
    t->window.padding_y    = 16;
    t->window.font_size    = 13;
    snprintf(t->window.font, sizeof(t->window.font), "sans");

    /* label */
    t->label.background    = 0xf0f0f0;
    t->label.color         = 0x222222;
    t->label.padding_x     = 0;
    t->label.padding_y     = 2;
    t->label.font_size     = 13;
    snprintf(t->label.font, sizeof(t->label.font), "sans");

    /* button */
    t->button.background   = 0x0078d4;
    t->button.color        = 0xffffff;
    t->button.border_color = 0x005fa3;
    t->button.border_width = 0;
    t->button.border_radius= 5;
    t->button.padding_x    = 18;
    t->button.padding_y    = 8;
    t->button.font_size    = 13;
    snprintf(t->button.font, sizeof(t->button.font), "sans");

    /* button:hover */
    t->button_hover        = t->button;
    t->button_hover.background = 0x106ebe;

    /* input */
    t->input.background    = 0xffffff;
    t->input.color         = 0x222222;
    t->input.border_color  = 0xbcbcbc;
    t->input.border_width  = 1;
    t->input.border_radius = 4;
    t->input.padding_x     = 10;
    t->input.padding_y     = 6;
    t->input.font_size     = 13;
    snprintf(t->input.font, sizeof(t->input.font), "sans");

    /* input:focus */
    t->input_focus         = t->input;
    t->input_focus.border_color = 0x0078d4;
    t->input_focus.border_width = 2;
}

/* ------------------------------------------------------------------ helpers */

static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static const char *skip_ws_comments(const char *p) {
    for (;;) {
        p = skip_ws(p);
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
        } else {
            break;
        }
    }
    return p;
}

/* Read a word (alphanumeric + - _ ) into buf. Returns pointer past word. */
static const char *read_word(const char *p, char *buf, int bufsz) {
    int i = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_') && i < bufsz-1)
        buf[i++] = *p++;
    buf[i] = '\0';
    return p;
}

/* Read a token until whitespace/;/} into buf */
static const char *read_value_token(const char *p, char *buf, int bufsz) {
    p = skip_ws(p);
    int i = 0;
    while (*p && *p != ';' && *p != '}' && *p != '\n' && !isspace((unsigned char)*p) && i < bufsz-1)
        buf[i++] = *p++;
    buf[i] = '\0';
    return p;
}

static uint32_t parse_color(const char *s) {
    if (s[0] == '#') {
        s++;
        size_t len = strlen(s);
        unsigned long v = strtoul(s, NULL, 16);
        if (len == 3) {
            int r = (v >> 8) & 0xf;
            int g = (v >> 4) & 0xf;
            int b =  v       & 0xf;
            return (uint32_t)((r*17 << 16) | (g*17 << 8) | (b*17));
        }
        return (uint32_t)(v & 0xffffff);
    }
    if (strcmp(s, "white")   == 0) return 0xffffff;
    if (strcmp(s, "black")   == 0) return 0x000000;
    if (strcmp(s, "red")     == 0) return 0xff0000;
    if (strcmp(s, "green")   == 0) return 0x00aa00;
    if (strcmp(s, "blue")    == 0) return 0x0000ff;
    if (strcmp(s, "gray")    == 0) return 0x808080;
    if (strcmp(s, "silver")  == 0) return 0xc0c0c0;
    if (strcmp(s, "orange")  == 0) return 0xff8800;
    if (strcmp(s, "yellow")  == 0) return 0xffdd00;
    if (strcmp(s, "purple")  == 0) return 0x800080;
    if (strcmp(s, "pink")    == 0) return 0xff69b4;
    return 0x000000;
}

/* ------------------------------------------------------------------ apply property to a style */

static void apply_property(PssStyle *s, const char *prop, const char *src) {
    char tok1[128], tok2[128];
    const char *p = src;

    p = read_value_token(p, tok1, sizeof(tok1));

    if (strcmp(prop, "background") == 0) {
        s->background = parse_color(tok1);
    } else if (strcmp(prop, "color") == 0) {
        s->color = parse_color(tok1);
    } else if (strcmp(prop, "border-radius") == 0) {
        s->border_radius = atoi(tok1);
    } else if (strcmp(prop, "font-size") == 0) {
        s->font_size = atoi(tok1);
    } else if (strcmp(prop, "font") == 0) {
        /* strip optional quotes */
        const char *name = tok1;
        if (*name == '"' || *name == '\'') name++;
        snprintf(s->font, sizeof(s->font), "%s", name);
        size_t l = strlen(s->font);
        if (l > 0 && (s->font[l-1] == '"' || s->font[l-1] == '\''))
            s->font[l-1] = '\0';
    } else if (strcmp(prop, "border") == 0) {
        /* border: width color */
        s->border_width = atoi(tok1);
        p = skip_ws(p);
        p = read_value_token(p, tok2, sizeof(tok2));
        if (tok2[0]) s->border_color = parse_color(tok2);
    } else if (strcmp(prop, "padding") == 0) {
        /* padding: N  or  padding: N N  (vertical horizontal) */
        s->padding_y = atoi(tok1);
        p = skip_ws(p);
        read_value_token(p, tok2, sizeof(tok2));
        s->padding_x = tok2[0] ? atoi(tok2) : s->padding_y;
    }
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
    char selector[64], pseudo[32], prop[64], val[256];

    while (*p) {
        p = skip_ws_comments(p);
        if (!*p) break;

        /* Read selector, e.g. "button" or "button:hover" */
        p = read_word(p, selector, sizeof(selector));
        if (!selector[0]) { p++; continue; }

        p = skip_ws(p);
        pseudo[0] = '\0';
        if (*p == ':') {
            p++;
            p = read_word(p, pseudo, sizeof(pseudo));
        }

        p = skip_ws_comments(p);
        if (*p != '{') { /* malformed, skip line */
            while (*p && *p != '\n') p++;
            continue;
        }
        p++; /* consume '{' */

        /* Resolve which PssStyle to fill */
        PssStyle *style = NULL;
        if (strcmp(selector, "window") == 0 && !pseudo[0])
            style = &t->window;
        else if (strcmp(selector, "label") == 0 && !pseudo[0])
            style = &t->label;
        else if (strcmp(selector, "button") == 0 && !pseudo[0])
            style = &t->button;
        else if (strcmp(selector, "button") == 0 && strcmp(pseudo, "hover") == 0)
            style = &t->button_hover;
        else if (strcmp(selector, "input") == 0 && !pseudo[0])
            style = &t->input;
        else if (strcmp(selector, "input") == 0 && strcmp(pseudo, "focus") == 0)
            style = &t->input_focus;

        /* Parse properties until '}' */
        while (*p) {
            p = skip_ws_comments(p);
            if (!*p || *p == '}') break;

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
            if (*p == ';') p++;

            if (style) apply_property(style, prop, val);
        }
        if (*p == '}') p++;
    }

    free(src);
    return true;
}
