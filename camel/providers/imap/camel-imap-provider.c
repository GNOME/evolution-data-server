/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-provider.c: imap provider registration code */

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

#include <config.h>

#include <string.h>
#include <camel/camel.h>
#include <glib/gi18n-lib.h>

#include "camel-imap-store.h"

static void add_hash (guint *hash, gchar *s);
static guint imap_url_hash (gconstpointer key);
static gint check_equal (gchar *s1, gchar *s2);
static gint imap_url_equal (gconstpointer a, gconstpointer b);

static CamelProviderConfEntry imap_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for New Mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check-subscribed", NULL,
	  N_("Ch_eck for new messages in subscribed folders"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "folders", NULL,
	  N_("Folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-subscriptions", NULL,
	  N_("_Show only subscribed folders"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use-namespace", NULL,
	  N_("O_verride server-supplied folder namespace"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "namespace", "use-namespace",
	  N_("Names_pace:") },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-all", NULL,
	  N_("Apply _filters to new messages in all folders"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-inbox", "!filter-all",
	  N_("_Apply filters to new messages in Inbox on this server"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk", NULL,
	  N_("Check new messages for _Junk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter-junk-inbox", "filter-junk",
	  N_("Only check for Junk messages in the IN_BOX folder"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "stay-synchronized", NULL,
	  N_("Automatically synchroni_ze remote mail locally"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

CamelProviderPortEntry imap_port_entries[] = {
	{ 143, N_("IMAP default port"), FALSE },
	{ 993, N_("IMAP over SSL"), TRUE },
	{ 0, NULL, 0 }
};

static CamelProvider imap_provider = {
	"imap",
	N_("IMAP"),

	N_("For reading and storing mail on IMAP servers."),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL |
	CAMEL_PROVIDER_ALLOW_REAL_TRASH_FOLDER | CAMEL_PROVIDER_ALLOW_REAL_JUNK_FOLDER,

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH,

	imap_conf_entries,

	imap_port_entries,

	/* ... */
};

CamelServiceAuthType camel_imap_password_authtype = {
	N_("Password"),

	N_("This option will connect to the IMAP server using a "
	   "plaintext password."),

	"",

	TRUE
};

void
camel_provider_module_init (void)
{
	imap_provider.object_types[CAMEL_PROVIDER_STORE] = camel_imap_store_get_type ();
	imap_provider.url_hash = imap_url_hash;
	imap_provider.url_equal = imap_url_equal;
	imap_provider.authtypes = camel_sasl_authtype_list (FALSE);
	imap_provider.authtypes = g_list_prepend (imap_provider.authtypes, &camel_imap_password_authtype);
	imap_provider.translation_domain = GETTEXT_PACKAGE;

	camel_provider_register (&imap_provider);
}

static void
add_hash (guint *hash,
          gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
imap_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *) key;
	guint hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
check_equal (gchar *s1,
             gchar *s2)
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
imap_url_equal (gconstpointer a,
                gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return check_equal (u1->protocol, u2->protocol)
		&& check_equal (u1->user, u2->user)
		&& check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
