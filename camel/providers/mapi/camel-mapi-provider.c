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

#include "oc.h"

#define d(x) x

static void add_hash (guint *, char *);
static guint mapi_url_hash (gconstpointer);
static gint check_equal (char *, char *);
static gint mapi_url_equal (gconstpointer, gconstpointer);


static CamelProvider mapi_provider = {
	"mapi",	

	"Exchange MAPI", 

	N_("For accessing Microsoft Exchange / OpenChange servers using MAPI"),	

	"mail",	

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_DISABLE_SENT_FOLDER, 

	CAMEL_URL_NEED_USER | CAMEL_URL_NEED_HOST,

	0,

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

gboolean
mapi_initialize(void)
{
	if (!exchange_mapi_connection_new (NULL, NULL))
		return FALSE;

	return TRUE;
}


void
camel_provider_module_init(void)
{
	mapi_provider.name = "Exchange MAPI";
	mapi_provider.extra_conf = NULL;
	mapi_provider.auto_detect = mapi_auto_detect_cb;
	mapi_provider.authtypes = g_list_prepend (mapi_provider.authtypes, &camel_mapi_password_authtype);
	mapi_provider.url_hash = mapi_url_hash;
	mapi_provider.url_equal = mapi_url_equal;
	mapi_provider.license = "LGPL";
	mapi_provider.object_types[CAMEL_PROVIDER_STORE] = camel_mapi_store_get_type();
	mapi_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_openchange_transport_get_type();
	camel_provider_register (&mapi_provider);
	mapi_initialize();
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
