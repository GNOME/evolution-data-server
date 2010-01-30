/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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
 */

#ifndef _CAMEL_NNTP_ADDRESS_H
#define _CAMEL_NNTP_ADDRESS_H

#include <camel/camel-address.h>

#define CAMEL_NNTP_ADDRESS(obj)         CAMEL_CHECK_CAST (obj, camel_nntp_address_get_type (), CamelNNTPAddress)
#define CAMEL_NNTP_ADDRESS_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_nntp_address_get_type (), CamelNNTPAddressClass)
#define CAMEL_IS_NNTP_ADDRESS(obj)      CAMEL_CHECK_TYPE (obj, camel_nntp_address_get_type ())

G_BEGIN_DECLS

typedef struct _CamelNNTPAddress CamelNNTPAddress;
typedef struct _CamelNNTPAddressClass CamelNNTPAddressClass;

struct _CamelNNTPAddress {
	CamelAddress parent;

	struct _CamelNNTPAddressPrivate *priv;
};

struct _CamelNNTPAddressClass {
	CamelAddressClass parent_class;
};

CamelType		camel_nntp_address_get_type	(void);
CamelNNTPAddress   *camel_nntp_address_new	(void);

gint			camel_nntp_address_add	(CamelNNTPAddress *a, const gchar *name);
gboolean		camel_nntp_address_get	(const CamelNNTPAddress *a, gint index, const gchar **namep);

G_END_DECLS

#endif /* _CAMEL_NNTP_ADDRESS_H */
