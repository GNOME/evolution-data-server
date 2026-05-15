/*
 * SPDX-FileCopyrightText: (C) 2012 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_DATA_BOOK_DIRECT_H
#define E_DATA_BOOK_DIRECT_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define E_TYPE_DATA_BOOK_DIRECT        (e_data_book_direct_get_type ())
#define E_DATA_BOOK_DIRECT(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirect))
#define E_DATA_BOOK_DIRECT_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirectClass))
#define E_IS_DATA_BOOK_DIRECT(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_DATA_BOOK_DIRECT))
#define E_IS_DATA_BOOK_DIRECT_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_DATA_BOOK_DIRECT))
#define E_DATA_BOOK_DIRECT_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirect))

typedef struct _EDataBookDirect EDataBookDirect;
typedef struct _EDataBookDirectClass EDataBookDirectClass;
typedef struct _EDataBookDirectPrivate EDataBookDirectPrivate;

struct _EDataBookDirect {
	GObject parent;
	EDataBookDirectPrivate *priv;
};

struct _EDataBookDirectClass {
	GObjectClass parent;
};

GType			e_data_book_direct_get_type		 (void);
EDataBookDirect *	e_data_book_direct_new			 (const gchar *backend_path,
								  const gchar *backend_factory_name,
								  const gchar *config);

gboolean                e_data_book_direct_register_gdbus_object (EDataBookDirect *direct,
								  GDBusConnection *connection,
								  const gchar *object_path,
								  GError **error);

G_END_DECLS

#endif /* E_DATA_BOOK_DIRECT_H */
