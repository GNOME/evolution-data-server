/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
 *
 */

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef E_BOOK_CLIENT_VIEW_H
#define E_BOOK_CLIENT_VIEW_H

#include <glib-object.h>
#include <libebook-contacts/libebook-contacts.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_CLIENT_VIEW \
	(e_book_client_view_get_type ())
#define E_BOOK_CLIENT_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_CLIENT_VIEW, EBookClientView))
#define E_BOOK_CLIENT_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_CLIENT_VIEW, EBookClientViewClass))
#define E_IS_BOOK_CLIENT_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_CLIENT_VIEW))
#define E_IS_BOOK_CLIENT_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_CLIENT_VIEW))
#define E_BOOK_CLIENT_VIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_CLIENT_VIEW, EBookClientViewClass))

G_BEGIN_DECLS

typedef struct _EBookClientView EBookClientView;
typedef struct _EBookClientViewClass EBookClientViewClass;
typedef struct _EBookClientViewPrivate EBookClientViewPrivate;

struct _EBookClient;

/**
 * EBookClientView:
 *
 * Contains only private data the should be read and manipulated using the
 * functions below.
 *
 * Since: 3.2
 **/
struct _EBookClientView {
	GObject parent;
	EBookClientViewPrivate *priv;
};

struct _EBookClientViewClass {
	GObjectClass parent_class;

	/* Signals */
	void		(*objects_added)	(EBookClientView *client_view,
						 const GSList *objects);
	void		(*objects_modified)	(EBookClientView *client_view,
						 const GSList *objects);
	void		(*objects_removed)	(EBookClientView *client_view,
						 const GSList *uids);
	void		(*progress)		(EBookClientView *client_view,
						 guint percent,
						 const gchar *message);
	void		(*complete)		(EBookClientView *client_view,
						 const GError *error);
};

GType		e_book_client_view_get_type	(void) G_GNUC_CONST;
struct _EBookClient *
		e_book_client_view_ref_client	(EBookClientView *client_view);
GDBusConnection *
		e_book_client_view_get_connection
						(EBookClientView *client_view);
const gchar *	e_book_client_view_get_object_path
						(EBookClientView *client_view);
gboolean	e_book_client_view_is_running	(EBookClientView *client_view);
void		e_book_client_view_set_fields_of_interest
						(EBookClientView *client_view,
						 const GSList *fields_of_interest,
						 GError **error);
void		e_book_client_view_start	(EBookClientView *client_view,
						 GError **error);
void		e_book_client_view_stop		(EBookClientView *client_view,
						 GError **error);
void		e_book_client_view_set_flags	(EBookClientView *client_view,
						 EBookClientViewFlags flags,
						 GError **error);

#ifndef EDS_DISABLE_DEPRECATED
struct _EBookClient *
		e_book_client_view_get_client	(EBookClientView *client_view);
#endif /* EDS_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* E_BOOK_CLIENT_VIEW_H */
