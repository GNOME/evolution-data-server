/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 *          Bertrand Guiheneuf <bertrand@helixcode.com>
 */

#include "evolution-data-server-config.h"

#include <signal.h>

#include <nspr.h>
#include <prthread.h>
#include "nss.h"      /* Don't use <> here or it will include the system nss.h instead */
#include <ssl.h>
#include <sslproto.h>
#include <errno.h>

#include <glib/gi18n-lib.h>

#include "camel.h"
#include "camel-certdb.h"
#include "camel-debug.h"
#include "camel-provider.h"
#include "camel-win32.h"

/* private functions from camel-utils.c */
void _camel_utils_initialize (void);
void _camel_utils_shutdown (void);

/* To protect NSS initialization and shutdown. This prevents
 * concurrent calls to shutdown () and init () by different threads */
PRLock *nss_initlock = NULL;

/* Whether or not Camel has initialized the NSS library. We cannot
 * unconditionally call NSS_Shutdown () if NSS was initialized by other
 * library before. This boolean ensures that we only perform a cleanup
 * if and only if Camel is the one that previously initialized NSS */
volatile gboolean nss_initialized = FALSE;

static gint initialised = FALSE;

gint camel_application_is_exiting = FALSE;

#define NSS_SYSTEM_DB "/etc/pki/nssdb"

static gint
nss_has_system_db (void)
{
	gint found = FALSE;
#ifndef G_OS_WIN32
	FILE *f;
	gchar buf[80];

	f = fopen (NSS_SYSTEM_DB "/pkcs11.txt", "r");
	if (!f)
		return FALSE;

	/* Check whether the system NSS db is actually enabled */
	while (fgets (buf, 80, f) && !found) {
		if (!strcmp (buf, "library=libnsssysinit.so\n"))
			found = TRUE;
	}
	fclose (f);
#endif
	return found;
}

gint
camel_init (const gchar *configdir,
            gboolean nss_init)
{
	CamelCertDB *certdb;
	gchar *path;

	if (initialised)
		return 0;

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	camel_debug_init ();

	if (nss_init) {
		static gchar v2_enabled = -1, weak_ciphers = -1;
		gchar *nss_configdir = NULL;
		gchar *nss_sql_configdir = NULL;
		SECStatus status = SECFailure;

#if NSS_VMAJOR < 3 || (NSS_VMAJOR == 3 && NSS_VMINOR < 14)
		/* NSS pre-3.14 has most of the ciphers disabled, thus enable
		 * weak ciphers, if it's compiled against such */
		weak_ciphers = 1;
#endif

		/* check camel-tcp-stream-ssl.c for the same "CAMEL_SSL_V2_ENABLE" */
		if (v2_enabled == -1)
			v2_enabled = g_strcmp0 (g_getenv ("CAMEL_SSL_V2_ENABLE"), "1") == 0 ? 1 : 0;
		if (weak_ciphers == -1)
			weak_ciphers = g_strcmp0 (g_getenv ("CAMEL_SSL_WEAK_CIPHERS"), "1") == 0 ? 1 : 0;

		if (nss_initlock == NULL) {
			PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 10);
			nss_initlock = PR_NewLock ();
		}
		PR_Lock (nss_initlock);

		if (NSS_IsInitialized ())
			goto skip_nss_init;

#ifndef G_OS_WIN32
		nss_configdir = g_strdup (configdir);
#else
		nss_configdir = g_win32_locale_filename_from_utf8 (configdir);
#endif

		if (nss_has_system_db ()) {
			nss_sql_configdir = g_strdup ("sql:" NSS_SYSTEM_DB );
		} else {
			/* On Windows, we use the Evolution configdir. On other
			 * operating systems we use ~/.pki/nssdb/, which is where
			 * the user-specific part of the "shared system db" is
			 * stored and is what Chrome uses too.
			 *
			 * We have to create the configdir if it does not exist,
			 * to prevent camel from bailing out on first run. */
#ifdef G_OS_WIN32
			g_mkdir_with_parents (configdir, 0700);
			nss_sql_configdir = g_strconcat (
				"sql:", nss_configdir, NULL);
#else
			gchar *user_nss_dir;

			/* Flatpak should allow access to the host machine's ~/.pki */
			user_nss_dir = g_build_filename (g_get_home_dir (), ".pki", "nssdb", NULL);

			if (g_mkdir_with_parents (user_nss_dir, 0700))
				g_warning (
					"Failed to create SQL "
					"database directory %s: %s\n",
					user_nss_dir, strerror (errno));

			nss_sql_configdir = g_strconcat (
				"sql:", user_nss_dir, NULL);
			g_free (user_nss_dir);
#endif
		}

#if NSS_VMAJOR > 3 || (NSS_VMAJOR == 3 && NSS_VMINOR >= 12)
		/* See: https://wiki.mozilla.org/NSS_Shared_DB,
		 * particularly "Mode 3A".  Note that the target
		 * directory MUST EXIST. */
		status = NSS_InitWithMerge (
			nss_sql_configdir,	/* dest dir */
			"", "",			/* new DB name prefixes */
			SECMOD_DB,		/* secmod name */
			nss_configdir,		/* old DB dir */
			"", "",			/* old DB name prefixes */
			nss_configdir,		/* unique ID for old DB */
			"Evolution S/MIME",	/* UI name for old DB */
			0);			/* flags */

		if (status == SECFailure) {
			g_warning (
				"Failed to initialize NSS SQL database in %s: NSS error %d",
				nss_sql_configdir, PORT_GetError ());
			/* Fall back to opening the old DBM database */
		}
#endif
		/* Support old versions of libnss, pre-sqlite support. */
		if (status == SECFailure)
			status = NSS_InitReadWrite (nss_configdir);
		if (status == SECFailure) {
			/* Fall back to using volatile dbs? */
			status = NSS_NoDB_Init (nss_configdir);
			if (status == SECFailure) {
				g_free (nss_configdir);
				g_free (nss_sql_configdir);
				g_warning ("Failed to initialize NSS");
				PR_Unlock (nss_initlock);
				return -1;
			}
		}

		nss_initialized = TRUE;
skip_nss_init:

		NSS_SetDomesticPolicy ();

		if (weak_ciphers) {
			PRUint16 indx;

			/* enable SSL3/TLS cipher-suites */
			for (indx = 0; indx < SSL_NumImplementedCiphers; indx++) {
				if (!SSL_IS_SSL2_CIPHER (SSL_ImplementedCiphers[indx]) &&
				    SSL_ImplementedCiphers[indx] != SSL_RSA_WITH_NULL_SHA &&
				    SSL_ImplementedCiphers[indx] != SSL_RSA_WITH_NULL_MD5)
					SSL_CipherPrefSetDefault (SSL_ImplementedCiphers[indx], PR_TRUE);
			}
		}

		SSL_OptionSetDefault (SSL_ENABLE_SSL2, v2_enabled ? PR_TRUE : PR_FALSE);
		SSL_OptionSetDefault (SSL_V2_COMPATIBLE_HELLO, PR_FALSE);
		SSL_OptionSetDefault (SSL_ENABLE_SSL3, PR_TRUE);
		SSL_OptionSetDefault (SSL_ENABLE_TLS, PR_TRUE);

		PR_Unlock (nss_initlock);

		g_free (nss_configdir);
		g_free (nss_sql_configdir);
	}

	path = g_strdup_printf ("%s/camel-cert.db", configdir);
	certdb = camel_certdb_new ();
	camel_certdb_set_filename (certdb, path);
	g_free (path);

	/* if we fail to load, who cares? it'll just be a volatile certdb */
	camel_certdb_load (certdb);

	/* set this certdb as the default db */
	camel_certdb_set_default (certdb);

	g_object_unref (certdb);

	_camel_utils_initialize ();

	initialised = TRUE;

	return 0;
}

/**
 * camel_shutdown:
 *
 * Since: 2.24
 **/
void
camel_shutdown (void)
{
	CamelCertDB *certdb;

	if (!initialised)
		return;

	certdb = camel_certdb_get_default ();
	if (certdb) {
		camel_certdb_save (certdb);
		camel_certdb_set_default (NULL);
	}

	/* These next calls must come last. */

	if (nss_initlock != NULL) {
		PR_Lock (nss_initlock);
		if (nss_initialized)
			NSS_Shutdown ();
		PR_Unlock (nss_initlock);
	}

	_camel_utils_shutdown ();

	initialised = FALSE;
}

static GRecMutex camel_binding_lock;

/**
 * camel_binding_bind_property:
 * @source: (type GObject.Object): the source #GObject
 * @source_property: the property on @source to bind
 * @target: (type GObject.Object): the target #GObject
 * @target_property: the property on @target to bind
 * @flags: flags to pass to #GBinding
 *
 * Thread safe variant of g_object_bind_property(). See its documentation
 * for more information on arguments and return value.
 *
 * Returns: (transfer none):
 *
 * Since: 3.16
 **/
GBinding *
camel_binding_bind_property (gpointer source,
			     const gchar *source_property,
			     gpointer target,
			     const gchar *target_property,
			     GBindingFlags flags)
{
	GBinding *binding;

	g_rec_mutex_lock (&camel_binding_lock);

	binding = g_object_bind_property (source, source_property, target, target_property, flags);

	g_rec_mutex_unlock (&camel_binding_lock);

	return binding;
}

/**
 * camel_binding_bind_property_full:
 * @source: (type GObject.Object): the source #GObject
 * @source_property: the property on @source to bind
 * @target: (type GObject.Object): the target #GObject
 * @target_property: the property on @target to bind
 * @flags: flags to pass to #GBinding
 * @transform_to: (scope notified) (nullable): the transformation function
 *   from the @source to the @target, or %NULL to use the default
 * @transform_from: (scope notified) (nullable): the transformation function
 *   from the @target to the @source, or %NULL to use the default
 * @user_data: custom data to be passed to the transformation functions,
 *   or %NULL
 * @notify: function to be called when disposing the binding, to free the
 *   resources used by the transformation functions
 *
 * Thread safe variant of g_object_bind_property_full(). See its documentation
 * for more information on arguments and return value.
 *
 * Return value: (transfer none): the #GBinding instance representing the
 *   binding between the two #GObject instances. The binding is released
 *   whenever the #GBinding reference count reaches zero.
 *
 * Since: 3.16
 **/
GBinding *
camel_binding_bind_property_full (gpointer source,
				  const gchar *source_property,
				  gpointer target,
				  const gchar *target_property,
				  GBindingFlags flags,
				  GBindingTransformFunc transform_to,
				  GBindingTransformFunc transform_from,
				  gpointer user_data,
				  GDestroyNotify notify)
{
	GBinding *binding;

	g_rec_mutex_lock (&camel_binding_lock);

	binding = g_object_bind_property_full (source, source_property, target, target_property, flags,
		transform_to, transform_from, user_data, notify);

	g_rec_mutex_unlock (&camel_binding_lock);

	return binding;
}

/**
 * camel_binding_bind_property_with_closures: (rename-to camel_binding_bind_property_full)
 * @source: (type GObject.Object): the source #GObject
 * @source_property: the property on @source to bind
 * @target: (type GObject.Object): the target #GObject
 * @target_property: the property on @target to bind
 * @flags: flags to pass to #GBinding
 * @transform_to: a #GClosure wrapping the transformation function
 *   from the @source to the @target, or %NULL to use the default
 * @transform_from: a #GClosure wrapping the transformation function
 *   from the @target to the @source, or %NULL to use the default
 *
 * Thread safe variant of g_object_bind_property_with_closures(). See its
 * documentation for more information on arguments and return value.
 *
 * Return value: (transfer none): the #GBinding instance representing the
 *   binding between the two #GObject instances. The binding is released
 *   whenever the #GBinding reference count reaches zero.
 *
 * Since: 3.16
 **/
GBinding *
camel_binding_bind_property_with_closures (gpointer source,
					   const gchar *source_property,
					   gpointer target,
					   const gchar *target_property,
					   GBindingFlags flags,
					   GClosure *transform_to,
					   GClosure *transform_from)
{
	GBinding *binding;

	g_rec_mutex_lock (&camel_binding_lock);

	binding = g_object_bind_property_with_closures (source, source_property, target, target_property, flags,
		transform_to, transform_from);

	g_rec_mutex_unlock (&camel_binding_lock);

	return binding;
}

static gint
sort_paths_by_index (gconstpointer aa,
		     gconstpointer bb,
		     gpointer user_data)
{
	GHashTable *paths_hash = user_data;
	const gchar *path1 = *((gchar **) aa);
	const gchar *path2 = *((gchar **) bb);
	gint val1, val2;

	val1 = GPOINTER_TO_INT (g_hash_table_lookup (paths_hash, path1));
	val2 = GPOINTER_TO_INT (g_hash_table_lookup (paths_hash, path2));

	return val1 - val2;
}

/**
 * camel_util_get_directory_variants:
 * @main_path: the main path to work with
 * @replace_prefix: path prefix to replace
 * @with_modules_dir: whether to add also the modules directory
 *
 * The @main_path is a directory, which will be always used. It
 * should have as its prefix the @replace_prefix, otherwise
 * the function returns only the @main_path in the paths array.
 *
 * When there's exported an environment variable EDS_EXTRA_PREFIXES,
 * it is used as a list of alternative prefixes where to look for
 * the @main_path (rest after the @replace_prefix).
 *
 * When the @with_modules_dir is %TRUE, there's also added
 * g_get_user_data_dir() + "evolution/modules/", aka
 * ~/.local/share/evolution/modules/, into the resulting array.
 *
 * Returns: (element-type utf8) (transfer container): a %GPtrArray
 *    with paths to use, including the @main_path. Free it with
 *    g_ptr_array_unref(), when no longer needed.
 *
 * Since: 3.40
 **/
GPtrArray *
camel_util_get_directory_variants (const gchar *main_path,
				   const gchar *replace_prefix,
				   gboolean with_modules_dir)
{
	GPtrArray *paths;
	GHashTable *paths_hash;
	GHashTableIter iter;
	gpointer key;
	gint index = 0;

	g_return_val_if_fail (main_path && *main_path, NULL);
	g_return_val_if_fail (replace_prefix && *replace_prefix, NULL);

	paths_hash = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (paths_hash, g_strdup (main_path), GINT_TO_POINTER (index++));

	if (g_str_has_prefix (main_path, replace_prefix)) {
		const gchar *add_path;
		guint len = strlen (replace_prefix);

		if (replace_prefix[len - 1] == G_DIR_SEPARATOR)
			len--;

		add_path = main_path + len;

		if (add_path[0] == G_DIR_SEPARATOR) {
			const gchar *env = g_getenv ("EDS_EXTRA_PREFIXES");

			/* Skip the directory separator */
			add_path++;

			if (env) {
				gchar **strv;
				guint ii;

				strv = g_strsplit (env,
				#ifdef G_OS_WIN32
					";",
				#else
					":",
				#endif
					-1);

				for (ii = 0; strv && strv[ii]; ii++) {
					const gchar *prefix = strv[ii];

					if (*prefix) {
						gchar *path = g_build_filename (prefix, add_path, NULL);

						if (!path || g_hash_table_contains (paths_hash, path))
							g_free (path);
						else
							g_hash_table_insert (paths_hash, path, GINT_TO_POINTER (index++));
					}
				}

				g_strfreev (strv);
			}

			if (with_modules_dir) {
				gchar *path = g_build_filename (g_get_user_data_dir (), "evolution", "modules", add_path, NULL);

				if (!path || g_hash_table_contains (paths_hash, path))
					g_free (path);
				else
					g_hash_table_insert (paths_hash, path, GINT_TO_POINTER (index++));
			}
		}
	}

	paths = g_ptr_array_new_with_free_func (g_free);
	g_hash_table_iter_init (&iter, paths_hash);

	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		g_ptr_array_add (paths, key);
	}

	g_ptr_array_sort_with_data (paths, sort_paths_by_index, paths_hash);

	g_hash_table_destroy (paths_hash);

	return paths;
}
