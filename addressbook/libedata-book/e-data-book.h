/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#ifndef E_DATA_BOOK_H
#define E_DATA_BOOK_H

#include <glib-object.h>
#include <gio/gio.h>

#include <libedataserver/e-credentials.h>
#include <libedataserver/e-source.h>

#include "e-book-backend.h"
#include "e-data-book-types.h"

G_BEGIN_DECLS

#define E_TYPE_DATA_BOOK        (e_data_book_get_type ())
#define E_DATA_BOOK(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK, EDataBook))
#define E_DATA_BOOK_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_DATA_BOOK, EDataBookClass))
#define E_IS_DATA_BOOK(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK))
#define E_IS_DATA_BOOK_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK))
#define E_DATA_BOOK_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_DATA_BOOK, EDataBookClass))

typedef struct _EDataBookPrivate EDataBookPrivate;

struct _EDataBook {
	GObject parent;

	EDataBookPrivate *priv;
};

struct _EDataBookClass {
	GObjectClass parent;
};

GQuark e_data_book_error_quark (void);

/**
 * E_DATA_BOOK_ERROR:
 *
 * Since: 2.30
 **/
#define E_DATA_BOOK_ERROR e_data_book_error_quark ()

/**
 * e_data_book_create_error:
 * @status: #EDataBookStatus code
 * @custom_msg: Custom message to use for the error. When NULL,
 *              then uses a default message based on the @status code.
 *
 * Returns: NULL, when the @status is E_DATA_BOOK_STATUS_SUCCESS,
 *          or a newly allocated GError, which should be freed
 *          with g_error_free() call.
 **/
GError *e_data_book_create_error (EDataBookStatus status, const gchar *custom_msg);

/**
 * e_data_book_create_error_fmt:
 *
 * Similar as e_data_book_create_error(), only here, instead of custom_msg,
 * is used a printf() format to create a custom_msg for the error.
 **/
GError *e_data_book_create_error_fmt (EDataBookStatus status, const gchar *custom_msg_fmt, ...) G_GNUC_PRINTF (2, 3);

const gchar *e_data_book_status_to_string (EDataBookStatus status);

/**
 * e_return_data_book_error_if_fail:
 *
 * Since: 2.32
 **/
#define e_return_data_book_error_if_fail(expr, _code)				\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (error, E_DATA_BOOK_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			return;							\
		}								\
	} G_STMT_END

/**
 * e_return_data_book_error_if_fail:
 *
 * Same as e_return_data_book_error_if_fail(), only returns FALSE on a failure
 *
 * Since: 3.2
 **/
#define e_return_data_book_error_val_if_fail(expr, _code)			\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (error, E_DATA_BOOK_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			return FALSE;						\
		}								\
	} G_STMT_END

GType		e_data_book_get_type				(void);
EDataBook *	e_data_book_new					(EBookBackend *backend, ESource *source);
EBookBackend *	e_data_book_get_backend				(EDataBook *book);
ESource *	e_data_book_get_source				(EDataBook *book);

guint		e_data_book_register_gdbus_object		(EDataBook *cal, GDBusConnection *connection, const gchar *object_path, GError **error);

void		e_data_book_respond_open			(EDataBook *book, guint32 opid, GError *error);
void		e_data_book_respond_remove			(EDataBook *book, guint32 opid, GError *error);
void		e_data_book_respond_refresh			(EDataBook *book, guint32 opid, GError *error);
void		e_data_book_respond_get_backend_property	(EDataBook *book, guint32 opid, GError *error, const gchar *prop_value);
void		e_data_book_respond_set_backend_property	(EDataBook *book, guint32 opid, GError *error);
void		e_data_book_respond_create			(EDataBook *book, guint32 opid, GError *error, const EContact *contact);
void		e_data_book_respond_remove_contacts		(EDataBook *book, guint32 opid, GError *error, const GSList *ids);
void		e_data_book_respond_modify			(EDataBook *book, guint32 opid, GError *error, const EContact *contact);
void		e_data_book_respond_get_contact			(EDataBook *book, guint32 opid, GError *error, const gchar *vcard);
void		e_data_book_respond_get_contact_list		(EDataBook *book, guint32 opid, GError *error, const GSList *cards);

void		e_data_book_report_error			(EDataBook *book, const gchar *message);
void		e_data_book_report_readonly			(EDataBook *book, gboolean readonly);
void		e_data_book_report_online			(EDataBook *book, gboolean is_online);
void		e_data_book_report_auth_required		(EDataBook *book, const ECredentials *credentials);
void		e_data_book_report_opened			(EDataBook *book, const GError *error);

gchar *		e_data_book_string_slist_to_comma_string	(const GSList *strings);

G_END_DECLS

#endif /* E_DATA_BOOK_H */
