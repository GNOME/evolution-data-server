/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* e-book-backend-file.h - File contact backend.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Nat Friedman <nat@novell.com>
 *          Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
 */

#ifndef __E_BOOK_BACKEND_FILE_H__
#define __E_BOOK_BACKEND_FILE_H__

#include <libedata-book/e-book-backend-sync.h>

#define E_TYPE_BOOK_BACKEND_FILE        (e_book_backend_file_get_type ())
#define E_BOOK_BACKEND_FILE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFile))
#define E_BOOK_BACKEND_FILE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFileClass))
#define E_IS_BOOK_BACKEND_FILE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_FILE))
#define E_IS_BOOK_BACKEND_FILE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_FILE))
#define E_BOOK_BACKEND_FILE_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_FILE, EBookBackendFileClass))

typedef struct _EBookBackendFilePrivate EBookBackendFilePrivate;

typedef struct {
	EBookBackendSync         parent_object;
	EBookBackendFilePrivate *priv;
} EBookBackendFile;

typedef struct {
	EBookBackendSyncClass parent_class;
} EBookBackendFileClass;

EBookBackend *e_book_backend_file_new      (void);
GType       e_book_backend_file_get_type (void);

#endif /* ! __E_BOOK_BACKEND_FILE_H__ */

