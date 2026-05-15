/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_NTLM_H
#define CAMEL_SASL_NTLM_H

#include <camel/camel-sasl.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SASL_NTLM \
	(camel_sasl_ntlm_get_type ())
#define CAMEL_SASL_NTLM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SASL_NTLM, CamelSaslNTLM))
#define CAMEL_SASL_NTLM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SASL_NTLM, CamelSaslNTLMClass))
#define CAMEL_IS_SASL_NTLM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SASL_NTLM))
#define CAMEL_IS_SASL_NTLM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SASL_NTLM))
#define CAMEL_SASL_NTLM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SASL_NTLM, CamelSaslNTLMClass))

G_BEGIN_DECLS

typedef struct _CamelSaslNTLM CamelSaslNTLM;
typedef struct _CamelSaslNTLMClass CamelSaslNTLMClass;
typedef struct _CamelSaslNTLMPrivate CamelSaslNTLMPrivate;

struct _CamelSaslNTLM {
	CamelSasl parent;
	CamelSaslNTLMPrivate *priv;
};

struct _CamelSaslNTLMClass {
	CamelSaslClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType camel_sasl_ntlm_get_type (void);

G_END_DECLS

#endif /* CAMEL_SASL_NTLM_H */
