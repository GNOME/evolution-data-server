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

#define E_TYPE_BOOK_CLIENT_VIEW           (e_book_client_view_get_type ())
#define E_BOOK_CLIENT_VIEW(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_CLIENT_VIEW, EBookClientView))
#define E_BOOK_CLIENT_VIEW_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_CLIENT_VIEW, EBookClientViewClass))
#define E_IS_BOOK_CLIENT_VIEW(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_CLIENT_VIEW))
#define E_IS_BOOK_CLIENT_VIEW_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_CLIENT_VIEW))
#define E_BOOK_CLIENT_VIEW_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_CLIENT_VIEW, EBookClientViewClass))

G_BEGIN_DECLS

typedef struct _EBookClientView        EBookClientView;
typedef struct _EBookClientViewClass   EBookClientViewClass;
typedef struct _EBookClientViewPrivate EBookClientViewPrivate;

struct _EBookClient;  /* Forward reference */

/**
 * EBookClientViewFlags:
 * @E_BOOK_CLIENT_VIEW_FLAGS_NONE:
 *   Symbolic value for no flags
 * @E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL:
 *   If this flag is set then all contacts matching the view's query will
 *   be sent as notifications when starting the view, otherwise only future
 *   changes will be reported.  The default for a #EBookClientView is %TRUE.
 *
 * Flags that control the behaviour of an #EBookClientView.
 *
 * Since: 3.4
 */
typedef enum {
	E_BOOK_CLIENT_VIEW_FLAGS_NONE           = 0,
	E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL = (1 << 0),
} EBookClientViewFlags;

struct _EBookClientView {
	GObject     parent;
	/*< private >*/
	EBookClientViewPrivate *priv;
};

struct _EBookClientViewClass {
	GObjectClass parent;

	/*
	 * Signals.
	 */
	void (* objects_added)		(EBookClientView *view, const GSList *objects);
	void (* objects_modified)	(EBookClientView *view, const GSList *objects);
	void (* objects_removed)	(EBookClientView *view, const GSList *uids);

	void (* progress)		(EBookClientView *view, guint percent, const gchar *message);
	void (* complete)		(EBookClientView *view, const GError *error);
};

GType			e_book_client_view_get_type		(void);
struct _EBookClient *	e_book_client_view_get_client		(EBookClientView *view);
gboolean		e_book_client_view_is_running		(EBookClientView *view);
void			e_book_client_view_set_fields_of_interest (EBookClientView *view, const GSList *fields_of_interest, GError **error);
void			e_book_client_view_start		(EBookClientView *view, GError **error);
void			e_book_client_view_stop			(EBookClientView *view, GError **error);
void                    e_book_client_view_set_flags            (EBookClientView *view, EBookClientViewFlags  flags, GError **error);

G_END_DECLS

#endif /* E_BOOK_CLIENT_VIEW_H */
