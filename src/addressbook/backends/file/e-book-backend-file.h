/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-FileCopyrightText: (C) 2012 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Nat Friedman <nat@novell.com>
 * SPDX-FileContributor: Chris Toshok <toshok@ximian.com>
 * SPDX-FileContributor: Hans Petter Jansson <hpj@novell.com>
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef E_BOOK_BACKEND_FILE_H
#define E_BOOK_BACKEND_FILE_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_FILE \
	(e_book_backend_file_get_type ())
#define E_BOOK_BACKEND_FILE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFile))
#define E_BOOK_BACKEND_FILE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFileClass))
#define E_IS_BOOK_BACKEND_FILE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_FILE))
#define E_IS_BOOK_BACKEND_FILE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_FILE))
#define E_BOOK_BACKEND_FILE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFileClass))

G_BEGIN_DECLS

typedef struct _EBookBackendFile EBookBackendFile;
typedef struct _EBookBackendFileClass EBookBackendFileClass;
typedef struct _EBookBackendFilePrivate EBookBackendFilePrivate;

struct _EBookBackendFile {
	EBookBackendSync parent;
	EBookBackendFilePrivate *priv;
};

struct _EBookBackendFileClass {
	EBookBackendSyncClass parent_class;
};

GType		e_book_backend_file_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_FILE_H */

