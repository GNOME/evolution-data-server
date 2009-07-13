/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e2k-global-catalog-ldap.h"
#include "e2k-sid.h"
#include "e2k-utils.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef HAVE_LDAP_NTLM_BIND
#include "xntlm.h"
#endif

#ifdef E2K_DEBUG
static gboolean e2k_gc_debug = FALSE;
#define E2K_GC_DEBUG_MSG(x) if (e2k_gc_debug) printf x
#else
#define E2K_GC_DEBUG_MSG(x)
#endif

struct _E2kGlobalCatalogPrivate {
	GMutex *ldap_lock;
	LDAP *ldap;

	GPtrArray *entries;
	GHashTable *entry_cache, *server_cache;

	gchar *server, *user, *nt_domain, *password;
	E2kAutoconfigGalAuthPref auth;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void finalize (GObject *);
static gint get_gc_connection (E2kGlobalCatalog *gc, E2kOperation *op);

static void
class_init (GObjectClass *object_class)
{
#ifdef E2K_DEBUG
	gchar *e2k_debug = getenv ("E2K_DEBUG");

	if (e2k_debug && atoi (e2k_debug) > 3)
		e2k_gc_debug = TRUE;
#endif

	/* For some reason, sasl_client_init (called by ldap_init
	 * below) takes a *really* long time to scan the sasl modules
	 * when running under gdb. We're not using sasl anyway, so...
	 */
	g_setenv ("SASL_PATH", "", TRUE);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

static void
init (GObject *object)
{
	E2kGlobalCatalog *gc = E2K_GLOBAL_CATALOG (object);

	gc->priv = g_new0 (E2kGlobalCatalogPrivate, 1);
	gc->priv->ldap_lock = g_mutex_new ();
	gc->priv->entries = g_ptr_array_new ();
	gc->priv->entry_cache = g_hash_table_new (e2k_ascii_strcase_hash,
						  e2k_ascii_strcase_equal);
	gc->priv->server_cache = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
free_entry (E2kGlobalCatalogEntry *entry)
{
	gint i;

	g_free (entry->dn);
	g_free (entry->display_name);

	if (entry->sid)
		g_object_unref (entry->sid);

	g_free (entry->email);
	g_free (entry->mailbox);

	if (entry->delegates) {
		for (i = 0; i < entry->delegates->len; i++)
			g_free (entry->delegates->pdata[i]);
		g_ptr_array_free (entry->delegates, TRUE);
	}
	if (entry->delegators) {
		for (i = 0; i < entry->delegators->len; i++)
			g_free (entry->delegators->pdata[i]);
		g_ptr_array_free (entry->delegators, TRUE);
	}

	g_free (entry);
}

static void
free_server (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
	g_free (value);
}

static void
finalize (GObject *object)
{
	E2kGlobalCatalog *gc = E2K_GLOBAL_CATALOG (object);
	gint i;

	if (gc->priv) {
		if (gc->priv->ldap)
			ldap_unbind (gc->priv->ldap);

		for (i = 0; i < gc->priv->entries->len; i++)
			free_entry (gc->priv->entries->pdata[i]);
		g_ptr_array_free (gc->priv->entries, TRUE);

		g_hash_table_foreach (gc->priv->server_cache, free_server, NULL);
		g_hash_table_destroy (gc->priv->server_cache);

		g_free (gc->priv->server);
		g_free (gc->priv->user);
		g_free (gc->priv->nt_domain);
		if (gc->priv->password) {
			memset (gc->priv->password, 0, strlen (gc->priv->password));
			g_free (gc->priv->password);
		}

		g_mutex_free (gc->priv->ldap_lock);

		g_free (gc->priv);
		gc->priv = NULL;
	}

	g_free (gc->domain);
	gc->domain = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (e2k_global_catalog, E2kGlobalCatalog, class_init, init, PARENT_TYPE)

static gint
gc_ldap_result (LDAP *ldap, E2kOperation *op,
		gint msgid, LDAPMessage **msg)
{
	struct timeval tv;
	gint status, ldap_error;

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	*msg = NULL;
	do {
		status = ldap_result (ldap, msgid, TRUE, &tv, msg);
		if (status == -1) {
			ldap_get_option (ldap, LDAP_OPT_ERROR_NUMBER,
					 &ldap_error);
			return ldap_error;
		}
	} while (status == 0 && !e2k_operation_is_cancelled (op));

	if (e2k_operation_is_cancelled (op)) {
		ldap_abandon (ldap, msgid);
		return LDAP_USER_CANCELLED;
	} else
		return LDAP_SUCCESS;
}

static gint
gc_search (E2kGlobalCatalog *gc, E2kOperation *op,
	   const gchar *base, gint scope, const gchar *filter,
	   const gchar **attrs, LDAPMessage **msg)
{
	gint ldap_error, msgid, try;

	for (try = 0; try < 2; try++) {
		ldap_error = get_gc_connection (gc, op);
		if (ldap_error != LDAP_SUCCESS)
			return ldap_error;
		ldap_error = ldap_search_ext (gc->priv->ldap, base, scope,
					      filter, (gchar **)attrs,
					      FALSE, NULL, NULL, NULL, 0,
					      &msgid);
		if (ldap_error == LDAP_SERVER_DOWN)
			continue;
		else if (ldap_error != LDAP_SUCCESS)
			return ldap_error;

		ldap_error = gc_ldap_result (gc->priv->ldap, op, msgid, msg);
		if (ldap_error == LDAP_SERVER_DOWN)
			continue;
		else if (ldap_error != LDAP_SUCCESS)
			return ldap_error;

		return LDAP_SUCCESS;
	}

	return LDAP_SERVER_DOWN;
}

#ifdef HAVE_LDAP_NTLM_BIND
static gint
ntlm_bind (E2kGlobalCatalog *gc, E2kOperation *op, LDAP *ldap)
{
	LDAPMessage *msg;
	gint ldap_error, msgid, err;
	gchar *nonce, *default_domain;
	GByteArray *ba;
	struct berval ldap_buf;

	/* Create and send NTLM request */
	ba = xntlm_negotiate ();
	ldap_buf.bv_len = ba->len;
	ldap_buf.bv_val = (gchar *) ba->data;
	ldap_error = ldap_ntlm_bind (ldap, "NTLM", LDAP_AUTH_NTLM_REQUEST,
				     &ldap_buf, NULL, NULL, &msgid);
	g_byte_array_free (ba, TRUE);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Failure sending first NTLM bind message: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	/* Extract challenge */
	ldap_error = gc_ldap_result (ldap, op, msgid, &msg);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse first NTLM bind response\n"));
		return ldap_error;
	}
	ldap_error = ldap_parse_ntlm_bind_result (ldap, msg, &ldap_buf);
	ldap_msgfree (msg);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse NTLM bind response: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	if (!xntlm_parse_challenge (ldap_buf.bv_val, ldap_buf.bv_len,
				    &nonce, &default_domain,
				    &gc->domain)) {
		E2K_GC_DEBUG_MSG(("GC: Could not find nonce in NTLM bind response\n"));
		ber_memfree (ldap_buf.bv_val);

		return LDAP_DECODING_ERROR;
	}
	ber_memfree (ldap_buf.bv_val);

	/* Create and send response */
	ba = xntlm_authenticate (nonce, gc->priv->nt_domain ? gc->priv->nt_domain : default_domain,
				 gc->priv->user, gc->priv->password, NULL);
	ldap_buf.bv_len = ba->len;
	ldap_buf.bv_val = (gchar *) ba->data;
	ldap_error = ldap_ntlm_bind (ldap, "NTLM", LDAP_AUTH_NTLM_RESPONSE,
				     &ldap_buf, NULL, NULL, &msgid);
	g_byte_array_free (ba, TRUE);
	g_free (nonce);
	g_free (default_domain);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Failure sending second NTLM bind message: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	/* And get the final result */
	ldap_error = gc_ldap_result (ldap, op, msgid, &msg);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse second NTLM bind response\n"));
		return ldap_error;
	}
	ldap_error = ldap_parse_result (ldap, msg, &err, NULL, NULL,
					NULL, NULL, TRUE);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse second NTLM bind response: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	return err;
}
#endif

static gint
connect_ldap (E2kGlobalCatalog *gc, E2kOperation *op, LDAP *ldap)
{
	gint ldap_error;
	gchar *nt_name;
#ifdef G_OS_WIN32
	SEC_WINNT_AUTH_IDENTITY_W auth;
#endif

	/* authenticate */
#ifdef HAVE_LDAP_NTLM_BIND
	if ((gc->priv->auth == E2K_AUTOCONFIG_USE_GAL_DEFAULT || gc->priv->auth == E2K_AUTOCONFIG_USE_GAL_NTLM)) {
		ldap_error = ntlm_bind (gc, op, ldap);
		if (ldap_error == LDAP_SUCCESS) {
			E2K_GC_DEBUG_MSG(("GC: connected via NTLM\n\n"));
			return LDAP_SUCCESS;
		} else if (gc->priv->auth == E2K_AUTOCONFIG_USE_GAL_NTLM) {
			E2K_GC_DEBUG_MSG(("GC: user setup NTLM, but it failed 0x%02x\n", ldap_error));
			return ldap_error;
		}
	}
#endif

	if (gc->priv->auth == E2K_AUTOCONFIG_USE_GAL_NTLM) {
		E2K_GC_DEBUG_MSG(("GC: user setup NTLM, but we do not have it\n"));
		/* a little hack */
		return LDAP_AUTH_METHOD_NOT_SUPPORTED;
	}

	nt_name = gc->priv->nt_domain ?
		g_strdup_printf ("%s\\%s", gc->priv->nt_domain, gc->priv->user) :
		g_strdup (gc->priv->user);
#ifndef G_OS_WIN32
	ldap_error = ldap_simple_bind_s (ldap, nt_name, gc->priv->password);
#else
	auth.User = g_utf8_to_utf16 (gc->priv->user, -1, NULL, NULL, NULL);
	auth.UserLength = wcslen (auth.User);
	auth.Domain = gc->priv->nt_domain ?
		g_utf8_to_utf16 (gc->priv->nt_domain, -1, NULL, NULL, NULL) :
		g_utf8_to_utf16 ("", -1, NULL, NULL, NULL);
	auth.DomainLength = wcslen (auth.Domain);
	auth.Password = g_utf8_to_utf16 (gc->priv->password, -1, NULL, NULL, NULL);
	auth.PasswordLength = wcslen (auth.Password);
	auth.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;
	ldap_error = ldap_bind_s (ldap, nt_name, &auth, gc->priv->auth == E2K_AUTOCONFIG_USE_GAL_BASIC ? LDAP_AUTH_SIMPLE : LDAP_AUTH_NTLM);
	g_free (auth.Password);
	g_free (auth.Domain);
	g_free (auth.User);
#endif
	g_free (nt_name);

	if (ldap_error != LDAP_SUCCESS)
		g_warning ("LDAP authentication failed (0x%02x (%s))", ldap_error, ldap_err2string (ldap_error) ? ldap_err2string (ldap_error) : "Unknown error");
	else
		E2K_GC_DEBUG_MSG(("GC: connected\n\n"));

	return ldap_error;
}

static gint
get_ldap_connection (E2kGlobalCatalog *gc, E2kOperation *op,
		     const gchar *server, gint port,
		     LDAP **ldap)
{
	gint ldap_opt, ldap_error;

	E2K_GC_DEBUG_MSG(("\nGC: Connecting to ldap://%s:%d/\n", server, port));

	*ldap = ldap_init (server, port);
	if (!*ldap) {
		E2K_GC_DEBUG_MSG(("GC: failed\n\n"));
		g_warning ("Could not connect to ldap://%s:%d/",
			   server, port);
		return LDAP_SERVER_DOWN;
	}

	/* Set options */
	ldap_opt = LDAP_DEREF_ALWAYS;
	ldap_set_option (*ldap, LDAP_OPT_DEREF, &ldap_opt);
	ldap_opt = gc->response_limit;
	ldap_set_option (*ldap, LDAP_OPT_SIZELIMIT, &ldap_opt);
	ldap_opt = LDAP_VERSION3;
	ldap_set_option (*ldap, LDAP_OPT_PROTOCOL_VERSION, &ldap_opt);

	ldap_error = connect_ldap (gc, op, *ldap);
	if (ldap_error != LDAP_SUCCESS) {
		ldap_unbind (*ldap);
		*ldap = NULL;
	}
	return ldap_error;
}

static gint
get_gc_connection (E2kGlobalCatalog *gc, E2kOperation *op)
{
	gint err;

	if (gc->priv->ldap) {
		ldap_get_option (gc->priv->ldap, LDAP_OPT_ERROR_NUMBER, &err);
		if (err != LDAP_SERVER_DOWN)
			return LDAP_SUCCESS;

		return connect_ldap (gc, op, gc->priv->ldap);
	} else {
		return get_ldap_connection (gc, op,
					    gc->priv->server, 3268,
					    &gc->priv->ldap);
	}
}

/**
 * e2k_global_catalog_get_ldap:
 * @gc: the global catalog
 * @op: pointer to an initialized #E2kOperation to use for cancellation
 * @ldap_error: set the value returned from get_ldap_connection if not NULL
 *
 * Returns a new LDAP handle. The caller must ldap_unbind() it when it
 * is done.
 *
 * Return value: an LDAP handle, or %NULL if it can't connect
 **/
LDAP *
e2k_global_catalog_get_ldap (E2kGlobalCatalog *gc, E2kOperation *op, gint *ldap_error)
{
	LDAP *ldap;
	gint err;

	g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), NULL);

	err = get_ldap_connection (gc, op, gc->priv->server, 3268, &ldap);

	if (ldap_error)
		*ldap_error = err;

	return ldap;
}

/**
 * e2k_global_catalog_new:
 * @server: the GC server name
 * @response_limit: the maximum number of responses to return from a search
 * @user: username to authenticate with
 * @domain: NT domain of @user, or %NULL to autodetect.
 * @password: password to authenticate with
 *
 * Create an object for communicating with the Windows Global Catalog
 * via LDAP.
 *
 * Return value: the new E2kGlobalCatalog. (This call will always succeed.
 * If the passed-in data is bad, it will fail on a later call.)
 **/
E2kGlobalCatalog *
e2k_global_catalog_new (const gchar *server, gint response_limit,
			const gchar *user, const gchar *domain,
			const gchar *password, E2kAutoconfigGalAuthPref use_auth)
{
	E2kGlobalCatalog *gc;

	gc = g_object_new (E2K_TYPE_GLOBAL_CATALOG, NULL);
	gc->priv->server = g_strdup (server);
	gc->priv->auth = use_auth;
	gc->priv->user = g_strdup (user);
	gc->priv->nt_domain = g_strdup (domain);
	gc->priv->password = g_strdup (password);
	gc->response_limit = response_limit;

	return gc;
}

static const gchar *
lookup_mta (E2kGlobalCatalog *gc, E2kOperation *op, const gchar *mta_dn)
{
	gchar *hostname, **values;
	const gchar *attrs[2];
	LDAPMessage *resp;
	gint ldap_error, i;

	/* Skip over "CN=Microsoft MTA," */
	mta_dn = strchr (mta_dn, ',');
	if (!mta_dn)
		return NULL;
	mta_dn++;

	hostname = g_hash_table_lookup (gc->priv->server_cache, mta_dn);
	if (hostname)
		return hostname;

	E2K_GC_DEBUG_MSG(("GC:   Finding hostname for %s\n", mta_dn));

	attrs[0] = "networkAddress";
	attrs[1] = NULL;

	ldap_error = gc_search (gc, op, mta_dn, LDAP_SCOPE_BASE,
				NULL, attrs, &resp);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC:   lookup failed (0x%02x)\n", ldap_error));
		return NULL;
	}

	values = ldap_get_values (gc->priv->ldap, resp, "networkAddress");
	ldap_msgfree (resp);
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC:   entry has no networkAddress\n"));
		return NULL;
	}

	hostname = NULL;
	for (i = 0; values[i]; i++) {
		if (strstr (values[i], "_tcp")) {
			hostname = strchr (values[i], ':');
			break;
		}
	}
	if (!hostname) {
		E2K_GC_DEBUG_MSG(("GC:   host is not availble by TCP?\n"));
		ldap_value_free (values);
		return NULL;
	}

	hostname = g_strdup (hostname + 1);
	g_hash_table_insert (gc->priv->server_cache, g_strdup (mta_dn), hostname);
	ldap_value_free (values);

	E2K_GC_DEBUG_MSG(("GC:   %s\n", hostname));
	return hostname;
}

static void
get_sid_values (E2kGlobalCatalog *gc, E2kOperation *op,
		LDAPMessage *msg, E2kGlobalCatalogEntry *entry)
{
	gchar **values;
	struct berval **bsid_values;
	E2kSidType type;

	values = ldap_get_values (gc->priv->ldap, msg, "displayName");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: displayName %s\n", values[0]));
		entry->display_name = g_strdup (values[0]);
		ldap_value_free (values);
	}

	bsid_values = ldap_get_values_len (gc->priv->ldap, msg, "objectSid");
	if (!bsid_values)
		return;
	if (bsid_values[0]->bv_len < 2 ||
	    bsid_values[0]->bv_len != E2K_SID_BINARY_SID_LEN (bsid_values[0]->bv_val)) {
		E2K_GC_DEBUG_MSG(("GC: invalid SID\n"));
		return;
	}

	values = ldap_get_values (gc->priv->ldap, msg, "objectCategory");
	if (values && values[0] && !g_ascii_strncasecmp (values[0], "CN=Group", 8))
		type = E2K_SID_TYPE_GROUP;
	else if (values && values[0] && !g_ascii_strncasecmp (values[0], "CN=Foreign", 10))
		type = E2K_SID_TYPE_WELL_KNOWN_GROUP;
	else /* FIXME? */
		type = E2K_SID_TYPE_USER;
	if (values)
		ldap_value_free (values);

	entry->sid = e2k_sid_new_from_binary_sid (
		type, (guint8 *) bsid_values[0]->bv_val, entry->display_name);
	entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_SID;

	ldap_value_free_len (bsid_values);
}

static void
get_mail_values (E2kGlobalCatalog *gc, E2kOperation *op,
		 LDAPMessage *msg, E2kGlobalCatalogEntry *entry)
{
	gchar **values, **mtavalues;

	values = ldap_get_values (gc->priv->ldap, msg, "mail");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: mail %s\n", values[0]));
		entry->email = g_strdup (values[0]);
		g_hash_table_insert (gc->priv->entry_cache,
				     entry->email, entry);
		entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_EMAIL;
		ldap_value_free (values);
	}

	values = ldap_get_values (gc->priv->ldap, msg, "mailNickname");
	mtavalues = ldap_get_values (gc->priv->ldap, msg, "homeMTA");
	if (values && mtavalues) {
		E2K_GC_DEBUG_MSG(("GC: mailNickname %s\n", values[0]));
		E2K_GC_DEBUG_MSG(("GC: homeMTA %s\n", mtavalues[0]));
		entry->exchange_server = (gchar *)lookup_mta (gc, op, mtavalues[0]);
		ldap_value_free (mtavalues);
		if (entry->exchange_server)
			entry->mailbox = g_strdup (values[0]);
		ldap_value_free (values);
		entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX;
	}

	values = ldap_get_values (gc->priv->ldap, msg, "legacyExchangeDN");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: legacyExchangeDN %s\n", values[0]));
		entry->legacy_exchange_dn = g_strdup (values[0]);
		g_hash_table_insert (gc->priv->entry_cache,
				     entry->legacy_exchange_dn,
				     entry);
		entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN;
		ldap_value_free (values);
	}
}

static void
get_delegation_values (E2kGlobalCatalog *gc, E2kOperation *op,
		       LDAPMessage *msg, E2kGlobalCatalogEntry *entry)
{
	gchar **values;
	gint i;

	values = ldap_get_values (gc->priv->ldap, msg, "publicDelegates");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: publicDelegates\n"));
		entry->delegates = g_ptr_array_new ();
		for (i = 0; values[i]; i++) {
			E2K_GC_DEBUG_MSG(("GC:   %s\n", values[i]));
			g_ptr_array_add (entry->delegates,
					 g_strdup (values[i]));
		}
		entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES;
		ldap_value_free (values);
	}
	values = ldap_get_values (gc->priv->ldap, msg, "publicDelegatesBL");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: publicDelegatesBL\n"));
		entry->delegators = g_ptr_array_new ();
		for (i = 0; values[i]; i++) {
			E2K_GC_DEBUG_MSG(("GC:   %s\n", values[i]));
			g_ptr_array_add (entry->delegators,
					 g_strdup (values[i]));
		}
		entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS;
		ldap_value_free (values);
	}
}

static void
get_quota_values (E2kGlobalCatalog *gc, E2kOperation *op,
		  LDAPMessage *msg, E2kGlobalCatalogEntry *entry)
{
	gchar **quota_setting_values, **quota_limit_values;

	/* Check if mailbox store default values are used */
	quota_setting_values = ldap_get_values (gc->priv->ldap, msg, "mDBUseDefaults");
	if (!quota_setting_values) {
		entry->quota_warn = entry->quota_nosend = entry->quota_norecv = 0;
		return;
	}

	entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_QUOTA;
	E2K_GC_DEBUG_MSG(("GC: mDBUseDefaults %s\n", quota_setting_values[0]));

	if (!strcmp (quota_setting_values[0], "TRUE")) {
		/* use global mailbox store settings */
		E2K_GC_DEBUG_MSG(("GC: Using global mailbox store limits\n"));
	}
	ldap_value_free (quota_setting_values);

	quota_limit_values = ldap_get_values (gc->priv->ldap, msg, "mDBStorageQuota");
	if (quota_limit_values) {
		entry->quota_warn = atoi(quota_limit_values[0]);
		E2K_GC_DEBUG_MSG(("GC: mDBStorageQuota %s\n", quota_limit_values[0]));
		ldap_value_free (quota_limit_values);
	}

	quota_limit_values = ldap_get_values (gc->priv->ldap, msg, "mDBOverQuotaLimit");
	if (quota_limit_values) {
		entry->quota_nosend = atoi(quota_limit_values[0]);
		E2K_GC_DEBUG_MSG(("GC: mDBOverQuotaLimit %s\n", quota_limit_values[0]));
		ldap_value_free (quota_limit_values);
	}

	quota_limit_values = ldap_get_values (gc->priv->ldap, msg, "mDBOverHardQuotaLimit");
	if (quota_limit_values) {
		entry->quota_norecv = atoi(quota_limit_values[0]);
		E2K_GC_DEBUG_MSG(("GC: mDBHardQuotaLimit %s\n", quota_limit_values[0]));
		ldap_value_free (quota_limit_values);
	}
}

static void
get_account_control_values (E2kGlobalCatalog *gc, E2kOperation *op,
			    LDAPMessage *msg, E2kGlobalCatalogEntry *entry)
{
	gchar **values;

	values = ldap_get_values (gc->priv->ldap, msg, "userAccountControl");
	if (values) {
		entry->user_account_control = atoi(values[0]);
		E2K_GC_DEBUG_MSG(("GC: userAccountControl %s\n", values[0]));
		entry->mask |= E2K_GLOBAL_CATALOG_LOOKUP_ACCOUNT_CONTROL;
		ldap_value_free (values);
	}

}

/**
 * e2k_global_catalog_lookup:
 * @gc: the global catalog
 * @op: pointer to an #E2kOperation to use for cancellation
 * @type: the type of information in @key
 * @key: email address or DN to look up
 * @flags: the information to look up
 * @entry_p: pointer to a variable to return the entry in.
 *
 * Look up the indicated user in the global catalog and
 * return their information in *@entry_p.
 *
 * Return value: the status of the lookup
 **/
E2kGlobalCatalogStatus
e2k_global_catalog_lookup (E2kGlobalCatalog *gc,
			   E2kOperation *op,
			   E2kGlobalCatalogLookupType type,
			   const gchar *key,
			   E2kGlobalCatalogLookupFlags flags,
			   E2kGlobalCatalogEntry **entry_p)
{
	E2kGlobalCatalogEntry *entry;
	GPtrArray *attrs;
	E2kGlobalCatalogLookupFlags lookup_flags, need_flags = 0;
	const gchar *base = NULL;
	gchar *filter = NULL, *dn;
	gint scope = LDAP_SCOPE_BASE, ldap_error;
	E2kGlobalCatalogStatus status;
	LDAPMessage *msg, *resp;

	g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), E2K_GLOBAL_CATALOG_ERROR);
	g_return_val_if_fail (key != NULL, E2K_GLOBAL_CATALOG_ERROR);

	g_mutex_lock (gc->priv->ldap_lock);

	entry = g_hash_table_lookup (gc->priv->entry_cache, key);
	if (!entry)
		entry = g_new0 (E2kGlobalCatalogEntry, 1);

	attrs = g_ptr_array_new ();

	if (!entry->display_name)
		g_ptr_array_add (attrs, (guint8 *) "displayName");
	if (!entry->email) {
		g_ptr_array_add (attrs, (guint8 *) "mail");
		if (flags & E2K_GLOBAL_CATALOG_LOOKUP_EMAIL)
			need_flags |= E2K_GLOBAL_CATALOG_LOOKUP_EMAIL;
	}
	if (!entry->legacy_exchange_dn) {
		g_ptr_array_add (attrs, (guint8 *) "legacyExchangeDN");
		if (flags & E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN)
			need_flags |= E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN;
	}

	lookup_flags = flags & ~entry->mask;

	if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_SID) {
		g_ptr_array_add (attrs, (guint8 *) "objectSid");
		g_ptr_array_add (attrs, (guint8 *) "objectCategory");
		need_flags |= E2K_GLOBAL_CATALOG_LOOKUP_SID;
	}
	if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX) {
		g_ptr_array_add (attrs, (guint8 *) "mailNickname");
		g_ptr_array_add (attrs, (guint8 *) "homeMTA");
		need_flags |= E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX;
	}
	if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES)
		g_ptr_array_add (attrs, (guint8 *) "publicDelegates");
	if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS)
		g_ptr_array_add (attrs, (guint8 *) "publicDelegatesBL");
	if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_QUOTA) {
		g_ptr_array_add (attrs, (guint8 *) "mDBUseDefaults");
		g_ptr_array_add (attrs, (guint8 *) "mDBStorageQuota");
		g_ptr_array_add (attrs, (guint8 *) "mDBOverQuotaLimit");
		g_ptr_array_add (attrs, (guint8 *) "mDBOverHardQuotaLimit");
	}
	if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_ACCOUNT_CONTROL)
		g_ptr_array_add (attrs, (guint8 *) "userAccountControl");

	if (attrs->len == 0) {
		E2K_GC_DEBUG_MSG(("\nGC: returning cached info for %s\n", key));
		goto lookedup;
	}

	E2K_GC_DEBUG_MSG(("\nGC: looking up info for %s\n", key));
	g_ptr_array_add (attrs, NULL);

	switch (type) {
	case E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL:
		filter = g_strdup_printf ("(mail=%s)", key);
		base = LDAP_ROOT_DSE;
		scope = LDAP_SCOPE_SUBTREE;
		break;

	case E2K_GLOBAL_CATALOG_LOOKUP_BY_DN:
		filter = NULL;
		base = key;
		scope = LDAP_SCOPE_BASE;
		break;

	case E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN:
		filter = g_strdup_printf ("(legacyExchangeDN=%s)", key);
		base = LDAP_ROOT_DSE;
		scope = LDAP_SCOPE_SUBTREE;
		break;
	}

	ldap_error = gc_search (gc, op, base, scope, filter,
				(const gchar **)attrs->pdata, &msg);
	if (ldap_error == LDAP_USER_CANCELLED) {
		E2K_GC_DEBUG_MSG(("GC: ldap_search cancelled"));
		status = E2K_GLOBAL_CATALOG_CANCELLED;
		goto done;
	} else if (ldap_error == LDAP_INVALID_CREDENTIALS) {
		E2K_GC_DEBUG_MSG(("GC: ldap_search auth failed"));
		status = E2K_GLOBAL_CATALOG_AUTH_FAILED;
		goto done;
	} else if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: ldap_search failed: 0x%02x\n\n", ldap_error));
		status = E2K_GLOBAL_CATALOG_ERROR;
		goto done;
	}

	resp = ldap_first_entry (gc->priv->ldap, msg);
	if (!resp) {
		E2K_GC_DEBUG_MSG(("GC: no such user\n\n"));
		status = E2K_GLOBAL_CATALOG_NO_SUCH_USER;
		ldap_msgfree (msg);
		goto done;
	}

	if (!entry->dn) {
		dn = ldap_get_dn (gc->priv->ldap, resp);
		entry->dn = g_strdup (dn);
		E2K_GC_DEBUG_MSG(("GC: dn = %s\n\n", dn));
		ldap_memfree (dn);
		g_ptr_array_add (gc->priv->entries, entry);
		g_hash_table_insert (gc->priv->entry_cache,
				     entry->dn, entry);
	}

	get_sid_values (gc, op, resp, entry);
	get_mail_values (gc, op, resp, entry);
	get_delegation_values (gc, op, resp, entry);
	get_quota_values (gc, op, resp, entry);
	get_account_control_values (gc, op, resp, entry);
	ldap_msgfree (msg);

 lookedup:
	if (need_flags & ~entry->mask) {
		E2K_GC_DEBUG_MSG(("GC: no data\n\n"));
		status = E2K_GLOBAL_CATALOG_NO_DATA;
	} else {
		E2K_GC_DEBUG_MSG(("\n"));
		status = E2K_GLOBAL_CATALOG_OK;
		entry->mask |= lookup_flags;
		*entry_p = entry;
	}

 done:
	g_free (filter);
	g_ptr_array_free (attrs, TRUE);

	if (status != E2K_GLOBAL_CATALOG_OK && !entry->dn)
		g_free (entry);

	g_mutex_unlock (gc->priv->ldap_lock);
	return status;
}

struct async_lookup_data {
	E2kGlobalCatalog *gc;
	E2kOperation *op;
	E2kGlobalCatalogLookupType type;
	gchar *key;
	E2kGlobalCatalogLookupFlags flags;
	E2kGlobalCatalogCallback callback;
	gpointer user_data;

	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;
};

static gboolean
idle_lookup_result (gpointer user_data)
{
	struct async_lookup_data *ald = user_data;

	ald->callback (ald->gc, ald->status, ald->entry, ald->user_data);
	g_object_unref (ald->gc);
	g_free (ald->key);
	g_free (ald);
	return FALSE;
}

static gpointer
do_lookup_thread (gpointer user_data)
{
	struct async_lookup_data *ald = user_data;

	ald->status = e2k_global_catalog_lookup (ald->gc, ald->op, ald->type,
						 ald->key, ald->flags,
						 &ald->entry);
	g_idle_add (idle_lookup_result, ald);
	return NULL;
}

/**
 * e2k_global_catalog_async_lookup:
 * @gc: the global catalog
 * @op: pointer to an #E2kOperation to use for cancellation
 * @type: the type of information in @key
 * @key: email address or DN to look up
 * @flags: the information to look up
 * @callback: the callback to invoke after finding the user
 * @user_data: data to pass to callback
 *
 * Asynchronously look up the indicated user in the global catalog and
 * return the requested information to the callback.
 **/
void
e2k_global_catalog_async_lookup (E2kGlobalCatalog *gc,
				 E2kOperation *op,
				 E2kGlobalCatalogLookupType type,
				 const gchar *key,
				 E2kGlobalCatalogLookupFlags flags,
				 E2kGlobalCatalogCallback callback,
				 gpointer user_data)
{
	struct async_lookup_data *ald;
	pthread_t pth;

	ald = g_new0 (struct async_lookup_data, 1);
	ald->gc = g_object_ref (gc);
	ald->op = op;
	ald->type = type;
	ald->key = g_strdup (key);
	ald->flags = flags;
	ald->callback = callback;
	ald->user_data = user_data;

	if (pthread_create (&pth, NULL, do_lookup_thread, ald) == -1) {
		g_warning ("Could not create lookup thread\n");
		ald->status = E2K_GLOBAL_CATALOG_ERROR;
		g_idle_add (idle_lookup_result, ald);
	}
}

static const gchar *
lookup_controlling_ad_server (E2kGlobalCatalog *gc, E2kOperation *op,
			      const gchar *dn)
{
	gchar *hostname, **values, *ad_dn;
	const gchar *attrs[2];
	LDAPMessage *resp;
	gint ldap_error;

	while (g_ascii_strncasecmp (dn, "DC=", 3) != 0) {
		dn = strchr (dn, ',');
		if (!dn)
			return NULL;
		dn++;
	}

	hostname = g_hash_table_lookup (gc->priv->server_cache, dn);
	if (hostname)
		return hostname;

	E2K_GC_DEBUG_MSG(("GC:   Finding AD server for %s\n", dn));

	attrs[0] = "masteredBy";
	attrs[1] = NULL;

	ldap_error = gc_search (gc, op, dn, LDAP_SCOPE_BASE, NULL, attrs, &resp);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC:   ldap_search failed: 0x%02x\n", ldap_error));
		return NULL;
	}

	values = ldap_get_values (gc->priv->ldap, resp, "masteredBy");
	ldap_msgfree (resp);
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC:   no known AD server\n\n"));
		return NULL;
	}

	/* Skip over "CN=NTDS Settings," */
	ad_dn = strchr (values[0], ',');
	if (!ad_dn) {
		E2K_GC_DEBUG_MSG(("GC:   bad dn %s\n\n", values[0]));
		ldap_value_free (values);
		return NULL;
	}
	ad_dn++;

	attrs[0] = "dNSHostName";
	attrs[1] = NULL;

	ldap_error = gc_search (gc, op, ad_dn, LDAP_SCOPE_BASE, NULL, attrs, &resp);
	ldap_value_free (values);

	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC:   ldap_search failed: 0x%02x\n\n", ldap_error));
		return NULL;
	}

	values = ldap_get_values (gc->priv->ldap, resp, "dNSHostName");
	ldap_msgfree (resp);
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC:   entry has no dNSHostName\n\n"));
		return NULL;
	}

	hostname = g_strdup (values[0]);
	ldap_value_free (values);

	g_hash_table_insert (gc->priv->server_cache, g_strdup (dn), hostname);

	E2K_GC_DEBUG_MSG(("GC:   %s\n", hostname));
	return hostname;
}

static gchar *
find_domain_dn (gchar *domain)
{
	GString *dn_value = g_string_new (NULL);
	gchar *dn;
	gchar  *sub_domain=NULL;

	sub_domain = strtok (domain, ".");
	while (sub_domain != NULL) {
		g_string_append (dn_value, "DC=");
		g_string_append (dn_value, sub_domain);
		g_string_append (dn_value, ",");
		sub_domain = strtok (NULL, ".");
	}
	if (dn_value->str[0])
		dn = g_strndup (dn_value->str, strlen(dn_value->str) - 1);
	else
		dn = NULL;
	g_string_free (dn_value, TRUE);
	return dn;
}

gdouble
lookup_passwd_max_age (E2kGlobalCatalog *gc, E2kOperation *op)
{
	gchar **values = NULL, *filter = NULL, *val=NULL;
	const gchar *attrs[2];
	LDAP *ldap;
	LDAPMessage *msg=NULL;
	gint ldap_error, msgid;
	gdouble maxAge=0;
	gchar *dn=NULL;

	attrs[0] = "maxPwdAge";
	attrs[1] = NULL;

	filter = g_strdup("objectClass=domainDNS");

	dn = find_domain_dn (gc->domain);

	ldap_error = get_ldap_connection (gc, op, gc->priv->server, LDAP_PORT, &ldap);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Establishing ldap connection failed : 0x%02x\n\n",
									ldap_error));
		return -1;
	}

	ldap_error = ldap_search_ext (ldap, dn, LDAP_SCOPE_BASE, filter, (gchar **)attrs,
				      FALSE, NULL, NULL, NULL, 0, &msgid);
	if (!ldap_error) {
		ldap_error = gc_ldap_result (ldap, op, msgid, &msg);
		if (ldap_error) {
			E2K_GC_DEBUG_MSG(("GC: ldap_result failed: 0x%02x\n\n", ldap_error));
			return -1;
		}
	}
	else {
		E2K_GC_DEBUG_MSG(("GC: ldap_search failed:0x%02x \n\n", ldap_error));
		return -1;
	}

	values = ldap_get_values (ldap, msg, "maxPwdAge");
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC: couldn't retrieve maxPwdAge\n"));
		return -1;
	}

	if (values[0]) {
		val = values[0];
		if (*val == '-')
			++val;
		maxAge = strtod (val, NULL);
	}

	/*g_hash_table_insert (gc->priv->server_cache, g_strdup (dn), hostname); FIXME? */

	E2K_GC_DEBUG_MSG(("GC:   maxPwdAge = %f\n", maxAge));

	if (msg)
		ldap_msgfree (msg);
	if (values)
		ldap_value_free (values);
	ldap_unbind (ldap);
	g_free (filter);
	g_free (dn);
	return maxAge;
}

static E2kGlobalCatalogStatus
do_delegate_op (E2kGlobalCatalog *gc, E2kOperation *op, gint deleg_op,
		const gchar *self_dn, const gchar *delegate_dn)
{
	LDAP *ldap;
	LDAPMod *mods[2], mod;
	const gchar *ad_server;
	gchar *values[2];
	gint ldap_error, msgid;

	g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), E2K_GLOBAL_CATALOG_ERROR);
	g_return_val_if_fail (self_dn != NULL, E2K_GLOBAL_CATALOG_ERROR);
	g_return_val_if_fail (delegate_dn != NULL, E2K_GLOBAL_CATALOG_ERROR);

	ad_server = lookup_controlling_ad_server (gc, op, self_dn);
	if (!ad_server) {
		if (e2k_operation_is_cancelled (op))
			return E2K_GLOBAL_CATALOG_CANCELLED;
		else
			return E2K_GLOBAL_CATALOG_ERROR;
	}

	ldap_error = get_ldap_connection (gc, op, ad_server, LDAP_PORT, &ldap);
	if (ldap_error == LDAP_USER_CANCELLED)
		return E2K_GLOBAL_CATALOG_CANCELLED;
	else if (ldap_error != LDAP_SUCCESS)
		return E2K_GLOBAL_CATALOG_ERROR;

	mod.mod_op = deleg_op;
	mod.mod_type = (gchar *) "publicDelegates";
	mod.mod_values = values;
	values[0] = (gchar *)delegate_dn;
	values[1] = NULL;

	mods[0] = &mod;
	mods[1] = NULL;

	ldap_error = ldap_modify_ext (ldap, self_dn, mods, NULL, NULL, &msgid);
	if (ldap_error == LDAP_SUCCESS) {
		LDAPMessage *msg;

		ldap_error = gc_ldap_result (ldap, op, msgid, &msg);
		if (ldap_error == LDAP_SUCCESS) {
			ldap_parse_result (ldap, msg, &ldap_error, NULL, NULL,
					   NULL, NULL, TRUE);
		}
	}
	ldap_unbind (ldap);

	switch (ldap_error) {
	case LDAP_SUCCESS:
		E2K_GC_DEBUG_MSG(("\n"));
		return E2K_GLOBAL_CATALOG_OK;

	case LDAP_NO_SUCH_OBJECT:
		E2K_GC_DEBUG_MSG(("GC: no such user\n\n"));
		return E2K_GLOBAL_CATALOG_NO_SUCH_USER;

	case LDAP_NO_SUCH_ATTRIBUTE:
		E2K_GC_DEBUG_MSG(("GC: no such delegate\n\n"));
		return E2K_GLOBAL_CATALOG_NO_DATA;

	case LDAP_CONSTRAINT_VIOLATION:
		E2K_GC_DEBUG_MSG(("GC: bad delegate\n\n"));
		return E2K_GLOBAL_CATALOG_BAD_DATA;

	case LDAP_TYPE_OR_VALUE_EXISTS:
		E2K_GC_DEBUG_MSG(("GC: delegate already exists\n\n"));
		return E2K_GLOBAL_CATALOG_EXISTS;

	case LDAP_USER_CANCELLED:
		E2K_GC_DEBUG_MSG(("GC: cancelled\n\n"));
		return E2K_GLOBAL_CATALOG_CANCELLED;

	default:
		E2K_GC_DEBUG_MSG(("GC: ldap_modify failed: 0x%02x\n\n", ldap_error));
		return E2K_GLOBAL_CATALOG_ERROR;
	}
}

/**
 * e2k_global_catalog_add_delegate:
 * @gc: the global catalog
 * @op: pointer to an #E2kOperation to use for cancellation
 * @self_dn: Active Directory DN of the user to add a delegate to
 * @delegate_dn: Active Directory DN of the new delegate
 *
 * Attempts to make @delegate_dn a delegate of @self_dn.
 *
 * Return value: %E2K_GLOBAL_CATALOG_OK on success,
 * %E2K_GLOBAL_CATALOG_NO_SUCH_USER if @self_dn is invalid,
 * %E2K_GLOBAL_CATALOG_BAD_DATA if @delegate_dn is invalid,
 * %E2K_GLOBAL_CATALOG_EXISTS if @delegate_dn is already a delegate,
 * %E2K_GLOBAL_CATALOG_ERROR on other errors.
 **/
E2kGlobalCatalogStatus
e2k_global_catalog_add_delegate (E2kGlobalCatalog *gc,
				 E2kOperation *op,
				 const gchar *self_dn,
				 const gchar *delegate_dn)
{
	E2K_GC_DEBUG_MSG(("\nGC: adding %s as delegate for %s\n", delegate_dn, self_dn));

	return do_delegate_op (gc, op, LDAP_MOD_ADD, self_dn, delegate_dn);
}

/**
 * e2k_global_catalog_remove_delegate:
 * @gc: the global catalog
 * @op: pointer to an #E2kOperation to use for cancellation
 * @self_dn: Active Directory DN of the user to remove a delegate from
 * @delegate_dn: Active Directory DN of the delegate to remove
 *
 * Attempts to remove @delegate_dn as a delegate of @self_dn.
 *
 * Return value: %E2K_GLOBAL_CATALOG_OK on success,
 * %E2K_GLOBAL_CATALOG_NO_SUCH_USER if @self_dn is invalid,
 * %E2K_GLOBAL_CATALOG_NO_DATA if @delegate_dn is not a delegate of @self_dn,
 * %E2K_GLOBAL_CATALOG_ERROR on other errors.
 **/
E2kGlobalCatalogStatus
e2k_global_catalog_remove_delegate (E2kGlobalCatalog *gc,
				    E2kOperation *op,
				    const gchar *self_dn,
				    const gchar *delegate_dn)
{
	E2K_GC_DEBUG_MSG(("\nGC: removing %s as delegate for %s\n", delegate_dn, self_dn));

	return do_delegate_op (gc, op, LDAP_MOD_DELETE, self_dn, delegate_dn);
}
