/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <NotZed@ximian.com>
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

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef _CAMEL_NEWS_ADDRESS_H
#define _CAMEL_NEWS_ADDRESS_H

#include <camel/camel-address.h>

#define CAMEL_NEWS_ADDRESS(obj)         CAMEL_CHECK_CAST (obj, camel_news_address_get_type (), CamelNewsAddress)
#define CAMEL_NEWS_ADDRESS_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_news_address_get_type (), CamelNewsAddressClass)
#define CAMEL_IS_NEWS_ADDRESS(obj)      CAMEL_CHECK_TYPE (obj, camel_news_address_get_type ())

G_BEGIN_DECLS

typedef struct _CamelNewsAddressClass CamelNewsAddressClass;

struct _CamelNewsAddress {
	CamelAddress parent;

	struct _CamelNewsAddressPrivate *priv;
};

struct _CamelNewsAddressClass {
	CamelAddressClass parent_class;
};

CamelType		camel_news_address_get_type	(void);
CamelNewsAddress      *camel_news_address_new	(void);

G_END_DECLS

#endif /* _CAMEL_NEWS_ADDRESS_H */

#endif /* CAMEL_DISABLE_DEPRECATED */
