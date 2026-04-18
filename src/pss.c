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

    /* ── Toggle switch ───────────────────────────────────── */
    style_default_base(&t->toggle,    0xcccccc, 0xffffff);
    t->toggle.border_radius = 12;
    t->toggle.min_width     = 44;
    t->toggle.min_height    = 24;
    snprintf(t->toggle.cursor, sizeof(t->toggle.cursor), "pointer");

    t->toggle_on             = t->toggle;
    t->toggle_on.background  = 0x0078d4;

    t->toggle_off            = t->toggle;
    t->toggle_off.background = 0xcccccc;

    /* ── Slider ──────────────────────────────────────────── */
    style_default_base(&t->slider,       0xe0e0e0, 0x000000);
    t->slider.border_radius = 3;
    t->slider.min_height    = 6;

    style_default_base(&t->slider_track, 0xe0e0e0, 0x000000);
    t->slider_track.border_radius = 3;
    t->slider_track.min_height    = 6;

    style_default_base(&t->slider_thumb, 0x0078d4, 0xffffff);
    t->slider_thumb.border_radius = 10;
    t->slider_thumb.min_width     = 20;
    t->slider_thumb.min_height    = 20;
    snprintf(t->slider_thumb.cursor, sizeof(t->slider_thumb.cursor), "pointer");

    /* ── Select / dropdown ───────────────────────────────── */
    style_default_base(&t->select,            0xffffff, 0x222222);
    t->select.border_color  = 0xbcbcbc;
    t->select.border_width  = 1;
    t->select.border_radius = 4;
    t->select.padding_x     = 10;
    t->select.padding_y     = 6;
    snprintf(t->select.cursor, sizeof(t->select.cursor), "pointer");

    t->select_open            = t->select;
    t->select_open.border_color = 0x0078d4;
    t->select_open.border_width = 2;

    style_default_base(&t->select_item,       0xffffff, 0x222222);
    t->select_item.padding_x = 10;
    t->select_item.padding_y = 7;

    style_default_base(&t->select_item_hover, 0xf0f5ff, 0x0078d4);
    t->select_item_hover.padding_x = 10;
    t->select_item_hover.padding_y = 7;

    /* ── Chip ─────────────────────────────────────────────── */
    style_default_base(&t->chip,        0xe8e8e8, 0x444444);
    t->chip.border_radius = 12;
    t->chip.padding_x     = 10;
    t->chip.padding_y     = 4;
    t->chip.font_size     = 12;
    snprintf(t->chip.cursor, sizeof(t->chip.cursor), "pointer");

    t->chip_hover          = t->chip;
    t->chip_hover.background = 0xd8d8d8;

    t->chip_active         = t->chip;
    t->chip_active.background = 0x0078d4;
    t->chip_active.color      = 0xffffff;

    /* ── Spinner ──────────────────────────────────────────── */
    style_default_base(&t->spinner,     0x0078d4, 0x0078d4);
    t->spinner.min_width  = 32;
    t->spinner.min_height = 32;
    t->spinner.border_width  = 3;
    t->spinner.border_color  = 0xe0e0e0;
    t->spinner.border_radius = 16;

    /* ── Section divider ──────────────────────────────────── */
    style_default_base(&t->section,     0xfafafa, 0x555555);
    t->section.font_weight = 600;
    t->section.font_size   = 13;
    t->section.padding_y   = 4;
    t->section.margin_y    = 8;
    t->section.border_bottom = 1;
    t->section.border_color  = 0xdddddd;

    /* ── Group box ────────────────────────────────────────── */
    style_default_base(&t->group,       0xfafafa, 0x333333);
    t->group.border_color  = 0xdddddd;
    t->group.border_width  = 1;
    t->group.border_radius = 6;
    t->group.padding_x     = 12;
    t->group.padding_y     = 10;

    style_default_base(&t->group_title, 0xfafafa, 0x555555);
    t->group_title.font_weight = 600;
    t->group_title.font_size   = 12;
    t->group_title.padding_y   = 2;

    /* ── Toast ────────────────────────────────────────────── */
    style_default_base(&t->toast,         0x333333, 0xffffff);
    t->toast.border_radius = 6;
    t->toast.padding_x     = 14;
    t->toast.padding_y     = 10;
    t->toast.shadow_blur   = 8;
    t->toast.shadow_color  = 0x000000;
    t->toast.font_size     = 13;

    t->toast_success        = t->toast;
    t->toast_success.background = 0x28a745;

    t->toast_warning        = t->toast;
    t->toast_warning.background = 0xffc107;
    t->toast_warning.color      = 0x333333;

    t->toast_error          = t->toast;
    t->toast_error.background = 0xdc3545;

    /* ── Radio button ─────────────────────────────────────── */
    style_default_base(&t->radio,          0xffffff, 0x222222);
    t->radio.border_color  = 0xbcbcbc;
    t->radio.border_width  = 2;
    t->radio.border_radius = 10;
    t->radio.min_width     = 18;
    t->radio.min_height    = 18;
    snprintf(t->radio.cursor, sizeof(t->radio.cursor), "pointer");

    t->radio_checked        = t->radio;
    t->radio_checked.background  = 0xffffff;
    t->radio_checked.border_color = 0x0078d4;
    t->radio_checked.border_width = 2;
    t->radio_checked.accent_color = 0x0078d4;

    t->radio_disabled       = t->radio;
    t->radio_disabled.background   = 0xeeeeee;
    t->radio_disabled.border_color = 0xcccccc;
    snprintf(t->radio_disabled.cursor, sizeof(t->radio_disabled.cursor), "not-allowed");

    /* ── Menu bar ─────────────────────────────────────────── */
    style_default_base(&t->menu_bar,              0xf4f4f4, 0x333333);
    t->menu_bar.border_bottom = 1;
    t->menu_bar.border_color  = 0xe0e0e0;
    t->menu_bar.padding_y     = 4;
    t->menu_bar.padding_x     = 4;

    style_default_base(&t->menu_bar_item,         0xf4f4f4, 0x333333);
    t->menu_bar_item.padding_x = 12;
    t->menu_bar_item.padding_y = 6;
    t->menu_bar_item.border_radius = 4;
    snprintf(t->menu_bar_item.cursor, sizeof(t->menu_bar_item.cursor), "pointer");

    style_default_base(&t->menu_bar_item_hover,   0xe8e8e8, 0x222222);
    t->menu_bar_item_hover.padding_x = 12;
    t->menu_bar_item_hover.padding_y = 6;
    t->menu_bar_item_hover.border_radius = 4;

    style_default_base(&t->menu_bar_item_active,  0x0078d4, 0xffffff);
    t->menu_bar_item_active.padding_x = 12;
    t->menu_bar_item_active.padding_y = 6;
    t->menu_bar_item_active.border_radius = 4;

    /* ── Context menu ─────────────────────────────────────── */
    style_default_base(&t->context_menu,           0xffffff, 0x333333);
    t->context_menu.border_color  = 0xdddddd;
    t->context_menu.border_width  = 1;
    t->context_menu.border_radius = 6;
    t->context_menu.shadow_color  = 0x000000;
    t->context_menu.shadow_blur   = 12;
    t->context_menu.shadow_offset_y = 4;
    t->context_menu.padding_y     = 4;

    style_default_base(&t->context_menu_item,      0xffffff, 0x333333);
    t->context_menu_item.padding_x = 14;
    t->context_menu_item.padding_y = 8;
    t->context_menu_item.font_size = 13;
    snprintf(t->context_menu_item.cursor, sizeof(t->context_menu_item.cursor), "pointer");

    style_default_base(&t->context_menu_item_hover, 0xf0f5ff, 0x0078d4);
    t->context_menu_item_hover.padding_x = 14;
    t->context_menu_item_hover.padding_y = 8;
    t->context_menu_item_hover.font_size = 13;

    /* ── Table ────────────────────────────────────────────── */
    style_default_base(&t->table,             0xffffff, 0x333333);
    t->table.border_color  = 0xdddddd;
    t->table.border_width  = 1;
    t->table.border_radius = 4;

    style_default_base(&t->table_header,      0xf4f4f4, 0x444444);
    t->table_header.font_weight = 600;
    t->table_header.font_size   = 12;
    t->table_header.padding_x   = 12;
    t->table_header.padding_y   = 8;
    t->table_header.border_bottom = 2;
    t->table_header.border_color  = 0xcccccc;

    style_default_base(&t->table_row,         0xffffff, 0x333333);
    t->table_row.padding_x = 12;
    t->table_row.padding_y = 8;
    t->table_row.border_bottom = 1;
    t->table_row.border_color  = 0xeeeeee;

    style_default_base(&t->table_row_alt,     0xfafafa, 0x333333);
    t->table_row_alt.padding_x = 12;
    t->table_row_alt.padding_y = 8;
    t->table_row_alt.border_bottom = 1;
    t->table_row_alt.border_color  = 0xeeeeee;

    style_default_base(&t->table_row_hover,   0xf0f5ff, 0x333333);
    t->table_row_hover.padding_x = 12;
    t->table_row_hover.padding_y = 8;

    style_default_base(&t->table_row_selected, 0xddeeff, 0x0078d4);
    t->table_row_selected.padding_x = 12;
    t->table_row_selected.padding_y = 8;
    t->table_row_selected.font_weight = 600;

    style_default_base(&t->table_cell,        0xffffff, 0x333333);
    t->table_cell.padding_x = 12;
    t->table_cell.padding_y = 8;

    /* ── Tree view ────────────────────────────────────────── */
    style_default_base(&t->tree,              0xffffff, 0x333333);
    t->tree.padding_x = 4;

    style_default_base(&t->tree_node,         0xffffff, 0x333333);
    t->tree_node.padding_x = 8;
    t->tree_node.padding_y = 5;
    t->tree_node.border_radius = 4;
    snprintf(t->tree_node.cursor, sizeof(t->tree_node.cursor), "pointer");

    style_default_base(&t->tree_node_expanded, 0xf5f5f5, 0x333333);
    t->tree_node_expanded.padding_x = 8;
    t->tree_node_expanded.padding_y = 5;
    t->tree_node_expanded.border_radius = 4;
    t->tree_node_expanded.font_weight   = 600;

    style_default_base(&t->tree_node_selected, 0x0078d4, 0xffffff);
    t->tree_node_selected.padding_x = 8;
    t->tree_node_selected.padding_y = 5;
    t->tree_node_selected.border_radius = 4;
    t->tree_node_selected.font_weight   = 600;

    /* ── Collapsing section ───────────────────────────────── */
    style_default_base(&t->collapsing,      0xf8f8f8, 0x333333);
    t->collapsing.border_radius = 6;
    t->collapsing.padding_x     = 12;
    t->collapsing.padding_y     = 8;
    t->collapsing.border_color  = 0xeeeeee;
    t->collapsing.border_width  = 1;
    snprintf(t->collapsing.cursor, sizeof(t->collapsing.cursor), "pointer");

    t->collapsing_open          = t->collapsing;
    t->collapsing_open.background = 0xf0f5ff;
    t->collapsing_open.border_color = 0x0078d4;
    t->collapsing_open.font_weight  = 600;

    /* ── Modal / dialog ───────────────────────────────────── */
    style_default_base(&t->modal,           0xffffff, 0x333333);
    t->modal.border_radius = 10;
    t->modal.padding_x     = 24;
    t->modal.padding_y     = 24;
    t->modal.shadow_color  = 0x000000;
    t->modal.shadow_blur   = 32;
    t->modal.shadow_offset_y = 8;

    style_default_base(&t->modal_overlay,   0x000000, 0x000000);
    t->modal_overlay.opacity = 55;

    style_default_base(&t->modal_title,     0xffffff, 0x111111);
    t->modal_title.font_size   = 18;
    t->modal_title.font_weight = 700;
    t->modal_title.padding_y   = 4;
    t->modal_title.border_bottom = 1;
    t->modal_title.border_color  = 0xeeeeee;

    /* ── Spinbox ──────────────────────────────────────────── */
    style_default_base(&t->spinbox,             0xffffff, 0x222222);
    t->spinbox.border_color  = 0xbcbcbc;
    t->spinbox.border_width  = 1;
    t->spinbox.border_radius = 4;
    t->spinbox.padding_x     = 8;
    t->spinbox.padding_y     = 6;

    style_default_base(&t->spinbox_button,      0xf4f4f4, 0x444444);
    t->spinbox_button.border_color  = 0xbcbcbc;
    t->spinbox_button.border_width  = 1;
    t->spinbox_button.border_radius = 4;
    t->spinbox_button.font_weight   = 700;
    t->spinbox_button.min_width     = 26;
    snprintf(t->spinbox_button.cursor, sizeof(t->spinbox_button.cursor), "pointer");

    style_default_base(&t->spinbox_button_hover, 0xe8e8e8, 0x0078d4);
    t->spinbox_button_hover.border_color  = 0x0078d4;
    t->spinbox_button_hover.border_width  = 1;
    t->spinbox_button_hover.border_radius = 4;
    t->spinbox_button_hover.font_weight   = 700;
    t->spinbox_button_hover.min_width     = 26;
    snprintf(t->spinbox_button_hover.cursor, sizeof(t->spinbox_button_hover.cursor), "pointer");

    /* ── Status bar ───────────────────────────────────────── */
    style_default_base(&t->status_bar,      0xf0f0f0, 0x666666);
    t->status_bar.border_top  = 1;
    t->status_bar.border_color = 0xdddddd;
    t->status_bar.padding_x   = 12;
    t->status_bar.padding_y   = 4;
    t->status_bar.font_size   = 12;
    t->status_bar.min_height  = 24;

    /* ── Splitter ─────────────────────────────────────────── */
    style_default_base(&t->splitter,             0xeeeeee, 0x000000);
    t->splitter.min_width  = 6;
    t->splitter.min_height = 6;

    style_default_base(&t->splitter_handle,      0xdddddd, 0x000000);
    t->splitter_handle.border_radius = 3;
    snprintf(t->splitter_handle.cursor, sizeof(t->splitter_handle.cursor), "pointer");

    style_default_base(&t->splitter_handle_hover, 0x0078d4, 0x000000);
    t->splitter_handle_hover.border_radius = 3;
    snprintf(t->splitter_handle_hover.cursor, sizeof(t->splitter_handle_hover.cursor), "pointer");

    /* ── Icon button ──────────────────────────────────────── */
    style_default_base(&t->icon_button,       0xf4f4f4, 0x444444);
    t->icon_button.border_radius = 6;
    t->icon_button.padding_x     = 8;
    t->icon_button.padding_y     = 8;
    snprintf(t->icon_button.cursor, sizeof(t->icon_button.cursor), "pointer");

    style_default_base(&t->icon_button_hover, 0xe8e8e8, 0x0078d4);
    t->icon_button_hover.border_radius = 6;
    t->icon_button_hover.padding_x     = 8;
    t->icon_button_hover.padding_y     = 8;

    style_default_base(&t->icon_button_active, 0xddeeff, 0x0078d4);
    t->icon_button_active.border_radius = 6;
    t->icon_button_active.padding_x     = 8;
    t->icon_button_active.padding_y     = 8;

    /* ── Scroll area ──────────────────────────────────────── */
    style_default_base(&t->scroll_area,     0xfafafa, 0x333333);
    t->scroll_area.border_color  = 0xdddddd;
    t->scroll_area.border_width  = 1;
    t->scroll_area.border_radius = 4;

    /* ── Drag handle ──────────────────────────────────────── */
    style_default_base(&t->drag_control,      0xe8e8e8, 0x666666);
    t->drag_control.border_radius = 4;
    t->drag_control.padding_x     = 6;
    t->drag_control.padding_y     = 4;
    snprintf(t->drag_control.cursor, sizeof(t->drag_control.cursor), "pointer");

    style_default_base(&t->drag_control_hover, 0xddeeff, 0x0078d4);
    t->drag_control_hover.border_radius = 4;
    t->drag_control_hover.padding_x     = 6;
    t->drag_control_hover.padding_y     = 4;
    snprintf(t->drag_control_hover.cursor, sizeof(t->drag_control_hover.cursor), "pointer");
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
    /* rgb(r,g,b) or rgba(r,g,b,a) */
    const char *p = s + 3;
    if (*p == 'a') p++;
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

/* hsl(h, s%, l%) → 0xRRGGBB
   Uses the standard HSL-to-RGB conversion formula. */
static uint32_t parse_hsl(const char *s) {
    const char *p = s + 3;
    if (*p == '(') p++;
    float h = (float)atof(p);
    while (*p && *p != ',') p++;
    if (*p) p++;
    float sv = (float)atof(p);
    while (*p && *p != ',') p++;
    if (*p) p++;
    float lv = (float)atof(p);
    /* Normalize h to [0,1], s and l from % to [0,1] */
    h  = h / 360.0f;
    sv = sv / 100.0f;
    lv = lv / 100.0f;
    if (h < 0.0f) h += 1.0f;
    if (h > 1.0f) h -= 1.0f;
    float q = lv < 0.5f ? lv * (1.0f + sv) : lv + sv - lv * sv;
    float p2 = 2.0f * lv - q;
    /* HUE_TO_RGB helper via lambda-style macro */
#define HUE_TO_RGB(t) ( \
    (t) < 0.0f ? (t) + 1.0f : \
    (t) > 1.0f ? (t) - 1.0f : (t) )
    float tr = HUE_TO_RGB(h + 1.0f/3.0f);
    float tg = HUE_TO_RGB(h);
    float tb = HUE_TO_RGB(h - 1.0f/3.0f);
#define HSL_COMP(tc) ( \
    (tc) < 1.0f/6.0f ? p2 + (q - p2)*6.0f*(tc) : \
    (tc) < 0.5f      ? q : \
    (tc) < 2.0f/3.0f ? p2 + (q - p2)*(2.0f/3.0f - (tc))*6.0f : p2 )
    int ri = (int)(HSL_COMP(tr) * 255.0f + 0.5f);
    int gi = (int)(HSL_COMP(tg) * 255.0f + 0.5f);
    int bi = (int)(HSL_COMP(tb) * 255.0f + 0.5f);
#undef HUE_TO_RGB
#undef HSL_COMP
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    return (uint32_t)((ri << 16) | (gi << 8) | bi);
}

static uint32_t parse_color_str(const char *s) {
    if (!s || !*s) return 0x000000;

    /* hsl() */
    if (s[0] == 'h' && s[1] == 's' && s[2] == 'l')
        return parse_hsl(s);

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

    /* ── Gap ─────────────────────────────────────────────── */
    } else if (strcmp(prop, "gap") == 0 || strcmp(prop, "grid-gap") == 0 ||
               strcmp(prop, "row-gap") == 0 || strcmp(prop, "column-gap") == 0) {
        s->gap = atoi(tok1);

    /* ── Gradient background ──────────────────────────────── */
    } else if (strcmp(prop, "background-image") == 0 ||
               (strcmp(prop, "background") == 0 && strncmp(src, "linear-gradient", 15) == 0)) {
        /* linear-gradient(to bottom|right|..., c1, c2)
           or linear-gradient(angle, c1, c2) — we store start+end+dir */
        const char *lg = strstr(src, "linear-gradient");
        if (lg) {
            const char *pa = strchr(lg, '(');
            if (pa) { pa++; }
            else pa = lg;
            /* skip whitespace and optional direction token */
            pa = skip_ws(pa);
            char dirstr[64] = {0};
            if (strncmp(pa, "to ", 3) == 0) {
                pa += 3; pa = skip_ws(pa);
                int di = 0;
                while (*pa && *pa != ',' && di < 63) dirstr[di++] = *pa++;
                if (*pa == ',') pa++;
                if (strstr(dirstr, "right"))       s->gradient_dir = 1;
                else if (strstr(dirstr, "bottom"))  s->gradient_dir = 0;
                else if (strstr(dirstr, "left"))    s->gradient_dir = 1;
                else if (strstr(dirstr, "top"))     s->gradient_dir = 0;
                else                                s->gradient_dir = 0;
            } else if (isdigit((unsigned char)*pa) || *pa == '-') {
                /* angle: 0=vertical, 90=horizontal */
                float ang = (float)atof(pa);
                while (*pa && *pa != ',') pa++;
                if (*pa) pa++;
                s->gradient_dir = (int)(ang / 90.0f) & 3;
            }
            /* first color */
            pa = skip_ws(pa);
            char c1[64], c2[64];
            int ci = 0;
            while (*pa && *pa != ',' && *pa != ')' && ci < 63) c1[ci++] = *pa++;
            c1[ci] = '\0';
            /* strip trailing whitespace */
            while (ci > 0 && (c1[ci-1] == ' ' || c1[ci-1] == '\t')) c1[--ci] = '\0';
            if (*pa == ',') pa++;
            pa = skip_ws(pa);
            ci = 0;
            while (*pa && *pa != ')' && ci < 63) c2[ci++] = *pa++;
            c2[ci] = '\0';
            while (ci > 0 && (c2[ci-1] == ' ' || c2[ci-1] == '\t')) c2[--ci] = '\0';
            s->background    = resolve_color(t, c1);
            s->gradient_end  = resolve_color(t, c2);
        }

    /* ── Multi-layer box-shadow ───────────────────────────── */
    } else if (strcmp(prop, "box-shadow") == 0) {
        /* box-shadow: x y blur color [, x y blur color] ...
           We parse up to PSS_MAX_SHADOWS layers separated by commas.
           Each layer: "x y blur color"  or "x y color" (blur optional) */
        const char *pp = src;
        s->shadow_count = 0;
        while (*pp && s->shadow_count < PSS_MAX_SHADOWS) {
            pp = skip_ws(pp);
            if (!*pp || *pp == ';') break;
            PssShadowLayer *sl = &s->shadows[s->shadow_count];
            /* x */
            char tv[64];
            pp = read_value_token(pp, tv, sizeof(tv)); sl->x = atoi(tv);
            pp = skip_ws(pp);
            /* y */
            pp = read_value_token(pp, tv, sizeof(tv)); sl->y = atoi(tv);
            pp = skip_ws(pp);
            /* next token is either blur (digit/negative) or color */
            pp = read_value_token(pp, tv, sizeof(tv));
            if (tv[0] && (isdigit((unsigned char)tv[0]) || tv[0] == '-')) {
                sl->blur = atoi(tv);
                pp = skip_ws(pp);
                pp = read_value_token(pp, tv, sizeof(tv));
            }
            sl->color = resolve_color(t, tv);
            s->shadow_count++;
            /* legacy compat: mirror layer 0 into flat fields */
            if (s->shadow_count == 1) {
                s->shadow_offset_x = sl->x;
                s->shadow_offset_y = sl->y;
                s->shadow_blur     = sl->blur;
                s->shadow_color    = sl->color;
            }
            pp = skip_ws(pp);
            if (*pp == ',') pp++;
        }

    /* ── Misc ─────────────────────────────────────────────── */
    } else if (strcmp(prop, "cursor") == 0) {
        strncpy(s->cursor, tok1, sizeof(s->cursor) - 1);
        s->cursor[sizeof(s->cursor) - 1] = '\0';
    }
    /* Unknown properties are silently ignored for forward-compatibility */
    (void)t;
}

/* ------------------------------------------------------------------ calc() evaluator (simple: N op N) */

/* Resolve a px value that may be wrapped in calc(expr). */
__attribute__((unused)) static int resolve_px(const char *tok) {
    /* calc(a + b), calc(a - b), calc(a * b), calc(a / b) */
    if (strncmp(tok, "calc(", 5) == 0) {
        const char *p = tok + 5;
        float a = (float)atof(p);
        while (*p && *p != '+' && *p != '-' && *p != '*' && *p != '/' && *p != ')') p++;
        if (!*p || *p == ')') return (int)a;
        char op = *p++;
        float b = (float)atof(p);
        switch (op) {
            case '+': return (int)(a + b);
            case '-': return (int)(a - b);
            case '*': return (int)(a * b);
            case '/': return b != 0.0f ? (int)(a / b) : (int)a;
        }
    }
    return atoi(tok);
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
    SMAP("badge",                  "",         badge),
    SMAP("badge",                  "success",  badge_success),
    SMAP("badge",                  "warning",  badge_warning),
    SMAP("badge",                  "error",    badge_error),
    /* ── New widgets ─────────────────────────────────────── */
    SMAP("toggle",                 "",         toggle),
    SMAP("toggle",                 "on",       toggle_on),
    SMAP("toggle",                 "off",      toggle_off),
    SMAP("slider",                 "",         slider),
    SMAP("slider",                 "thumb",    slider_thumb),
    SMAP("slider",                 "track",    slider_track),
    SMAP("select",                 "",         select),
    SMAP("select",                 "open",     select_open),
    SMAP("select",                 "item",     select_item),
    SMAP("select-item",            "hover",    select_item_hover),
    SMAP("chip",                   "",         chip),
    SMAP("chip",                   "hover",    chip_hover),
    SMAP("chip",                   "active",   chip_active),
    SMAP("spinner",                "",         spinner),
    SMAP("section",                "",         section),
    SMAP("group",                  "",         group),
    SMAP("group",                  "title",    group_title),
    SMAP("toast",                  "",         toast),
    SMAP("toast",                  "success",  toast_success),
    SMAP("toast",                  "warning",  toast_warning),
    SMAP("toast",                  "error",    toast_error),
    SMAP("radio",                  "",         radio),
    SMAP("radio",                  "checked",  radio_checked),
    SMAP("radio",                  "disabled", radio_disabled),
    SMAP("menu-bar",               "",         menu_bar),
    SMAP("menu-bar-item",          "",         menu_bar_item),
    SMAP("menu-bar-item",          "hover",    menu_bar_item_hover),
    SMAP("menu-bar-item",          "active",   menu_bar_item_active),
    SMAP("context-menu",           "",         context_menu),
    SMAP("context-menu-item",      "",         context_menu_item),
    SMAP("context-menu-item",      "hover",    context_menu_item_hover),
    SMAP("table",                  "",         table),
    SMAP("table",                  "header",   table_header),
    SMAP("table",                  "row",      table_row),
    SMAP("table",                  "row-alt",  table_row_alt),
    SMAP("table",                  "row-hover",table_row_hover),
    SMAP("table",                  "selected", table_row_selected),
    SMAP("table-cell",             "",         table_cell),
    SMAP("tree",                   "",         tree),
    SMAP("tree-node",              "",         tree_node),
    SMAP("tree-node",              "expanded", tree_node_expanded),
    SMAP("tree-node",              "selected", tree_node_selected),
    SMAP("collapsing",             "",         collapsing),
    SMAP("collapsing",             "open",     collapsing_open),
    SMAP("modal",                  "",         modal),
    SMAP("modal",                  "overlay",  modal_overlay),
    SMAP("modal",                  "title",    modal_title),
    SMAP("spinbox",                "",         spinbox),
    SMAP("spinbox-button",         "",         spinbox_button),
    SMAP("spinbox-button",         "hover",    spinbox_button_hover),
    SMAP("status-bar",             "",         status_bar),
    SMAP("splitter",               "",         splitter),
    SMAP("splitter-handle",        "",         splitter_handle),
    SMAP("splitter-handle",        "hover",    splitter_handle_hover),
    SMAP("icon-button",            "",         icon_button),
    SMAP("icon-button",            "hover",    icon_button_hover),
    SMAP("icon-button",            "active",   icon_button_active),
    SMAP("scroll-area",            "",         scroll_area),
    SMAP("drag-control",           "",         drag_control),
    SMAP("drag-control",           "hover",    drag_control_hover),
    {NULL, NULL, 0}
};

static PssStyle *resolve_selector(PssTheme *t, const char *sel, const char *pseudo) {
    for (const SelectorMap *m = s_map; m->sel; m++) {
        if (strcmp(m->sel, sel) == 0 && strcmp(m->pseudo, pseudo) == 0)
            return (PssStyle *)((char *)t + m->offset);
    }
    return NULL;
}

/* ------------------------------------------------------------------ core PSS parser (shared by load and load_str) */

/* Extract directory part from path into dir (for @import resolution).
   Returns length written (0 if path has no directory). */
static int path_dirname(const char *path, char *dir, int dirsz) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    if (!slash) { if (dirsz > 0) dir[0] = '\0'; return 0; }
    int len = (int)(slash - path) + 1;
    if (len >= dirsz) len = dirsz - 1;
    memcpy(dir, path, (size_t)len);
    dir[len] = '\0';
    return len;
}

/* Forward declaration so pss_theme_parse_src can recurse for @import/@theme. */
static void pss_theme_parse_src(PssTheme *t, const char *src,
                                 const char *base_dir, bool dark_only);

/* Parse a full PSS source string into *t.
   base_dir: directory prefix for @import resolution (NULL = current dir).
   dark_only: if true, only process rules inside @theme dark { } blocks. */
static void pss_theme_parse_src(PssTheme *t, const char *src,
                                  const char *base_dir, bool dark_only) {
    const char *p = src;
    char selector[128], pseudo[64], prop[64], val[512];

    while (*p) {
        p = skip_ws_comments(p);
        if (!*p) break;

        /* ── @import "file.pss" ────────────────────────────── */
        if (p[0] == '@' && strncmp(p, "@import", 7) == 0) {
            p += 7; p = skip_ws(p);
            char ipath[512] = {0};
            if (*p == '"') {
                p++;
                int ii = 0;
                while (*p && *p != '"' && ii < 511) ipath[ii++] = *p++;
                if (*p == '"') p++;
            } else {
                p = read_word(p, ipath, sizeof(ipath));
            }
            /* skip trailing ; */
            p = skip_ws(p); if (*p == ';') p++;
            if (ipath[0]) {
                char full[1024];
                if (base_dir && base_dir[0])
                    snprintf(full, sizeof(full), "%s%s", base_dir, ipath);
                else
                    snprintf(full, sizeof(full), "%s", ipath);
                pss_theme_load(t, full);  /* recursive import */
            }
            continue;
        }

        /* ── @theme dark { } / @theme light { } ──────────────── */
        if (p[0] == '@' && strncmp(p, "@theme", 6) == 0) {
            p += 6; p = skip_ws(p);
            char variant[32] = {0};
            p = read_word(p, variant, sizeof(variant));
            p = skip_ws_comments(p);
            if (*p == '{') {
                p++;
                /* collect entire block */
                int depth = 1;
                const char *block_start = p;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
                /* p now points after the closing '}' */
                size_t block_len = (size_t)(p - block_start - 1);
                char *block = malloc(block_len + 1);
                memcpy(block, block_start, block_len);
                block[block_len] = '\0';
                /* Apply if the variant matches or no active theme is set */
                bool should_apply = (t->active_theme[0] == '\0' ||
                                     strcmp(t->active_theme, variant) == 0);
                if (should_apply)
                    pss_theme_parse_src(t, block, base_dir, false);
                free(block);
            }
            continue;
        }

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
                    if (t->var_count < PSS_VAR_MAX) {
                        int nl = (int)strlen(varname);
                        while (nl > 0 && isspace((unsigned char)varname[nl-1])) varname[--nl] = '\0';
                        { size_t n_ = strnlen(varname, 127); memcpy(t->vars[t->var_count][0], varname, n_); t->vars[t->var_count][0][n_] = '\0'; }
                        { size_t n_ = strnlen(varval,  127); memcpy(t->vars[t->var_count][1], varval,  n_); t->vars[t->var_count][1][n_] = '\0'; }
                        t->var_count++;
                    }
                } else {
                    while (*p && *p != ';' && *p != '}') p++;
                    if (*p == ';') p++;
                }
            }
            if (*p == '}') p++;
            continue;
        }

        /* ── Selector ──────────────────────────────────────────── */
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
        p++;

        PssStyle *style = resolve_selector(t, selector, pseudo);

        while (*p) {
            p = skip_ws_comments(p);
            if (!*p || *p == '}') break;

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

            int vi = 0;
            while (*p && *p != ';' && *p != '}' && vi < (int)sizeof(val)-1)
                val[vi++] = *p++;
            val[vi] = '\0';
            while (vi > 0 && isspace((unsigned char)val[vi-1])) val[--vi] = '\0';
            if (*p == ';') p++;

            if (style) apply_property(t, style, prop, val);
        }
        if (*p == '}') p++;
    }
    (void)dark_only;
}

/* ------------------------------------------------------------------ public API */

void pss_theme_load_str(PssTheme *t, const char *src) {
    if (!t || !src) return;
    pss_theme_parse_src(t, src, NULL, false);
}

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

    /* Extract base directory for @import resolution */
    char base_dir[512] = {0};
    path_dirname(path, base_dir, sizeof(base_dir));

    pss_theme_parse_src(t, src, base_dir, false);

    free(src);
    return true;
}
