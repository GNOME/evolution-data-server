/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * camel-provider.c: provider framework
 *
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *  Dan Winship <danw@ximian.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

/* FIXME: Shouldn't we add a version number to providers ? */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gmodule.h>

#include "camel-provider.h"
#include "camel-string-utils.h"
#include "camel-vee-store.h"
#include "camel-win32.h"

/* table of CamelProviderModule's */
static GHashTable *module_table;
/* table of CamelProvider's */
static GHashTable *provider_table;
static GStaticRecMutex provider_lock = G_STATIC_REC_MUTEX_INIT;

#define LOCK()		(g_static_rec_mutex_lock(&provider_lock))
#define UNLOCK()	(g_static_rec_mutex_unlock(&provider_lock))

/* The vfolder provider is always available */
static CamelProvider vee_provider = {
	"vfolder",
	N_("Virtual folder email provider"),

	N_("For reading mail as a query of another set of folders"),

	"vfolder",

	CAMEL_PROVIDER_IS_STORAGE,
	CAMEL_URL_NEED_PATH | CAMEL_URL_PATH_IS_ABSOLUTE | CAMEL_URL_FRAGMENT_IS_PATH,

	/* ... */
};

static GOnce setup_once = G_ONCE_INIT;

static gpointer
provider_setup (gpointer param)
{
	module_table = g_hash_table_new(camel_strcase_hash, camel_strcase_equal);
	provider_table = g_hash_table_new(camel_strcase_hash, camel_strcase_equal);

	vee_provider.object_types[CAMEL_PROVIDER_STORE] = camel_vee_store_get_type ();
	vee_provider.url_hash = camel_url_hash;
	vee_provider.url_equal = camel_url_equal;
	camel_provider_register(&vee_provider);

	return NULL;
}

/**
 * camel_provider_init:
 *
 * Initialize the Camel provider system by reading in the .urls
 * files in the provider directory and creating a hash table mapping
 * URLs to module names.
 *
 * A .urls file has the same initial prefix as the shared library it
 * correspond to, and consists of a series of lines containing the URL
 * protocols that that library handles.
 *
 * TODO: This should be pathed?
 * TODO: This should be plugin-d?
 **/
void
camel_provider_init (void)
{
	GDir *dir;
	const gchar *entry;
	gchar *p, *name, buf[80];
	CamelProviderModule *m;
	static gint loaded = 0;

	g_once (&setup_once, provider_setup, NULL);

	if (loaded)
		return;

	loaded = 1;

	dir = g_dir_open (CAMEL_PROVIDERDIR, 0, NULL);
	if (!dir) {
		g_warning("Could not open camel provider directory (%s): %s",
			  CAMEL_PROVIDERDIR, g_strerror (errno));
		return;
	}

	while ((entry = g_dir_read_name (dir))) {
		FILE *fp;

		p = strrchr (entry, '.');
		if (!p || strcmp (p, ".urls") != 0)
			continue;

		name = g_strdup_printf ("%s/%s", CAMEL_PROVIDERDIR, entry);
		fp = g_fopen (name, "r");
		if (!fp) {
			g_warning ("Could not read provider info file %s: %s",
				   name, g_strerror (errno));
			g_free (name);
			continue;
		}

		p = strrchr (name, '.');
		strcpy (p, "." G_MODULE_SUFFIX);

		m = g_malloc0(sizeof(*m));
		m->path = name;

		while ((fgets (buf, sizeof (buf), fp))) {
			buf[sizeof (buf) - 1] = '\0';
			p = strchr (buf, '\n');
			if (p)
				*p = '\0';

			if (*buf) {
				gchar *protocol = g_strdup(buf);

				m->types = g_slist_prepend(m->types, protocol);
				g_hash_table_insert(module_table, protocol, m);
			}
		}

		fclose (fp);
	}

	g_dir_close (dir);
}

/**
 * camel_provider_load:
 * @path: the path to a shared library
 * @error: return location for a #GError, or %NULL
 *
 * Loads the provider at @path, and calls its initialization function,
 * passing @session as an argument. The provider should then register
 * itself with @session.
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_provider_load (const gchar *path,
                     GError **error)
{
	GModule *module;
	CamelProvider *(*provider_module_init) (void);

	g_once (&setup_once, provider_setup, NULL);

	if (!g_module_supported ()) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load %s: Module loading "
			  "not supported on this system."), path);
		return FALSE;
	}

	module = g_module_open (path, G_MODULE_BIND_LAZY);
	if (module == NULL) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load %s: %s"),
			path, g_module_error ());
		return FALSE;
	}

	if (!g_module_symbol (module, "camel_provider_module_init",
			      (gpointer *)&provider_module_init)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not load %s: No initialization "
			  "code in module."), path);
		g_module_close (module);
		return FALSE;
	}

	provider_module_init ();

	return TRUE;
}

/**
 * camel_provider_register:
 * @provider: provider object
 *
 * Registers a provider.
 **/
void
camel_provider_register(CamelProvider *provider)
{
	gint i;
	CamelProviderConfEntry *conf;
	GList *l;

	g_return_if_fail (provider != NULL);

	g_assert(provider_table);

	LOCK();

	if (g_hash_table_lookup(provider_table, provider->protocol) != NULL) {
		g_warning("Trying to re-register camel provider for protocol '%s'", provider->protocol);
		UNLOCK();
		return;
	}

	for (i = 0; i < CAMEL_NUM_PROVIDER_TYPES; i++) {
		if (provider->object_types[i])
			provider->service_cache[i] = camel_object_bag_new (provider->url_hash, provider->url_equal,
									   (CamelCopyFunc)camel_url_copy, (GFreeFunc)camel_url_free);
	}

	/* Translate all strings here */
#define P_(string) dgettext (provider->translation_domain, string)

	provider->name = P_(provider->name);
	provider->description = P_(provider->description);
	conf = provider->extra_conf;
	if (conf) {
		for (i=0;conf[i].type != CAMEL_PROVIDER_CONF_END;i++) {
			if (conf[i].text)
				conf[i].text = P_(conf[i].text);
		}
	}

	l = provider->authtypes;
	while (l) {
		CamelServiceAuthType *auth = l->data;

		auth->name = P_(auth->name);
		auth->description = P_(auth->description);
		l = l->next;
	}

	g_hash_table_insert (
		provider_table,
		(gpointer) provider->protocol, provider);

	UNLOCK();
}

static gint
provider_compare (gconstpointer a, gconstpointer b)
{
	const CamelProvider *cpa = (const CamelProvider *)a;
	const CamelProvider *cpb = (const CamelProvider *)b;

	return strcmp (cpa->name, cpb->name);
}

static void
add_to_list (gpointer key, gpointer value, gpointer user_data)
{
	GList **list = user_data;

	*list = g_list_prepend(*list, value);
}

/**
 * camel_session_list_providers:
 * @session: the session
 * @load: whether or not to load in providers that are not already loaded
 *
 * This returns a list of available providers in this session. If @load
 * is %TRUE, it will first load in all available providers that haven't
 * yet been loaded.
 *
 * Returns: a GList of providers, which the caller must free.
 **/
GList *
camel_provider_list(gboolean load)
{
	GList *list = NULL;

	/* provider_table can be NULL, so initialize it */
	if (G_UNLIKELY (provider_table == NULL))
		camel_provider_init ();

	g_return_val_if_fail (provider_table != NULL, NULL);

	LOCK();

	if (load) {
		GList *w;

		g_hash_table_foreach(module_table, add_to_list, &list);
		for (w = list;w;w = w->next) {
			CamelProviderModule *m = w->data;

			if (!m->loaded) {
				camel_provider_load(m->path, NULL);
				m->loaded = 1;
			}
		}
		g_list_free(list);
		list = NULL;
	}

	g_hash_table_foreach(provider_table, add_to_list, &list);

	UNLOCK();

	list = g_list_sort(list, provider_compare);

	return list;
}

/**
 * camel_provider_get:
 * @url_string: the URL for the service whose provider you want
 * @error: return location for a #GError, or %NULL
 *
 * This returns the CamelProvider that would be used to handle
 * @url_string, loading it in from disk if necessary.
 *
 * Returns: the provider, or %NULL, in which case @error will be set.
 **/
CamelProvider *
camel_provider_get (const gchar *url_string,
                    GError **error)
{
	CamelProvider *provider = NULL;
	gchar *protocol;
	gsize len;

	g_return_val_if_fail (url_string != NULL, NULL);
	g_return_val_if_fail (provider_table != NULL, NULL);

	len = strcspn(url_string, ":");
	protocol = g_alloca(len+1);
	memcpy(protocol, url_string, len);
	protocol[len] = 0;

	LOCK();

	provider = g_hash_table_lookup(provider_table, protocol);
	if (!provider) {
		CamelProviderModule *m;

		m = g_hash_table_lookup(module_table, protocol);
		if (m && !m->loaded) {
			m->loaded = 1;
			if (!camel_provider_load (m->path, error))
				goto fail;
		}
		provider = g_hash_table_lookup(provider_table, protocol);
	}

	if (provider == NULL)
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("No provider available for protocol '%s'"),
			protocol);
fail:
	UNLOCK();

	return provider;
}

/**
 * camel_provider_auto_detect:
 * @provider: camel provider
 * @url: a #CamelURL
 * @auto_detected: output hash table of auto-detected values
 * @error: return location for a #GError, or %NULL
 *
 * After filling in the standard Username/Hostname/Port/Path settings
 * (which must be set in @url), if the provider supports it, you
 * may wish to have the provider auto-detect further settings based on
 * the aformentioned settings.
 *
 * If the provider does not support auto-detection, @auto_detected
 * will be set to %NULL. Otherwise the provider will attempt to
 * auto-detect whatever it can and file them into @auto_detected. If
 * for some reason it cannot auto-detect anything (not enough
 * information provided in @url?) then @auto_detected will be
 * set to %NULL and an exception may be set to explain why it failed.
 *
 * Returns: 0 on success or -1 on fail.
 **/
gint
camel_provider_auto_detect (CamelProvider *provider,
                            CamelURL *url,
                            GHashTable **auto_detected,
                            GError **error)
{
	g_return_val_if_fail (provider != NULL, -1);

	if (provider->auto_detect) {
		return provider->auto_detect (url, auto_detected, error);
	} else {
		*auto_detected = NULL;
		return 0;
	}
}
