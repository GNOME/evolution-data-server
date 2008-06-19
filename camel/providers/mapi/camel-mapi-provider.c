/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <string.h>

#include <gmodule.h>

#include <camel/camel-provider.h>
#include <camel/camel-session.h>
#include <camel/camel-url.h>
#include <camel/camel-sasl.h>
#include <camel/camel-i18n.h>

#include "camel-mapi-store.h"
#include "camel-mapi-transport.h"

#define d(x) x

static void add_hash (guint *, char *);
static guint mapi_url_hash (gconstpointer);
static gint check_equal (char *, char *);
static gint mapi_url_equal (gconstpointer, gconstpointer);

static CamelProviderConfEntry mapi_conf_entries[] = {
	{ CAMEL_PROVIDER_CONF_SECTION_START, "mailcheck", NULL,
	  N_("Checking for New Mail") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "check_all", NULL,
	  N_("C_heck for new messages in all folders"), "1" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },

/* 	/\* override the labels/defaults of the standard settings *\/ */
/* 	{ CAMEL_PROVIDER_CONF_LABEL, "username", NULL, */
/* 	  /\* i18n: the '_' should appear before the same letter it */
/* 	     does in the evolution:mail-config.glade "User_name" */
/* 	     translation (or not at all) *\/ */
/* 	  N_("Windows User_name:") }, */

	/* extra Exchange configuration settings */
	{ CAMEL_PROVIDER_CONF_SECTION_START, "activedirectory", NULL,
	  /* i18n: GAL is an Outlookism, AD is a Windowsism */
	  N_("Global Address List / Active Directory") },
	{ CAMEL_PROVIDER_CONF_ENTRY, "ad_server", NULL,
	  /* i18n: "Global Catalog" is a Windowsism, but it's a
	     technical term and may not have translations? */
	  N_("_Global Catalog server name:") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "ad_limit", NULL,
	  N_("_Limit number of GAL responses: %s"), "y:1:500:10000" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "generals", NULL,
	  N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "sync_offline", NULL,
	  N_("Automatically synchroni_ze account locally"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter", NULL,
	  /* i18n: copy from evolution:camel-imap-provider.c */
	  N_("_Apply filters to new messages in Inbox on this server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk", NULL,
	  N_("Check new messages for _Junk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk_inbox", "filter_junk",
	  N_("Only check for Junk messag_es in the Inbox folder"), "0" },

	 	
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_END }
};

static CamelProvider mapi_provider = {
	"mapi",	

	"Exchange MAPI", 

	N_("For accessing Microsoft Exchange / OpenChange servers using MAPI"),	

	"mail",	

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_DISABLE_SENT_FOLDER | CAMEL_PROVIDER_IS_EXTERNAL, 

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST,

	mapi_conf_entries,

	/* ... */
};

CamelServiceAuthType camel_mapi_password_authtype = {
	N_("Password"),
	N_("This option will connect to the Openchange server using a plaintext password."),
	"",
	TRUE
};

static int 
mapi_auto_detect_cb(CamelURL *url, GHashTable **auto_detected, CamelException *ex)
{
        d (printf("mapi_auto_detect_cb\n"));
	*auto_detected = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (*auto_detected, g_strdup ("poa"), g_strdup (url->host));

	return 0;
}

void
camel_provider_module_init(void)
{
	mapi_provider.name = "Exchange MAPI";
	mapi_provider.auto_detect = mapi_auto_detect_cb;
	mapi_provider.authtypes = g_list_prepend (mapi_provider.authtypes, &camel_mapi_password_authtype);
	mapi_provider.url_hash = mapi_url_hash;
	mapi_provider.url_equal = mapi_url_equal;
	mapi_provider.license = "LGPL";
	mapi_provider.object_types[CAMEL_PROVIDER_STORE] = camel_mapi_store_get_type();
	mapi_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_mapi_transport_get_type();
	camel_provider_register (&mapi_provider);
}

static void
add_hash(guint *hash, char *s)
{
	if (s) {
		*hash ^= g_str_hash(s);
	}
}

static guint
mapi_url_hash(gconstpointer key)
{
	const CamelURL	*u = (CamelURL *)key;
	guint		hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->authmech);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint
check_equal(char *s1, char *s2)
{
	if (s1 == NULL) {
		if (s2 == NULL) {
			return TRUE;
		} else {
			return FALSE;
		}
	}
	if (s2 == NULL) {
		return FALSE;
	}

	return strcmp (s1, s2) == 0;
}

static gint
mapi_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL	*u1 = a;
	const CamelURL	*u2 = b;
  
	return check_equal (u1->protocol, u2->protocol)
		&& check_equal (u1->user, u2->user)
		&& check_equal (u1->authmech, u2->authmech)
		&& check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
