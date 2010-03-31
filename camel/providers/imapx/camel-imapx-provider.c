/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-provider.c: pop3 provider registration code */

/*
 * Authors :
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>

#include "camel/camel-provider.h"
#include "camel/camel-session.h"
#include "camel/camel-url.h"
#include "camel/camel-sasl.h"

#include "camel-imapx-store.h"

static guint imapx_url_hash (gconstpointer key);
static gint  imapx_url_equal (gconstpointer a, gconstpointer b);

CamelProviderConfEntry imapx_conf_entries[] = {
		{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for New Mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use_idle", NULL,
	  N_("Use I_dle if the server supports it"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check_all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check_lsub", NULL,
	  N_("Ch_eck for new messages in subscribed folders"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
#if 0	
	{ CAMEL_PROVIDER_CONF_SECTION_START, "cmdsection", NULL,
	  N_("Connection to Server") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use_command", NULL,
	  N_("_Use custom command to connect to server"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "command", "use_command",
	  N_("Command:"), "ssh -C -l %u %h exec /usr/sbin/imapd" },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "cachedconn", NULL,
	  N_("Numbe_r of cached connections to use"), "y:1:5:7" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
#endif
	{ CAMEL_PROVIDER_CONF_SECTION_START, "folders", NULL,
	  N_("Folders") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "use_lsub", NULL,
	  N_("_Show only subscribed folders"), "1" },
#if 0	
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "override_namespace", NULL,
	  N_("O_verride server-supplied folder namespace"), "0" },
	{ CAMEL_PROVIDER_CONF_ENTRY, "namespace", "override_namespace",
	  N_("Namespace:") },
#endif	
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "general", NULL, N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter", NULL,
	  N_("_Apply filters to new messages in INBOX on this server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk", NULL,
	  N_("Check new messages for Jun_k contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk_inbox", "filter_junk",
	  N_("Only check for Junk messages in the IN_BOX folder"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "sync_offline", NULL,
	  N_("Automatically synchroni_ze remote mail locally"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider imapx_provider = {
	"imapx",

	N_("IMAP+"),

	N_("For reading and storing mail on IMAP servers."),
	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_SUPPORTS_SSL,

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST | CAMEL_URL_ALLOW_AUTH,

	imapx_conf_entries,

	/* ... */
};

CamelServiceAuthType camel_imapx_password_authtype = {
	N_("Password"),

	N_("This option will connect to the IMAP server using a "
	   "plaintext password."),

	"",
	TRUE
};

void camel_imapx_module_init(void);

extern void camel_exception_setup(void);
extern void imapx_utils_init(void);

void
camel_imapx_module_init(void)
{
	imapx_provider.object_types[CAMEL_PROVIDER_STORE] = camel_imapx_store_get_type();
	imapx_provider.url_hash = imapx_url_hash;
	imapx_provider.url_equal = imapx_url_equal;
	imapx_provider.authtypes = camel_sasl_authtype_list(FALSE);
	imapx_provider.authtypes = g_list_prepend(imapx_provider.authtypes, &camel_imapx_password_authtype);
	imapx_provider.translation_domain = GETTEXT_PACKAGE;

	/* TEMPORARY */
	camel_exception_setup();
	imapx_utils_init();

	camel_provider_register(&imapx_provider);
}

void
camel_provider_module_init(void)
{
	camel_imapx_module_init();
}

static void
imapx_add_hash (guint *hash, gchar *s)
{
	if (s)
		*hash ^= g_str_hash(s);
}

static guint
imapx_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *)key;
	guint hash = 0;

	imapx_add_hash (&hash, u->user);
	imapx_add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
imapx_check_equal (gchar *s1, gchar *s2)
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
imapx_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return imapx_check_equal (u1->protocol, u2->protocol)
		&& imapx_check_equal (u1->user, u2->user)
		&& imapx_check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
