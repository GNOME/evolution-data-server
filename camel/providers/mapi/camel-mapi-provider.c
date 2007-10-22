/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *  Copyright (C) Remi L'Ecolier 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "oc.h"
#include <string.h>
#include <gmodule.h>
#include <camel/camel-provider.h>
#include <camel/camel-session.h>
#include <camel/camel-url.h>
#include <camel/camel-sasl.h>
#include <camel/camel-i18n.h>
#include "camel-mapi-store.h"
#include "camel-mapi-transport.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void	add_hash (guint *, char *);
static guint	openchange_url_hash (gconstpointer);
static gint	check_equal (char *, char *);
static gint	openchange_url_equal (gconstpointer, gconstpointer);


static CamelProvider openchange_provider = {
	"mapi",	/*  protocole this string is showable in the account view */
	"Exchange MAPI",	/* name this string is showable in the server list */

	N_("For accessing Microsoft Exchange / OpenChange servers using MAPI"),	/* a description of what the provider do is showable under the type of the selected srv */
	"mail",	/* conserne the domaine of application in the most of the case mail or news */
	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_DISABLE_SENT_FOLDER, /* a list of flag who discribe the provider if enable display widget in aaccount wizard */
	CAMEL_URL_NEED_USER|CAMEL_URL_NEED_HOST,	/* flag if enable display widget in account wizard(host entry , user entry etc) */
	0,	/* array who descibe optional information about data storage */
	/* ... */
};

CamelServiceAuthType camel_openchange_password_authtype = {
	N_("Password"),
	N_("This option will connect to the Openchange server using a plaintext password."),
	"",
	TRUE
};

static int openchange_auto_detect_cb(CamelURL *url, GHashTable **auto_detected, CamelException *ex)
{
	printf("openchange_auto_detect_cb\n");
	*auto_detected = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (*auto_detected, g_strdup ("poa"), g_strdup (url->host));
	return 0;
}

int m_oc_initialize(void)
{
	enum MAPISTATUS	retval;
	const char	*profname = NULL;
	FILE		*fop;
	char		*profpath;

#if 0
	MAPIUninitialize();
	profpath = g_strdup_printf("%s/%s", getenv("HOME"), PATH_LDB);
	if ((fop = fopen(profpath,"r")) != NULL){
		fclose (fop);
		if (MAPIInitialize(profpath) == -1){
			g_free(profpath);
			retval = GetLastError();
/* 			mapi_errstr("MAPIInitialize", GetLastError()); */
			if (retval == MAPI_E_SESSION_LIMIT){
				printf("GOooooooooooooooooooooooooo\n");
				MAPIUninitialize();
				return (m_oc_initialize());
			}            
		}
		if ((retval = GetDefaultProfile(&profname)) != MAPI_E_SUCCESS) {
/* 			mapi_errstr("GetDefaultProfile", GetLastError()); */
			return -1;
		}
/* 		mapi_errstr("GetDefaultProfile", GetLastError()); */
		retval = MapiLogonEx(&global_mapi_ctx->session, profname, NULL);
/* 		mapi_errstr("MapiLogonEx", GetLastError()); */
		MAPI_RETVAL_IF(retval, retval, NULL);
		return (0);
	}
	else {
		g_free(profpath);
	}
	return (-1);
#endif
	if (!exchange_mapi_connection_new (NULL, NULL))
		return -1;

	return 0;
}


void camel_provider_module_init(void)
{
	openchange_provider.name = "Exchange MAPI";
	openchange_provider.extra_conf = NULL;
	openchange_provider.auto_detect = openchange_auto_detect_cb;
	openchange_provider.authtypes = g_list_prepend (openchange_provider.authtypes, &camel_openchange_password_authtype);
	openchange_provider.url_hash = openchange_url_hash;
	openchange_provider.url_equal = openchange_url_equal;
	openchange_provider.license = "GPL";
	openchange_provider.object_types[CAMEL_PROVIDER_STORE] = camel_openchange_store_get_type();
	openchange_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_openchange_transport_get_type();
	camel_provider_register (&openchange_provider);
	m_oc_initialize();
}

static void add_hash(guint *hash, char *s)
{
	if (s) {
		*hash ^= g_str_hash(s);
	}
}

static guint openchange_url_hash(gconstpointer key)
{
	const CamelURL	*u = (CamelURL *)key;
	guint		hash = 0;

	add_hash (&hash, u->user);
	add_hash (&hash, u->authmech);
	add_hash (&hash, u->host);
	hash ^= u->port;

	return hash;
}

static gint check_equal(char *s1, char *s2)
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

static gint openchange_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL	*u1 = a;
	const CamelURL	*u2 = b;
  
	return check_equal (u1->protocol, u2->protocol)
		&& check_equal (u1->user, u2->user)
		&& check_equal (u1->authmech, u2->authmech)
		&& check_equal (u1->host, u2->host)
		&& u1->port == u2->port;
}
