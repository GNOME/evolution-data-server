/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-auth-util.h - Lame helper to load addressbooks with authentication.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__LIBEDATASERVERUI_H_INSIDE__) && !defined (LIBEDATASERVERUI_COMPILATION)
#error "Only <libedataserverui/libedataserverui.h> should be included directly."
#endif

#ifndef EDS_DISABLE_DEPRECATED

#ifndef E_BOOK_AUTH_UTIL_H
#define E_BOOK_AUTH_UTIL_H

#include <gtk/gtk.h>
#include <libebook/libebook.h>

G_BEGIN_DECLS

void		e_load_book_source_async	(ESource *source,
						 GtkWindow *parent,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
EBook *		e_load_book_source_finish	(ESource *source,
						 GAsyncResult *result,
						 GError **error);
G_END_DECLS

#endif /* E_BOOK_AUTH_UTIL_H */

#endif /* EDS_DISABLE_DEPRECATED */
