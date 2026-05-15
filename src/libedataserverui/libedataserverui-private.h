/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
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
