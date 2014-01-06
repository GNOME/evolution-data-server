/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* e-book-backend-ldap.h - LDAP contact backend.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
 */

#ifndef E_BOOK_BACKEND_LDAP_H
#define E_BOOK_BACKEND_LDAP_H

#include <libedata-book/libedata-book.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_LDAP \
	(e_book_backend_ldap_get_type ())
#define E_BOOK_BACKEND_LDAP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_LDAP, EBookBackendLDAP))
#define E_BOOK_BACKEND_LDAP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_LDAP, EBookBackendLDAPClass))
#define E_IS_BOOK_BACKEND_LDAP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_LDAP))
#define E_IS_BOOK_BACKEND_LDAP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_LDAP))
#define E_BOOK_BACKEND_LDAP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BOOK_BACKEND_LDAP, EBookBackendLDAPClass))

G_BEGIN_DECLS

typedef struct _EBookBackendLDAP EBookBackendLDAP;
typedef struct _EBookBackendLDAPClass EBookBackendLDAPClass;
typedef struct _EBookBackendLDAPPrivate EBookBackendLDAPPrivate;

struct _EBookBackendLDAP {
	EBookBackend parent;
	EBookBackendLDAPPrivate *priv;
};

struct _EBookBackendLDAPClass {
	EBookBackendClass parent_class;
};

GType		e_book_backend_ldap_get_type	(void);

G_END_DECLS

#endif /* E_BOOK_BACKEND_LDAP_H */

