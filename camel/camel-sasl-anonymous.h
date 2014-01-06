/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
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
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_ANONYMOUS_H
#define CAMEL_SASL_ANONYMOUS_H

#include <camel/camel-enums.h>
#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_ANONYMOUS \
	(camel_sasl_anonymous_get_type ())
#define CAMEL_SASL_ANONYMOUS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_ANONYMOUS, CamelSaslAnonymous))
#define CAMEL_SASL_ANONYMOUS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_ANONYMOUS, CamelSaslAnonymousClass))
#define CAMEL_IS_SASL_ANONYMOUS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_ANONYMOUS))
#define CAMEL_IS_SASL_ANONYMOUS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_ANONYMOUS))
#define CAMEL_SASL_ANONYMOUS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_ANONYMOUS, CamelSaslAnonymousClass))

G_BEGIN_DECLS

typedef struct _CamelSaslAnonymous CamelSaslAnonymous;
typedef struct _CamelSaslAnonymousClass CamelSaslAnonymousClass;

struct _CamelSaslAnonymous {
	CamelSasl parent;

	gchar *trace_info;
	CamelSaslAnonTraceType type;
};

struct _CamelSaslAnonymousClass {
	CamelSaslClass parent_class;
};

GType camel_sasl_anonymous_get_type (void);

/* public methods */
CamelSasl *camel_sasl_anonymous_new (CamelSaslAnonTraceType type, const gchar *trace_info);

G_END_DECLS

#endif /* CAMEL_SASL_ANONYMOUS_H */
