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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_ADDRESS_H
#define CAMEL_ADDRESS_H

#include <camel/camel-object.h>

/* Standard GObject macros */
#define CAMEL_TYPE_ADDRESS \
	(camel_address_get_type ())
#define CAMEL_ADDRESS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_ADDRESS, CamelAddress))
#define CAMEL_ADDRESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_ADDRESS, CamelAddressClass))
#define CAMEL_IS_ADDRESS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_ADDRESS))
#define CAMEL_IS_ADDRESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_ADDRESS))
#define CAMEL_ADDRESS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_ADDRESS, CamelAddressClass))

G_BEGIN_DECLS

typedef struct _CamelAddress CamelAddress;
typedef struct _CamelAddressClass CamelAddressClass;
typedef struct _CamelAddressPrivate CamelAddressPrivate;

struct _CamelAddress {
	CamelObject parent;

	GPtrArray *addresses;

	CamelAddressPrivate *priv;
};

struct _CamelAddressClass {
	CamelObjectClass parent_class;

	gint		(*decode)		(CamelAddress *addr,
						 const gchar *raw);
	gchar *		(*encode)		(CamelAddress *addr);
	gint		(*unformat)		(CamelAddress *addr,
						 const gchar *raw);
	gchar *		(*format)		(CamelAddress *addr);
	gint		(*cat)			(CamelAddress *dest,
						 CamelAddress *source);
	void		(*remove)		(CamelAddress *addr,
						 gint index);
};

GType		camel_address_get_type		(void);
CamelAddress *	camel_address_new		(void);
CamelAddress *	camel_address_new_clone		(CamelAddress *addr);
gint		camel_address_length		(CamelAddress *addr);
gint		camel_address_decode		(CamelAddress *addr,
						 const gchar *raw);
gchar *		camel_address_encode		(CamelAddress *addr);
gint		camel_address_unformat		(CamelAddress *addr,
						 const gchar *raw);
gchar *		camel_address_format		(CamelAddress *addr);
gint		camel_address_cat		(CamelAddress *dest,
						 CamelAddress *source);
gint		camel_address_copy		(CamelAddress *dest,
						 CamelAddress *source);
void		camel_address_remove		(CamelAddress *addr,
						 gint index);

G_END_DECLS

#endif /* CAMEL_ADDRESS_H */
