/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBEDATASERVERUI_PRIVATE_H
#define LIBEDATASERVERUI_PRIVATE_H

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

void		_libedataserverui_load_modules		(void);
void		_libedataserverui_init_icon_theme	(void);

gint		_libedataserverui_dialog_run		(GtkDialog *dialog);
const gchar *	_libedataserverui_entry_get_text	(GtkEntry *entry);
void		_libedataserverui_entry_set_text	(GtkEntry *entry,
							 const gchar *text);
void		_libedataserverui_box_pack_start	(GtkBox *box,
							 GtkWidget *child,
							 gboolean expand,
							 gboolean fill,
							 guint padding);

G_END_DECLS

#endif /* LIBEDATASERVERUI_PRIVATE_H */
