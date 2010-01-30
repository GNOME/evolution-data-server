/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_INTERNET_ADDRESS_H
#define _CAMEL_INTERNET_ADDRESS_H

#include <camel/camel-address.h>

#define CAMEL_INTERNET_ADDRESS(obj)         CAMEL_CHECK_CAST (obj, camel_internet_address_get_type (), CamelInternetAddress)
#define CAMEL_INTERNET_ADDRESS_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_internet_address_get_type (), CamelInternetAddressClass)
#define CAMEL_IS_INTERNET_ADDRESS(obj)      CAMEL_CHECK_TYPE (obj, camel_internet_address_get_type ())

G_BEGIN_DECLS

typedef struct _CamelInternetAddressClass CamelInternetAddressClass;

struct _CamelInternetAddress {
	CamelAddress parent;

	struct _CamelInternetAddressPrivate *priv;
};

struct _CamelInternetAddressClass {
	CamelAddressClass parent_class;
};

CamelType		camel_internet_address_get_type	(void);
CamelInternetAddress   *camel_internet_address_new	(void);

gint			camel_internet_address_add	(CamelInternetAddress *addr, const gchar *name, const gchar *address);
gboolean		camel_internet_address_get	(const CamelInternetAddress *addr, gint index, const gchar **namep, const gchar **addressp);

gint			camel_internet_address_find_name(CamelInternetAddress *addr, const gchar *name, const gchar **addressp);
gint			camel_internet_address_find_address(CamelInternetAddress *addr, const gchar *address, const gchar **namep);

/* utility functions, for network/display formatting */
gchar *			camel_internet_address_encode_address(gint *len, const gchar *name, const gchar *addr);
gchar *			camel_internet_address_format_address(const gchar *name, const gchar *addr);

G_END_DECLS

#endif /* _CAMEL_INTERNET_ADDRESS_H */
