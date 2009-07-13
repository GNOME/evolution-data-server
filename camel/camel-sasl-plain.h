/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 */

#ifndef CAMEL_SASL_PLAIN_H
#define CAMEL_SASL_PLAIN_H

#include <camel/camel-sasl.h>

#define CAMEL_SASL_PLAIN_TYPE     (camel_sasl_plain_get_type ())
#define CAMEL_SASL_PLAIN(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SASL_PLAIN_TYPE, CamelSaslPlain))
#define CAMEL_SASL_PLAIN_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SASL_PLAIN_TYPE, CamelSaslPlainClass))
#define CAMEL_IS_SASL_PLAIN(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SASL_PLAIN_TYPE))

G_BEGIN_DECLS

typedef struct _CamelSaslPlain {
	CamelSasl parent_object;

} CamelSaslPlain;

typedef struct _CamelSaslPlainClass {
	CamelSaslClass parent_class;

} CamelSaslPlainClass;

/* Standard Camel function */
CamelType camel_sasl_plain_get_type (void);

extern CamelServiceAuthType camel_sasl_plain_authtype;

G_END_DECLS

#endif /* CAMEL_SASL_PLAIN_H */
