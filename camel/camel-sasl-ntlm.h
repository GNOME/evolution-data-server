/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SASL_NTLM_H
#define CAMEL_SASL_NTLM_H

#include <camel/camel-sasl.h>

#define CAMEL_SASL_NTLM_TYPE     (camel_sasl_ntlm_get_type ())
#define CAMEL_SASL_NTLM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_SASL_NTLM_TYPE, CamelSaslNTLM))
#define CAMEL_SASL_NTLM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_SASL_NTLM_TYPE, CamelSaslNTLMClass))
#define CAMEL_IS_SASL_NTLM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_SASL_NTLM_TYPE))

G_BEGIN_DECLS

typedef struct _CamelSaslNTLM CamelSaslNTLM;
typedef struct _CamelSaslNTLMClass CamelSaslNTLMClass;

struct _CamelSaslNTLM {
	CamelSasl parent;
};

struct _CamelSaslNTLMClass {
	CamelSaslClass parent_class;
};

CamelType camel_sasl_ntlm_get_type (void);

extern CamelServiceAuthType camel_sasl_ntlm_authtype;

G_END_DECLS

#endif /* CAMEL_SASL_NTLM_H */
