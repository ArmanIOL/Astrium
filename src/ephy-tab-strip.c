/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2026 ArmanIOL
 *
 *  This file is part of Astrium.
 *
 *  Astrium is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Astrium is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Astrium.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "ephy-tab-strip.h"

#include <glib/gi18n.h>
#include <string.h>

#include "ephy-embed.h"
#include "ephy-web-view.h"

#define MIN_TAB_WIDTH 46
#define MAX_TAB_WIDTH 240
#define PINNED_TAB_WIDTH 44
#define DRAG_THRESHOLD 4

struct _EphyTabStrip {
  AdwBin parent_instance;

  AdwTabView *view;

  GtkWidget *box;
  GtkWidget *window_handle;
  GtkWidget *start_action_widget;
  GtkWidget *scrolled_window;
  GtkWidget *tabs_box;
  GtkWidget *new_tab_button;
  GtkWidget *tab_search_button;
  GtkWidget *window_controls;
  GtkWidget *search_popover;
  GtkWidget *search_entry;
  GtkWidget *search_list;
  GtkWidget *context_menu;

  GHashTable *tabs; /* AdwTabPage * -> GtkWidget * */

  gboolean autohide;
  gboolean expand;
  gboolean inverted;
  gboolean updating_widths;

  int dragged_index;
  int drag_target_index;
};

G_DEFINE_FINAL_TYPE (EphyTabStrip, ephy_tab_strip, ADW_TYPE_BIN)

enum {
  SIGNAL_EXTRA_DRAG_DROP,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL];

static void rebuild (EphyTabStrip *self);
static void root_changed_cb (EphyTabStrip *self);

/* Helpers */

static AdwTabPage *
get_page_for_tab (EphyTabStrip *self,
                  GtkWidget    *tab)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->tabs);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (value == tab)
      return key;
  }

  return NULL;
}

static char *
get_page_address (AdwTabPage *page)
{
  GtkWidget *child;
  EphyEmbed *embed;
  EphyWebView *view;
  const char *uri;

  child = adw_tab_page_get_child (page);
  if (!EPHY_IS_EMBED (child))
    return NULL;

  embed = EPHY_EMBED (child);
  view = ephy_embed_get_web_view (embed);
  if (!view)
    return NULL;

  uri = webkit_web_view_get_uri (WEBKIT_WEB_VIEW (view));
  if (!uri)
    return NULL;

  return g_strdup (uri);
}

static char *
get_page_title (AdwTabPage *page)
{
  const char *title = adw_tab_page_get_title (page);

  if (title && *title)
    return g_strdup (title);

  return g_strdup (_("Untitled"));
}

static GIcon *
get_page_icon (AdwTabPage *page)
{
  GIcon *icon = adw_tab_page_get_icon (page);

  if (icon)
    return g_object_ref (icon);

  return g_themed_icon_new ("web-browser-symbolic");
}

static void
update_tab_state (EphyTabStrip *self,
                  AdwTabPage   *page,
                  GtkWidget    *tab)
{
  gboolean selected = adw_tab_page_get_selected (page);
  gboolean pinned = adw_tab_page_get_pinned (page);

  if (selected)
    gtk_widget_add_css_class (tab, "active");
  else
    gtk_widget_remove_css_class (tab, "active");

  if (adw_tab_page_get_needs_attention (page))
    gtk_widget_add_css_class (tab, "needs-attention");
  else
    gtk_widget_remove_css_class (tab, "needs-attention");

  if (pinned)
    gtk_widget_add_css_class (tab, "pinned");
  else
    gtk_widget_remove_css_class (tab, "pinned");

  gtk_widget_set_tooltip_text (tab, adw_tab_page_get_tooltip (page));
}

static void
update_tab_contents (EphyTabStrip *self,
                     AdwTabPage   *page,
                     GtkWidget    *tab)
{
  GtkWidget *stack = g_object_get_data (G_OBJECT (tab), "stack");
  GtkWidget *image = g_object_get_data (G_OBJECT (tab), "image");
  GtkWidget *label = g_object_get_data (G_OBJECT (tab), "label");
  GtkWidget *spinner = g_object_get_data (G_OBJECT (tab), "spinner");
  g_autoptr (GIcon) icon = NULL;
  g_autofree char *title = NULL;

  icon = get_page_icon (page);
  gtk_image_set_from_gicon (GTK_IMAGE (image), icon);

  title = get_page_title (page);
  gtk_label_set_label (GTK_LABEL (label), title);

  if (adw_tab_page_get_loading (page) && !adw_tab_page_get_pinned (page)) {
    gtk_spinner_start (GTK_SPINNER (spinner));
    gtk_stack_set_visible_child_name (GTK_STACK (stack), "spinner");
  } else {
    gtk_spinner_stop (GTK_SPINNER (spinner));
    gtk_stack_set_visible_child_name (GTK_STACK (stack), "icon");
  }
}

/* Sizing */

static void
update_tab_widths (EphyTabStrip *self)
{
  int available, width, fixed_width;
  guint n_tabs = 0, n_pinned = 0, n_normal, i;
  GHashTableIter iter;
  gpointer key, value;

  if (self->updating_widths || !self->view || !self->scrolled_window)
    return;

  n_tabs = adw_tab_view_get_n_pages (self->view);
  if (n_tabs == 0)
    return;

  for (i = 0; i < n_tabs; i++)
    if (adw_tab_page_get_pinned (adw_tab_view_get_nth_page (self->view, i)))
      n_pinned++;

  n_normal = n_tabs - n_pinned;

  if (self->expand && n_normal > 0) {
    /* The strip is laid out as:
     * [ 8px ][tabs][4][+][4][search][4][spacer][4][window controls]
     * so tabs get whatever is left after the fixed widgets. */
    fixed_width = 8 + 8 /* slop for css margins */
                  + gtk_widget_get_width (self->new_tab_button)
                  + gtk_widget_get_width (self->tab_search_button)
                  + gtk_widget_get_width (self->window_controls)
                  + 4 * 4; /* box spacing */

    available = gtk_widget_get_width (GTK_WIDGET (self)) - fixed_width;
    available -= n_pinned * (PINNED_TAB_WIDTH + 2);

    if (available <= 0)
      return;

    width = available / n_normal - 2;
    width = CLAMP (width, MIN_TAB_WIDTH, MAX_TAB_WIDTH);
  } else {
    width = MAX_TAB_WIDTH;
  }

  self->updating_widths = TRUE;

  g_hash_table_iter_init (&iter, self->tabs);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    AdwTabPage *page = key;
    GtkWidget *tab = value;
    int tab_width = adw_tab_page_get_pinned (page) ? PINNED_TAB_WIDTH : width;
    int current_width = -1, current_height = -1;

    gtk_widget_get_size_request (tab, &current_width, &current_height);
    if (current_width != tab_width)
      gtk_widget_set_size_request (tab, tab_width, -1);
  }

  self->updating_widths = FALSE;
}

static void
ephy_tab_strip_size_allocate (GtkWidget *widget,
                              int        width,
                              int        height,
                              int        baseline)
{
  EphyTabStrip *self = EPHY_TAB_STRIP (widget);

  GTK_WIDGET_CLASS (ephy_tab_strip_parent_class)->size_allocate (widget, width, height, baseline);

  update_tab_widths (self);
}

/* Tab search */

static void
search_row_clicked_cb (EphyTabStrip *self,
                       GtkButton    *button)
{
  AdwTabPage *page = g_object_get_data (G_OBJECT (button), "page");
  GtkRoot *root;

  if (!page)
    return;

  gtk_popover_popdown (GTK_POPOVER (self->search_popover));
  adw_tab_view_set_selected_page (self->view, page);

  root = gtk_widget_get_root (GTK_WIDGET (self));
  if (root)
    gtk_window_present (GTK_WINDOW (root));
}

static void
search_list_clear (EphyTabStrip *self)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->search_list)))
    gtk_box_remove (GTK_BOX (self->search_list), child);
}

static void
search_list_populate (EphyTabStrip *self)
{
  const char *needle;
  g_autofree char *needle_folded = NULL;
  guint n_pages;
  guint i;

  search_list_clear (self);

  if (!self->view)
    return;

  needle = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));
  if (needle && *needle)
    needle_folded = g_utf8_casefold (needle, -1);

  n_pages = adw_tab_view_get_n_pages (self->view);

  for (i = 0; i < n_pages; i++) {
    AdwTabPage *page = adw_tab_view_get_nth_page (self->view, i);
    g_autofree char *title = get_page_title (page);
    g_autofree char *address = get_page_address (page);
    g_autoptr (GIcon) icon = NULL;
    GtkWidget *row, *box, *image, *labels, *title_label, *address_label;

    if (needle_folded) {
      g_autofree char *title_folded = g_utf8_casefold (title, -1);
      g_autofree char *address_folded = address ? g_utf8_casefold (address, -1) : NULL;

      if (!strstr (title_folded, needle_folded) &&
          !(address_folded && strstr (address_folded, needle_folded)))
        continue;
    }

    icon = get_page_icon (page);

    row = gtk_button_new ();
    gtk_widget_add_css_class (row, "flat");
    gtk_widget_add_css_class (row, "astrium-tab-search-row");

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start (box, 8);
    gtk_widget_set_margin_end (box, 8);
    gtk_button_set_child (GTK_BUTTON (row), box);

    image = gtk_image_new_from_gicon (icon);
    gtk_image_set_pixel_size (GTK_IMAGE (image), 16);
    gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (box), image);

    labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand (labels, TRUE);
    gtk_box_append (GTK_BOX (box), labels);

    title_label = gtk_label_new (title);
    gtk_label_set_xalign (GTK_LABEL (title_label), 0);
    gtk_label_set_ellipsize (GTK_LABEL (title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode (GTK_LABEL (title_label), TRUE);
    gtk_box_append (GTK_BOX (labels), title_label);

    address_label = gtk_label_new (address ? address : "");
    gtk_widget_add_css_class (address_label, "dim-label");
    gtk_widget_add_css_class (address_label, "caption");
    gtk_label_set_xalign (GTK_LABEL (address_label), 0);
    gtk_label_set_ellipsize (GTK_LABEL (address_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode (GTK_LABEL (address_label), TRUE);
    gtk_widget_set_visible (address_label, address != NULL);
    gtk_box_append (GTK_BOX (labels), address_label);

    g_object_set_data (G_OBJECT (row), "page", page);
    g_signal_connect_object (row, "clicked", G_CALLBACK (search_row_clicked_cb), self, G_CONNECT_SWAPPED);

    gtk_box_append (GTK_BOX (self->search_list), row);
  }
}

static void
search_popover_show_cb (EphyTabStrip *self)
{
  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  search_list_populate (self);
  gtk_widget_grab_focus (self->search_entry);
}

static void
search_entry_changed_cb (EphyTabStrip *self)
{
  search_list_populate (self);
}

/* Context menu */

static void
context_menu_hidden_cb (EphyTabStrip *self)
{
  if (self->view && !gtk_widget_get_visible (self->context_menu))
    g_signal_emit_by_name (self->view, "setup-menu", NULL);
}

static void
show_context_menu (EphyTabStrip *self,
                   AdwTabPage   *page,
                   double        x,
                   double        y)
{
  GMenuModel *model;
  GdkRectangle rect = { (int)x, (int)y, 0, 0 };

  if (!self->view)
    return;

  model = adw_tab_view_get_menu_model (self->view);
  if (!G_IS_MENU_MODEL (model))
    return;

  g_signal_emit_by_name (self->view, "setup-menu", page);

  if (!self->context_menu) {
    self->context_menu = gtk_popover_menu_new_from_model (model);
    gtk_widget_set_parent (self->context_menu, GTK_WIDGET (self));
    gtk_popover_set_position (GTK_POPOVER (self->context_menu), GTK_POS_BOTTOM);
    gtk_popover_set_has_arrow (GTK_POPOVER (self->context_menu), FALSE);
    gtk_widget_set_halign (self->context_menu, GTK_ALIGN_START);

    g_signal_connect_object (self->context_menu, "notify::visible",
                             G_CALLBACK (context_menu_hidden_cb), self,
                             G_CONNECT_AFTER | G_CONNECT_SWAPPED);
  }

  gtk_popover_set_pointing_to (GTK_POPOVER (self->context_menu), &rect);
  gtk_popover_popup (GTK_POPOVER (self->context_menu));
}

/* Interactions */

static void
translate_point (GtkWidget *src,
                 GtkWidget *dest,
                 double     x,
                 double     y,
                 double    *out_x,
                 double    *out_y)
{
  graphene_point_t point = GRAPHENE_POINT_INIT ((float)x, (float)y);
  graphene_point_t out;

  if (gtk_widget_compute_point (src, dest, &point, &out)) {
    *out_x = out.x;
    *out_y = out.y;
  } else {
    *out_x = x;
    *out_y = y;
  }
}

static void
tab_click_released_cb (EphyTabStrip    *self,
                       int              n_press,
                       double           x,
                       double           y,
                       GtkGestureClick *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);

  if (page)
    adw_tab_view_set_selected_page (self->view, page);
}

static void
tab_middle_click_cb (EphyTabStrip    *self,
                     int              n_press,
                     double           x,
                     double           y,
                     GtkGestureClick *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);

  if (page)
    adw_tab_view_close_page (self->view, page);
}

static void
tab_right_click_cb (EphyTabStrip    *self,
                    int              n_press,
                    double           x,
                    double           y,
                    GtkGestureClick *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);
  double tx, ty;

  if (!page)
    return;

  translate_point (tab, GTK_WIDGET (self), x, y, &tx, &ty);
  show_context_menu (self, page, tx, ty);
}

static void
tab_long_press_cb (EphyTabStrip        *self,
                   double               x,
                   double               y,
                   GtkGestureLongPress *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);
  double tx, ty;

  if (!page)
    return;

  translate_point (tab, GTK_WIDGET (self), x, y, &tx, &ty);
  show_context_menu (self, page, tx, ty);
}

static void
tab_drag_begin_cb (EphyTabStrip   *self,
                   double          start_x,
                   double          start_y,
                   GtkGestureDrag *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);

  self->dragged_index = page ? (int)adw_tab_view_get_page_position (self->view, page) : -1;
  self->drag_target_index = self->dragged_index;
}

static void
tab_drag_update_cb (EphyTabStrip   *self,
                    double          offset_x,
                    double          offset_y,
                    GtkGestureDrag *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);
  double x, y, tab_x, tab_y;
  int index = 0;

  if (!page || self->dragged_index < 0)
    return;

  gtk_gesture_drag_get_start_point (gesture, &x, &y);
  x += offset_x;
  y += offset_y;

  translate_point (tab, self->tabs_box, x, y, &tab_x, &tab_y);

  for (GtkWidget *child = gtk_widget_get_first_child (self->tabs_box);
       child;
       child = gtk_widget_get_next_sibling (child)) {
    graphene_rect_t bounds;

    if (child == tab)
      continue;

    if (!gtk_widget_compute_bounds (child, self->tabs_box, &bounds))
      continue;

    if (tab_x > bounds.origin.x + bounds.size.width / 2)
      index++;
  }

  self->drag_target_index = CLAMP (index, 0, MAX ((int)adw_tab_view_get_n_pages (self->view) - 1, 0));
}

static void
tab_drag_end_cb (EphyTabStrip   *self,
                 double          offset_x,
                 double          offset_y,
                 GtkGestureDrag *gesture)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
  AdwTabPage *page = get_page_for_tab (self, tab);
  gboolean moved = ABS (offset_x) > DRAG_THRESHOLD || ABS (offset_y) > DRAG_THRESHOLD;

  if (!page)
    return;

  if (moved && self->drag_target_index >= 0 &&
      self->drag_target_index != self->dragged_index &&
      !adw_tab_page_get_pinned (page) &&
      !adw_tab_page_get_pinned (adw_tab_view_get_nth_page (self->view, self->drag_target_index)))
    adw_tab_view_reorder_page (self->view, page, self->drag_target_index);

  self->dragged_index = -1;
  self->drag_target_index = -1;
}

static gboolean
tab_key_pressed_cb (EphyTabStrip          *self,
                    guint                  keyval,
                    guint                  keycode,
                    GdkModifierType        state,
                    GtkEventControllerKey *controller)
{
  GtkWidget *tab = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller));
  AdwTabPage *page = get_page_for_tab (self, tab);

  if (!page)
    return GDK_EVENT_PROPAGATE;

  if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter || keyval == GDK_KEY_space) {
    adw_tab_view_set_selected_page (self->view, page);
    return GDK_EVENT_STOP;
  }

  if (keyval == GDK_KEY_Menu) {
    show_context_menu (self, page, 0, 0);
    return GDK_EVENT_STOP;
  }

  return GDK_EVENT_PROPAGATE;
}

/* Tab widget */

static void
close_button_clicked_cb (EphyTabStrip *self,
                         GtkButton    *button)
{
  AdwTabPage *page = g_object_get_data (G_OBJECT (button), "page");

  if (page)
    adw_tab_view_close_page (self->view, page);
}

static void
create_tab (EphyTabStrip *self,
            AdwTabPage   *page,
            GtkWidget   **out_tab)
{
  GtkWidget *tab, *box, *stack, *image, *spinner, *label, *close_button;
  GtkGesture *click, *middle_click, *right_click, *drag, *long_press;
  GtkEventController *key_controller;
  g_autoptr (GIcon) icon = NULL;

  tab = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class (tab, "astrium-tab");
  gtk_widget_set_can_focus (tab, TRUE);
  gtk_widget_set_valign (tab, GTK_ALIGN_FILL);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start (box, 10);
  gtk_widget_set_margin_end (box, 6);
  gtk_box_append (GTK_BOX (tab), box);

  stack = gtk_stack_new ();
  gtk_widget_set_valign (stack, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request (stack, 16, 16);
  gtk_box_append (GTK_BOX (box), stack);

  icon = get_page_icon (page);
  image = gtk_image_new_from_gicon (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (image), 16);
  gtk_stack_add_named (GTK_STACK (stack), image, "icon");

  spinner = gtk_spinner_new ();
  gtk_widget_set_size_request (spinner, 16, 16);
  gtk_stack_add_named (GTK_STACK (stack), spinner, "spinner");

  label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_single_line_mode (GTK_LABEL (label), TRUE);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), label);

  close_button = gtk_button_new_from_icon_name ("window-close-symbolic");
  gtk_widget_add_css_class (close_button, "flat");
  gtk_widget_add_css_class (close_button, "circular");
  gtk_widget_add_css_class (close_button, "astrium-tab-close");
  gtk_widget_set_valign (close_button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (close_button, _("Close tab"));
  gtk_box_append (GTK_BOX (box), close_button);

  g_object_set_data (G_OBJECT (tab), "stack", stack);
  g_object_set_data (G_OBJECT (tab), "image", image);
  g_object_set_data (G_OBJECT (tab), "spinner", spinner);
  g_object_set_data (G_OBJECT (tab), "label", label);
  g_object_set_data (G_OBJECT (tab), "close", close_button);
  g_object_set_data (G_OBJECT (close_button), "page", page);

  g_signal_connect_object (close_button, "clicked",
                           G_CALLBACK (close_button_clicked_cb), self,
                           G_CONNECT_SWAPPED);

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
  g_signal_connect_object (click, "released", G_CALLBACK (tab_click_released_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (click));

  middle_click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (middle_click), GDK_BUTTON_MIDDLE);
  g_signal_connect_object (middle_click, "released", G_CALLBACK (tab_middle_click_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (middle_click));

  right_click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (right_click), GDK_BUTTON_SECONDARY);
  g_signal_connect_object (right_click, "released", G_CALLBACK (tab_right_click_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (right_click));

  long_press = gtk_gesture_long_press_new ();
  g_signal_connect_object (long_press, "pressed", G_CALLBACK (tab_long_press_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (long_press));

  drag = gtk_gesture_drag_new ();
  g_signal_connect_object (drag, "drag-begin", G_CALLBACK (tab_drag_begin_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (drag, "drag-update", G_CALLBACK (tab_drag_update_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (drag, "drag-end", G_CALLBACK (tab_drag_end_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (drag));
  gtk_gesture_group (click, drag);

  key_controller = gtk_event_controller_key_new ();
  g_signal_connect_object (key_controller, "key-pressed", G_CALLBACK (tab_key_pressed_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (tab, key_controller);

  update_tab_contents (self, page, tab);
  update_tab_state (self, page, tab);

  *out_tab = tab;
}

static void
tab_notify_cb (EphyTabStrip *self,
               GParamSpec   *pspec,
               AdwTabPage   *page)
{
  GtkWidget *tab = g_hash_table_lookup (self->tabs, page);

  if (!tab)
    return;

  update_tab_contents (self, page, tab);
  update_tab_state (self, page, tab);

  if (self->search_popover && gtk_widget_get_visible (self->search_popover))
    search_list_populate (self);
}

/* Rebuild */

static void
rebuild (EphyTabStrip *self)
{
  guint n_pages, i;
  GtkWidget *child;
  g_autoptr (GPtrArray) ordered = NULL;
  gboolean order_matches = TRUE;

  if (!self->view)
    return;

  n_pages = adw_tab_view_get_n_pages (self->view);

  /* Remove stale tabs */
  {
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, self->tabs);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      AdwTabPage *page = key;
      gboolean found = FALSE;

      for (i = 0; i < n_pages; i++) {
        if (adw_tab_view_get_nth_page (self->view, i) == page) {
          found = TRUE;
          break;
        }
      }

      if (!found) {
        gtk_box_remove (GTK_BOX (self->tabs_box), value);
        g_hash_table_iter_remove (&iter);
      }
    }
  }

  ordered = g_ptr_array_new ();

  for (i = 0; i < n_pages; i++) {
    AdwTabPage *page = adw_tab_view_get_nth_page (self->view, i);
    GtkWidget *tab = g_hash_table_lookup (self->tabs, page);

    if (!tab) {
      create_tab (self, page, &tab);
      g_hash_table_insert (self->tabs, page, g_object_ref_sink (tab));

      g_signal_connect_object (page, "notify::title", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (page, "notify::icon", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (page, "notify::tooltip", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (page, "notify::needs-attention", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (page, "notify::pinned", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (page, "notify::loading", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
      g_signal_connect_object (page, "notify::selected", G_CALLBACK (tab_notify_cb), self, G_CONNECT_SWAPPED);
    }

    g_ptr_array_add (ordered, tab);
  }

  /* Only touch the box when the order actually changed, to avoid
   * destroying and recreating the widget hierarchy on every notification. */
  child = gtk_widget_get_first_child (self->tabs_box);
  for (i = 0; i < ordered->len; i++) {
    if (child != g_ptr_array_index (ordered, i)) {
      order_matches = FALSE;
      break;
    }
    child = gtk_widget_get_next_sibling (child);
  }
  if (child)
    order_matches = FALSE;

  if (!order_matches) {
    while ((child = gtk_widget_get_first_child (self->tabs_box)))
      gtk_box_remove (GTK_BOX (self->tabs_box), child);

    for (i = 0; i < ordered->len; i++)
      gtk_box_append (GTK_BOX (self->tabs_box), g_ptr_array_index (ordered, i));
  }

  update_tab_widths (self);

  if (self->search_popover && gtk_widget_get_visible (self->search_popover))
    search_list_populate (self);
}

static void
page_attached_cb (EphyTabStrip *self)
{
  rebuild (self);
}

static void
page_detached_cb (EphyTabStrip *self)
{
  rebuild (self);
}

static void
n_pages_changed_cb (EphyTabStrip *self)
{
  rebuild (self);
}

static void
adjustment_changed_cb (EphyTabStrip *self)
{
  update_tab_widths (self);
}

/* GObject */

static void
ephy_tab_strip_dispose (GObject *object)
{
  EphyTabStrip *self = EPHY_TAB_STRIP (object);

  g_clear_pointer (&self->context_menu, gtk_widget_unparent);
  g_clear_pointer (&self->tabs, g_hash_table_destroy);

  G_OBJECT_CLASS (ephy_tab_strip_parent_class)->dispose (object);
}

static void
ephy_tab_strip_class_init (EphyTabStripClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ephy_tab_strip_dispose;
  widget_class->size_allocate = ephy_tab_strip_size_allocate;

  signals[SIGNAL_EXTRA_DRAG_DROP] =
    g_signal_new ("extra-drag-drop",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  2, ADW_TYPE_TAB_PAGE, G_TYPE_VALUE);
}

static void
ephy_tab_strip_init (EphyTabStrip *self)
{
  GtkWidget *plus_button, *spacer;
  GtkAdjustment *adjustment;
  GtkWidget *search_box, *scrolled, *margin_box;

  self->tabs = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
  self->expand = TRUE;

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class (self->box, "astrium-tab-strip");
  gtk_widget_set_valign (self->box, GTK_ALIGN_FILL);

  self->window_handle = gtk_window_handle_new ();
  gtk_window_handle_set_child (GTK_WINDOW_HANDLE (self->window_handle), self->box);

  self->scrolled_window = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->scrolled_window),
                                  GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->scrolled_window), TRUE);
  gtk_widget_set_hexpand (self->scrolled_window, FALSE);
  gtk_widget_set_valign (self->scrolled_window, GTK_ALIGN_FILL);
  gtk_widget_set_margin_start (self->scrolled_window, 8);
  gtk_box_append (GTK_BOX (self->box), self->scrolled_window);

  self->tabs_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_valign (self->tabs_box, GTK_ALIGN_FILL);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->scrolled_window), self->tabs_box);

  adjustment = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  g_signal_connect_object (adjustment, "changed", G_CALLBACK (adjustment_changed_cb), self, G_CONNECT_SWAPPED);

  plus_button = gtk_button_new_from_icon_name ("list-add-symbolic");
  gtk_widget_add_css_class (plus_button, "flat");
  gtk_widget_add_css_class (plus_button, "astrium-tab-strip-button");
  gtk_widget_set_valign (plus_button, GTK_ALIGN_CENTER);
  /* Translators: tooltip for the new tab button */
  gtk_widget_set_tooltip_text (plus_button, _("Open a new tab"));
  gtk_actionable_set_action_name (GTK_ACTIONABLE (plus_button), "win.new-tab");
  gtk_box_append (GTK_BOX (self->box), plus_button);
  self->new_tab_button = plus_button;

  self->tab_search_button = gtk_menu_button_new ();
  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (self->tab_search_button), "system-search-symbolic");
  gtk_widget_add_css_class (self->tab_search_button, "flat");
  gtk_widget_add_css_class (self->tab_search_button, "astrium-tab-strip-button");
  gtk_widget_set_valign (self->tab_search_button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (self->tab_search_button, _("Search tabs"));
  gtk_box_append (GTK_BOX (self->box), self->tab_search_button);

  spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand (spacer, TRUE);
  gtk_box_append (GTK_BOX (self->box), spacer);

  self->window_controls = gtk_window_controls_new (GTK_PACK_END);
  gtk_widget_set_valign (self->window_controls, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (self->box), self->window_controls);
  g_signal_connect_object (self, "notify::root", G_CALLBACK (root_changed_cb), self, G_CONNECT_SWAPPED);

  self->search_popover = gtk_popover_new ();
  gtk_popover_set_has_arrow (GTK_POPOVER (self->search_popover), FALSE);
  gtk_popover_set_position (GTK_POPOVER (self->search_popover), GTK_POS_BOTTOM);
  gtk_widget_add_css_class (self->search_popover, "astrium-tab-search");

  search_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_size_request (search_box, 340, -1);
  margin_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_start (margin_box, 8);
  gtk_widget_set_margin_end (margin_box, 8);
  gtk_widget_set_margin_top (margin_box, 8);
  gtk_widget_set_margin_bottom (margin_box, 8);
  gtk_box_append (GTK_BOX (search_box), margin_box);

  self->search_entry = gtk_search_entry_new ();
  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (self->search_entry), _("Search tabs"));
  gtk_box_append (GTK_BOX (margin_box), self->search_entry);

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scrolled), TRUE);
  gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scrolled), 400);
  gtk_widget_set_margin_top (scrolled, 4);
  gtk_box_append (GTK_BOX (search_box), scrolled);

  self->search_list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), self->search_list);

  gtk_popover_set_child (GTK_POPOVER (self->search_popover), search_box);
  gtk_menu_button_set_popover (GTK_MENU_BUTTON (self->tab_search_button), self->search_popover);

  g_signal_connect_object (self->search_popover, "show", G_CALLBACK (search_popover_show_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->search_entry, "changed", G_CALLBACK (search_entry_changed_cb), self, G_CONNECT_SWAPPED);

  adw_bin_set_child (ADW_BIN (self), self->window_handle);
}

static void
update_window_controls_visibility (EphyTabStrip *self)
{
  GtkRoot *root;
  gboolean visible = TRUE;

  root = gtk_widget_get_root (GTK_WIDGET (self));

  if (root && GTK_IS_WINDOW (root))
    visible = !gtk_window_is_fullscreen (GTK_WINDOW (root));

  gtk_widget_set_visible (self->window_controls, visible);
}

static void
root_changed_cb (EphyTabStrip *self)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

  if (root)
    g_signal_connect_object (root, "notify::fullscreened",
                             G_CALLBACK (update_window_controls_visibility), self,
                             G_CONNECT_SWAPPED);

  update_window_controls_visibility (self);
}

EphyTabStrip *
ephy_tab_strip_new (void)
{
  return g_object_new (EPHY_TYPE_TAB_STRIP, NULL);
}

void
ephy_tab_strip_set_tab_view (EphyTabStrip *self,
                             AdwTabView   *tab_view)
{
  if (self->view) {
    g_signal_handlers_disconnect_by_data (self->view, self);
    g_set_object (&self->view, NULL);
  }

  g_set_object (&self->view, tab_view);

  if (!tab_view)
    return;

  g_signal_connect_object (tab_view, "page-attached", G_CALLBACK (page_attached_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (tab_view, "page-detached", G_CALLBACK (page_detached_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (tab_view, "notify::n-pages", G_CALLBACK (n_pages_changed_cb), self, G_CONNECT_SWAPPED);

  rebuild (self);
}

void
ephy_tab_strip_set_autohide (EphyTabStrip *self,
                             gboolean      autohide)
{
  if (self->autohide == autohide)
    return;

  self->autohide = autohide;
  rebuild (self);
}

void
ephy_tab_strip_set_expand_tabs (EphyTabStrip *self,
                                gboolean      expand)
{
  if (self->expand == expand)
    return;

  self->expand = expand;
  update_tab_widths (self);
}

void
ephy_tab_strip_set_inverted (EphyTabStrip *self,
                             gboolean      inverted)
{
  self->inverted = inverted;

  if (inverted)
    gtk_box_reorder_child_after (GTK_BOX (self->box), self->tab_search_button, NULL);
  else
    gtk_box_reorder_child_after (GTK_BOX (self->box), self->tab_search_button, self->new_tab_button);
}

void
ephy_tab_strip_set_start_action_widget (EphyTabStrip *self,
                                        GtkWidget    *widget)
{
  if (self->start_action_widget == widget)
    return;

  if (self->start_action_widget)
    gtk_box_remove (GTK_BOX (self->box), self->start_action_widget);

  self->start_action_widget = widget;

  if (widget)
    gtk_box_prepend (GTK_BOX (self->box), widget);
}

static gboolean
extra_drop_cb (EphyTabStrip  *self,
               double         x,
               double         y,
               const GValue  *value,
               GtkDropTarget *target)
{
  GtkWidget *picked;
  AdwTabPage *page = NULL;

  picked = gtk_widget_pick (GTK_WIDGET (self), x, y, GTK_PICK_DEFAULT);
  while (picked && picked != GTK_WIDGET (self)) {
    page = get_page_for_tab (self, picked);
    if (page)
      break;
    picked = gtk_widget_get_parent (picked);
  }

  if (!page && self->view)
    page = adw_tab_view_get_selected_page (self->view);

  if (!page)
    return FALSE;

  g_signal_emit (self, signals[SIGNAL_EXTRA_DRAG_DROP], 0, page, value);

  return TRUE;
}

void
ephy_tab_strip_setup_extra_drop_target (EphyTabStrip  *self,
                                        GdkDragAction  actions,
                                        GType         *types,
                                        gsize          n_types)
{
  gsize i;

  for (i = 0; i < n_types; i++) {
    GtkDropTarget *target = gtk_drop_target_new (types[i], actions);

    g_signal_connect_object (target, "drop", G_CALLBACK (extra_drop_cb), self, G_CONNECT_SWAPPED);
    gtk_widget_add_controller (GTK_WIDGET (self->scrolled_window), GTK_EVENT_CONTROLLER (target));
  }
}