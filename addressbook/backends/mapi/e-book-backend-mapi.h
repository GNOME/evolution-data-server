/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Srinivasa Ragavan <sragavan@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __E_BOOK_BACKEND_MAPI_H__
#define __E_BOOK_BACKEND_MAPI_H__

#include <libedata-book/e-book-backend.h>
#include <libedata-book/e-book-backend-sync.h>
#include "exchange-mapi-connection.h"

/* #include "db.h" */


#define E_TYPE_BOOK_BACKEND_MAPI         (e_book_backend_mapi_get_type ())
#define E_BOOK_BACKEND_MAPI(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_MAPI, EBookBackendMAPI))
#define E_BOOK_BACKEND_MAPI_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_MAPI, EBookBackendMAPIClass))
#define E_IS_BOOK_BACKEND_MAPI(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_MAPI))
#define E_IS_BOOK_BACKEND_MAPI_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_MAPI))
#define E_BOOK_BACKEND_MAPI_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_MAPI, EBookBackendMAPIClass))

typedef struct _EBookBackendMAPIPrivate EBookBackendMAPIPrivate;

typedef struct
{
	EBookBackend             parent_object;
	EBookBackendMAPIPrivate *priv;
} EBookBackendMAPI;

typedef struct
{
	EBookBackendClass parent_class;
} EBookBackendMAPIClass;

EBookBackend *e_book_backend_mapi_new      (void);
GType         e_book_backend_mapi_get_type (void);

#endif /* ! __E_BOOK_BACKEND_MAPI_H__ */

