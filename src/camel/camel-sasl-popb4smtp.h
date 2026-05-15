/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_POPB4SMTP_H
#define CAMEL_SASL_POPB4SMTP_H

#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_POPB4SMTP \
	(camel_sasl_popb4smtp_get_type ())
#define CAMEL_SASL_POPB4SMTP(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_POPB4SMTP, CamelSaslPOPB4SMTP))
#define CAMEL_SASL_POPB4SMTP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_POPB4SMTP, CamelSaslPOPB4SMTPClass))
#define CAMEL_IS_SASL_POPB4SMTP(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_POPB4SMTP))
#define CAMEL_IS_SASL_POPB4SMTP_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_POPB4SMTP))
#define CAMEL_SASL_POPB4SMTP_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_POPB4SMTP, CamelSaslPOPB4SMTPClass))

G_BEGIN_DECLS

typedef struct _CamelSaslPOPB4SMTP CamelSaslPOPB4SMTP;
typedef struct _CamelSaslPOPB4SMTPClass CamelSaslPOPB4SMTPClass;
typedef struct _CamelSaslPOPB4SMTPPrivate CamelSaslPOPB4SMTPPrivate;

struct _CamelSaslPOPB4SMTP {
	CamelSasl parent;
	CamelSaslPOPB4SMTPPrivate *priv;
};

struct _CamelSaslPOPB4SMTPClass {
	CamelSaslClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_sasl_popb4smtp_get_type (void);

G_END_DECLS

#endif /* CAMEL_SASL_POPB4SMTP_H */
