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

#ifndef __E_DATA_BOOK_H__
#define __E_DATA_BOOK_H__

#include <glib-object.h>
#include <dbus/dbus-glib.h>
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

struct _EDataBook {
	GObject parent;
	EBookBackend *backend;
	ESource *source;
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

EDataBook		*e_data_book_new                    (EBookBackend *backend, ESource *source);
EBookBackend		*e_data_book_get_backend            (EDataBook *book);
ESource			*e_data_book_get_source             (EDataBook *book);

void                    e_data_book_respond_open           (EDataBook *book,
							    guint32 opid,
							    EDataBookStatus status);
void                    e_data_book_respond_remove         (EDataBook *book,
							    guint32 opid,
							    EDataBookStatus status);
void                    e_data_book_respond_create         (EDataBook *book,
							    guint32 opid,
							    EDataBookStatus status,
							    EContact *contact);
void                    e_data_book_respond_remove_contacts (EDataBook *book,
							     guint32 opid,
							     EDataBookStatus  status,
							     GList *ids);
void                    e_data_book_respond_modify         (EDataBook *book,
							    guint32 opid,
							    EDataBookStatus status,
							    EContact *contact);
void                    e_data_book_respond_authenticate_user (EDataBook *book,
							       guint32 opid,
							       EDataBookStatus status);
void                    e_data_book_respond_get_supported_fields (EDataBook *book,
								  guint32 opid,
								  EDataBookStatus status,
								  GList *fields);
void                    e_data_book_respond_get_required_fields (EDataBook *book,
								  guint32 opid,
								  EDataBookStatus status,
								  GList *fields);
void                    e_data_book_respond_get_supported_auth_methods (EDataBook *book,
									guint32 opid,
									EDataBookStatus status,
									GList *fields);

void                    e_data_book_respond_get_contact (EDataBook *book,
							    guint32 opid,
							    EDataBookStatus status,
							    const gchar *vcard);
void                    e_data_book_respond_get_contact_list (EDataBook *book,
							      guint32 opid,
							      EDataBookStatus status,
							      GList *cards);
void                    e_data_book_respond_get_changes    (EDataBook *book,
							    guint32 opid,
							    EDataBookStatus status,
							    GList *changes);

void                    e_data_book_report_writable        (EDataBook                         *book,
							    gboolean                           writable);
void                    e_data_book_report_connection_status (EDataBook                        *book,
							      gboolean                         is_online);

void                    e_data_book_report_auth_required     (EDataBook                       *book);

GType                   e_data_book_get_type               (void);

G_END_DECLS

#endif /* __E_DATA_BOOK_H__ */
