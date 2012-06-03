/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* e-book-backend-file.h - File contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Nat Friedman <nat@novell.com>
 *          Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
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

