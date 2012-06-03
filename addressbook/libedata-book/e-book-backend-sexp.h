/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-backend-card-sexp.h
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_BACKEND_SEXP_H
#define E_BOOK_BACKEND_SEXP_H

#include <libebook/libebook.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_SEXP \
	(e_book_backend_sexp_get_type ())
#define E_BOOK_BACKEND_SEXP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_SEXP, EBookBackendSExp))
#define E_BOOK_BACKEND_SEXP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_SEXP, EBookBackendSExpClass))
#define E_IS_BOOK_BACKEND_SEXP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_SEXP))
#define E_IS_BOOK_BACKEND_SEXP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_SEXP))
#define E_BOOK_BACKEND_SEXP_GET_CLASS(cls) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_SEXP, EBookBackendSExpClass))

G_BEGIN_DECLS

typedef struct _EBookBackendSExp EBookBackendSExp;
typedef struct _EBookBackendSExpClass EBookBackendSExpClass;
typedef struct _EBookBackendSExpPrivate EBookBackendSExpPrivate;

struct _EBookBackendSExp {
	GObject parent_object;
	EBookBackendSExpPrivate *priv;
};

struct _EBookBackendSExpClass {
	GObjectClass parent_class;
};

GType		e_book_backend_sexp_get_type	(void) G_GNUC_CONST;
EBookBackendSExp *
		e_book_backend_sexp_new		(const gchar *text);
gboolean	e_book_backend_sexp_match_vcard	(EBookBackendSExp *sexp,
						 const gchar *vcard);
gboolean	e_book_backend_sexp_match_contact
						(EBookBackendSExp *sexp,
						 EContact *contact);

G_END_DECLS

#endif /* E_BOOK_BACKEND_SEXP_H */
