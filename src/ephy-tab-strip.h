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

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define EPHY_TYPE_TAB_STRIP (ephy_tab_strip_get_type())

G_DECLARE_FINAL_TYPE (EphyTabStrip, ephy_tab_strip, EPHY, TAB_STRIP, AdwBin)

EphyTabStrip *ephy_tab_strip_new                  (void);

void       ephy_tab_strip_set_tab_view            (EphyTabStrip *self,
                                                   AdwTabView   *tab_view);
void       ephy_tab_strip_set_autohide            (EphyTabStrip *self,
                                                   gboolean      autohide);
void       ephy_tab_strip_set_expand_tabs         (EphyTabStrip *self,
                                                   gboolean      expand);
void       ephy_tab_strip_set_inverted            (EphyTabStrip *self,
                                                   gboolean      inverted);
void       ephy_tab_strip_set_start_action_widget (EphyTabStrip *self,
                                                   GtkWidget    *widget);
void       ephy_tab_strip_setup_extra_drop_target (EphyTabStrip  *self,
                                                   GdkDragAction  actions,
                                                   GType         *types,
                                                   gsize          n_types);

G_END_DECLS