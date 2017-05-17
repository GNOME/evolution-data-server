/* e-book-backend-google.h - Google contact backendy.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>
 */

#ifndef E_BOOK_BACKEND_GOOGLE_H
#define E_BOOK_BACKEND_GOOGLE_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_GOOGLE \
	(e_book_backend_google_get_type ())
#define E_BOOK_BACKEND_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGoogle))
#define E_BOOK_BACKEND_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGoogleClass))
#define E_IS_BOOK_BACKEND_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_GOOGLE))
#define E_IS_BOOK_BACKEND_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_GOOGLE))
#define E_BOOK_BACKEND_GOOGLE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_GOOGLE, EBookBackendGoogleClass))

G_BEGIN_DECLS

typedef struct _EBookBackendGoogle EBookBackendGoogle;
typedef struct _EBookBackendGoogleClass EBookBackendGoogleClass;
typedef struct _EBookBackendGooglePrivate EBookBackendGooglePrivate;

struct _EBookBackendGoogle {
	EBookMetaBackend parent_object;
	EBookBackendGooglePrivate *priv;
};

struct _EBookBackendGoogleClass {
	EBookMetaBackendClass parent_class;
};

GType		e_book_backend_google_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_GOOGLE_H */
