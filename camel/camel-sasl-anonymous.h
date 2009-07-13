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

#ifndef CAMEL_SASL_ANONYMOUS_H
#define CAMEL_SASL_ANONYMOUS_H

#include <camel/camel-sasl.h>

#define CAMEL_SASL_ANONYMOUS_TYPE     (camel_sasl_anonymous_get_type ())
#define CAMEL_SASL_ANONYMOUS(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SASL_ANONYMOUS_TYPE, CamelSaslAnonymous))
#define CAMEL_SASL_ANONYMOUS_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SASL_ANONYMOUS_TYPE, CamelSaslAnonymousClass))
#define CAMEL_IS_SASL_ANONYMOUS(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SASL_ANONYMOUS_TYPE))

G_BEGIN_DECLS

typedef enum {
	CAMEL_SASL_ANON_TRACE_EMAIL,
	CAMEL_SASL_ANON_TRACE_OPAQUE,
	CAMEL_SASL_ANON_TRACE_EMPTY
} CamelSaslAnonTraceType;

typedef struct _CamelSaslAnonymous {
	CamelSasl parent_object;

	gchar *trace_info;
	CamelSaslAnonTraceType type;
} CamelSaslAnonymous;

typedef struct _CamelSaslAnonymousClass {
	CamelSaslClass parent_class;

} CamelSaslAnonymousClass;

/* Standard Camel function */
CamelType camel_sasl_anonymous_get_type (void);

/* public methods */
CamelSasl *camel_sasl_anonymous_new (CamelSaslAnonTraceType type, const gchar *trace_info);

extern CamelServiceAuthType camel_sasl_anonymous_authtype;

G_END_DECLS

#endif /* CAMEL_SASL_ANONYMOUS_H */
