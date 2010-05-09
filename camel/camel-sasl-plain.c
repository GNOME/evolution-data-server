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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-sasl-plain.h"
#include "camel-service.h"

#define CAMEL_SASL_PLAIN_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SASL_PLAIN, CamelSaslPlainPrivate))

struct _CamelSaslPlainPrivate {
	gint placeholder;  /* allow for future expansion */
};

CamelServiceAuthType camel_sasl_plain_authtype = {
	N_("PLAIN"),

	N_("This option will connect to the server using a "
	   "simple password."),

	"PLAIN",
	TRUE
};

G_DEFINE_TYPE (CamelSaslPlain, camel_sasl_plain, CAMEL_TYPE_SASL)

static GByteArray *
sasl_plain_challenge (CamelSasl *sasl,
                      GByteArray *token,
                      GError **error)
{
	GByteArray *buf = NULL;
	CamelService *service;
	CamelURL *url;

	service = camel_sasl_get_service (sasl);
	url = service->url;
	g_return_val_if_fail (url->passwd != NULL, NULL);

	/* FIXME: make sure these are "UTF8-SAFE" */
	buf = g_byte_array_new ();
	g_byte_array_append (buf, (guint8 *) "", 1);
	g_byte_array_append (buf, (guint8 *) url->user, strlen (url->user));
	g_byte_array_append (buf, (guint8 *) "", 1);
	g_byte_array_append (buf, (guint8 *) url->passwd, strlen (url->passwd));

	camel_sasl_set_authenticated (sasl, TRUE);

	return buf;
}

static void
camel_sasl_plain_class_init (CamelSaslPlainClass *class)
{
	CamelSaslClass *sasl_class;

	g_type_class_add_private (class, sizeof (CamelSaslPlainPrivate));

	sasl_class = CAMEL_SASL_CLASS (class);
	sasl_class->challenge = sasl_plain_challenge;
}

static void
camel_sasl_plain_init (CamelSaslPlain *sasl)
{
	sasl->priv = CAMEL_SASL_PLAIN_GET_PRIVATE (sasl);
}
