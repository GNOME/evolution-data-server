/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2005 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __E_CATEGORIES__
#define __E_CATEGORIES__

#include <glib/glist.h>
#include <glib/gmacros.h>

G_BEGIN_DECLS

GList      *e_categories_get_list (void);

void        e_categories_add (const char *category, const char *color, const char *icon_file, gboolean searchable);
void        e_categories_remove (const char *category);

gboolean    e_categories_exist (const char *category);
const char *e_categories_get_color_for (const char *category);
void        e_categories_set_color_for (const char *category, const char *color);
const char *e_categories_get_icon_file_for (const char *category);
void        e_categories_set_icon_file_for (const char *category, const char *icon_file);
gboolean    e_categories_is_searchable (const char *category);

G_END_DECLS

#endif
