/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-smtp-provider.c: smtp provider registration code */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <camel/camel.h>
#include <glib/gi18n-lib.h>

#include "camel-smtp-transport.h"

static guint smtp_url_hash (gconstpointer key);
static gint smtp_url_equal (gconstpointer a, gconstpointer b);

static CamelProvider smtp_provider = {
	"smtp",
	N_("SMTP"),

	N_("For delivering mail by connecting to a remote mailhub "
	   "using SMTP."),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_SUPPORTS_SSL,

	CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH | CAMEL_URL_ALLOW_USER,

	/* ... */
};

void
camel_provider_module_init(void)
{
	smtp_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_smtp_transport_get_type ();
	smtp_provider.authtypes = g_list_append (camel_sasl_authtype_list (TRUE), camel_sasl_authtype ("LOGIN"));
	smtp_provider.authtypes = g_list_append (smtp_provider.authtypes, camel_sasl_authtype ("POPB4SMTP"));
	smtp_provider.url_hash = smtp_url_hash;
	smtp_provider.url_equal = smtp_url_equal;
	smtp_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register(&smtp_provider);
}

static void
add_hash (guint *hash, gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
smtp_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *)key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
check_equal (gchar *s1, gchar *s2)
{
	if (s1 == NULL) {
		if (s2 == NULL)
			return TRUE;
		else
			return FALSE;
	}

	if (s2 == NULL)
		return FALSE;

	return strcmp (s1, s2) == 0;
}

static gint
smtp_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return check_equal (u1->protocol, u2->protocol)
		&& check_equal (u1->user, u2->user)
		&& check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
