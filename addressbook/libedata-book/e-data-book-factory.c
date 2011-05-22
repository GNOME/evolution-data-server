/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <libebackend/e-data-server-module.h>
#include <libebackend/e-offline-listener.h>
#include "e-book-backend-factory.h"
#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-book-backend.h"

#include "e-gdbus-egdbusbookfactory.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include <conio.h>
#ifndef PROCESS_DEP_ENABLE
#define PROCESS_DEP_ENABLE 0x00000001
#endif
#ifndef PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x00000002
#endif
#endif

#define d(x)

static GMainLoop *loop;

/* Keep running after the last client is closed. */
static gboolean opt_keep_running = FALSE;

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) G_STMT_START{ \
		if G_LIKELY (test) {} else {				\
			g_set_error (error, domain, code, #test);	\
			g_warning(#test " failed");			\
			return (returnval);				\
		}							\
	}G_STMT_END

G_DEFINE_TYPE (EDataBookFactory, e_data_book_factory, G_TYPE_OBJECT);

struct _EDataBookFactoryPrivate {
	EGdbusBookFactory *gdbus_object;

	/* 'protocol' -> EBookBackendFactory hash table */
	GHashTable *factories;

	GMutex *backends_lock;
	/* 'uri' -> EBookBackend */
	GHashTable *backends;

	GMutex *books_lock;
	/* A hash of object paths for book URIs to EDataBooks */
	GHashTable *books;

	GMutex *connections_lock;
	/* This is a hash of client addresses to GList* of EDataBooks */
	GHashTable *connections;

	guint exit_timeout;

        gint mode;
};

/* Forward Declarations */
void e_data_book_migrate_basedir (void);

/* Create the EDataBookFactory error quark */
GQuark
e_data_book_factory_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("e_data_book_factory_error");
	return quark;
}

/**
 * e_data_book_factory_register_backend_factory:
 * @factory: an #EDataBookFactory
 * @backend_factory: an #EBookBackendFactory
 *
 * Registers @backend_factory with @factory.
 **/
static void
e_data_book_factory_register_backend (EDataBookFactory *book_factory,
                                      EBookBackendFactory *backend_factory)
{
	EBookBackendFactoryClass *class;
	const gchar *proto;

	g_return_if_fail (E_IS_DATA_BOOK_FACTORY (book_factory));
	g_return_if_fail (E_IS_BOOK_BACKEND_FACTORY (backend_factory));

	class = E_BOOK_BACKEND_FACTORY_GET_CLASS (backend_factory);

	proto = class->get_protocol (backend_factory);

	if (g_hash_table_lookup (book_factory->priv->factories, proto) != NULL) {
		g_warning ("%s: Proto \"%s\" already registered!\n", G_STRFUNC, proto);
	}

	g_hash_table_insert (
		book_factory->priv->factories,
		g_strdup (proto), backend_factory);
}

/**
 * e_data_book_factory_register_backends:
 * @book_factory: an #EDataBookFactory
 *
 * Register the backends supported by the Evolution Data Server,
 * with @book_factory.
 **/
static void
e_data_book_factory_register_backends (EDataBookFactory *book_factory)
{
	GList *factories, *f;

	factories = e_data_server_get_extensions_for_type (E_TYPE_BOOK_BACKEND_FACTORY);
	for (f = factories; f; f = f->next) {
		EBookBackendFactory *backend_factory = f->data;

		e_data_book_factory_register_backend (
			book_factory, g_object_ref (backend_factory));
	}

	e_data_server_extension_list_free (factories);
	e_data_server_module_remove_unused ();
}

static void
set_backend_online_status (gpointer key, gpointer value, gpointer data)
{
	EBookBackend *backend = E_BOOK_BACKEND (value);

	g_return_if_fail (backend != NULL);

	e_book_backend_set_mode (backend,  GPOINTER_TO_INT (data));
}

/**
 * e_data_book_factory_set_backend_mode:
 * @factory: A bookendar factory.
 * @mode: Online mode to set.
 *
 * Sets the online mode for all backends created by the given factory.
 */
void
e_data_book_factory_set_backend_mode (EDataBookFactory *factory,
                                      gint mode)
{
	g_return_if_fail (E_IS_DATA_BOOK_FACTORY (factory));

	factory->priv->mode = mode;
	g_mutex_lock (factory->priv->backends_lock);
	g_hash_table_foreach (
		factory->priv->backends,
		set_backend_online_status,
		GINT_TO_POINTER (factory->priv->mode));
	g_mutex_unlock (factory->priv->backends_lock);
}

/* TODO: write dispose to kill hash */
static gchar *
e_data_book_factory_extract_proto_from_uri (const gchar *uri)
{
	gchar *proto, *p;
	p = strchr (uri, ':');
	if (p == NULL)
		return NULL;
	proto = g_malloc0 (p - uri + 1);
	strncpy (proto, uri, p - uri);
	return proto;
}

static EBookBackendFactory*
e_data_book_factory_lookup_backend_factory (EDataBookFactory *factory,
                                            const gchar *uri)
{
	EBookBackendFactory *backend_factory;
	gchar *proto;

	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	proto = e_data_book_factory_extract_proto_from_uri (uri);
	if (proto == NULL) {
		g_warning ("Cannot extract protocol from URI %s", uri);
		return NULL;
	}

	backend_factory = g_hash_table_lookup (factory->priv->factories, proto);

	g_free (proto);

	return backend_factory;
}

static gchar *
construct_book_factory_path (void)
{
	static volatile gint counter = 1;

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/AddressBook/%d/%u",
		getpid (), g_atomic_int_exchange_and_add (&counter, 1));
}

static gboolean
remove_dead_pointer_cb (gpointer path, gpointer live, gpointer dead)
{
	return live == dead;
}

static void
book_freed_cb (EDataBookFactory *factory, GObject *dead)
{
	EDataBookFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("in factory %p (%p) is dead", factory, dead));

	g_hash_table_foreach_remove (priv->books, remove_dead_pointer_cb, dead);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *books = hvalue;

		if (g_list_find (books, dead)) {
			books = g_list_remove (books, dead);
			if (books != NULL)
				g_hash_table_insert (
					priv->connections,
					g_strdup (hkey), books);
			else
				g_hash_table_remove (priv->connections, hkey);

			break;
		}
	}

	if (g_hash_table_size (priv->books) > 0)
		return;

	/* If there are no open books, start a timer to quit */
	if (!opt_keep_running && priv->exit_timeout == 0)
		priv->exit_timeout = g_timeout_add_seconds (
			10, (GSourceFunc) g_main_loop_quit, loop);
}

static void
backend_gone_cb (EDataBookFactory *factory, GObject *dead)
{
	EDataBookFactoryPrivate *priv = factory->priv;

	g_mutex_lock (priv->backends_lock);
	g_hash_table_foreach_remove (priv->backends, remove_dead_pointer_cb, dead);
	g_mutex_unlock (priv->backends_lock);
}

static gboolean
impl_BookFactory_getBook (EGdbusBookFactory *object, GDBusMethodInvocation *invocation, const gchar *in_source, EDataBookFactory *factory)
{
	EDataBook *book;
	EBookBackend *backend;
	EDataBookFactoryPrivate *priv = factory->priv;
	ESource *source;
	gchar *uri, *path;
	const gchar *sender;
	GList *list;
	GError *error = NULL;

	if (in_source == NULL || in_source[0] == '\0') {
		error = g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Empty URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

	g_mutex_lock (priv->backends_lock);

	source = e_source_new_from_standalone_xml (in_source);
	if (!source) {
		g_mutex_unlock (priv->backends_lock);

		error = g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	uri = e_source_get_uri (source);

	if (!uri || !*uri) {
		g_mutex_unlock (priv->backends_lock);
		g_object_unref (source);
		g_free (uri);

		error = g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Empty URI"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	g_mutex_lock (priv->books_lock);
	backend = g_hash_table_lookup (priv->backends, uri);

	if (backend == NULL) {
		EBookBackendFactory *backend_factory;

		backend_factory =
			e_data_book_factory_lookup_backend_factory (factory, uri);
		if (backend_factory != NULL)
			backend = e_book_backend_factory_new_backend (backend_factory);

		if (backend != NULL) {
			gchar *uri_key = g_strdup (uri);

			g_hash_table_insert (
				priv->backends, uri_key, backend);
			g_object_weak_ref (G_OBJECT (backend), (GWeakNotify) backend_gone_cb, factory);
			g_signal_connect (backend, "last-client-gone", G_CALLBACK (g_object_unref), NULL);
			e_book_backend_set_mode (backend, priv->mode);
		}
	}

	if (backend == NULL) {
		g_free (uri);
		g_object_unref (source);

		g_mutex_unlock (priv->books_lock);
		g_mutex_unlock (priv->backends_lock);

		error = g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Invalid source"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	path = construct_book_factory_path ();
	book = e_data_book_new (backend, source);
	g_hash_table_insert (priv->books, g_strdup (path), book);
	e_book_backend_add_client (backend, book);
	e_data_book_register_gdbus_object (book, g_dbus_method_invocation_get_connection (invocation), path, &error);
	g_object_weak_ref (
		G_OBJECT (book), (GWeakNotify)
		book_freed_cb, factory);

	/* Update the hash of open connections */
	g_mutex_lock (priv->connections_lock);
	sender = g_dbus_method_invocation_get_sender (invocation);
	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, book);
	g_hash_table_insert (priv->connections, g_strdup (sender), list);
	g_mutex_unlock (priv->connections_lock);

	g_mutex_unlock (priv->books_lock);
	g_mutex_unlock (priv->backends_lock);

	g_object_unref (source);
	g_free (uri);

	if (error) {
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);
	} else
		e_gdbus_book_factory_complete_get_book (object, invocation, path);

	g_free (path);

	return TRUE;
}

static void
remove_data_book_cb (gpointer data_bk, gpointer user_data)
{
	EDataBook *data_book;
	EBookBackend *backend;

	data_book = E_DATA_BOOK (data_bk);
	g_return_if_fail (data_book != NULL);

	backend = e_data_book_get_backend (data_book);
	e_book_backend_remove_client (backend, data_book);

	g_object_unref (data_book);
}

static void
e_data_book_factory_init (EDataBookFactory *factory)
{
	GError *error = NULL;

	factory->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		factory, E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate);

	factory->priv->gdbus_object = e_gdbus_book_factory_stub_new ();
	g_signal_connect (factory->priv->gdbus_object, "handle-get-book", G_CALLBACK (impl_BookFactory_getBook), factory);

	factory->priv->factories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	factory->priv->backends_lock = g_mutex_new ();
	factory->priv->backends = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	factory->priv->books_lock = g_mutex_new ();
	factory->priv->books = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	factory->priv->connections_lock = g_mutex_new ();
	factory->priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	if (!e_data_server_module_init (BACKENDDIR, &error))
		g_error ("%s", error->message);

	e_data_book_factory_register_backends (factory);
}

static void
e_data_book_factory_finalize (GObject *object)
{
	EDataBookFactory *factory = E_DATA_BOOK_FACTORY (object);

	g_return_if_fail (factory != NULL);

	g_object_unref (factory->priv->gdbus_object);

	g_hash_table_destroy (factory->priv->factories);
	g_hash_table_destroy (factory->priv->backends);
	g_hash_table_destroy (factory->priv->books);
	g_hash_table_destroy (factory->priv->connections);

	g_mutex_free (factory->priv->backends_lock);
	g_mutex_free (factory->priv->books_lock);
	g_mutex_free (factory->priv->connections_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->finalize (object);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *klass)
{
	GObjectClass *object_class;

	g_type_class_add_private (klass, sizeof (EDataBookFactoryPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_data_book_factory_finalize;
}

static guint
e_data_book_factory_register_gdbus_object (EDataBookFactory *factory, GDBusConnection *connection, const gchar *object_path, GError **error)
{
	g_return_val_if_fail (factory != NULL, 0);
	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), 0);
	g_return_val_if_fail (connection != NULL, 0);
	g_return_val_if_fail (object_path != NULL, 0);

	return e_gdbus_book_factory_register_object (factory->priv->gdbus_object, connection, object_path, error);
}

/* Convenience function to print an error and exit */
G_GNUC_NORETURN static void
die (const gchar *prefix, GError *error)
{
	g_error("%s: %s", prefix, error->message);
	g_error_free (error);
	exit (1);
}

static void
offline_state_changed_cb (EOfflineListener *eol, EDataBookFactory *factory)
{
	EOfflineListenerState state = e_offline_listener_get_state (eol);

	g_return_if_fail (state == EOL_STATE_ONLINE || state == EOL_STATE_OFFLINE);

	e_data_book_factory_set_backend_mode (
		factory, state == EOL_STATE_ONLINE ?
		E_DATA_BOOK_MODE_REMOTE : E_DATA_BOOK_MODE_LOCAL);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
	EDataBookFactory *factory = user_data;
	guint registration_id;
	GError *error = NULL;

	registration_id = e_data_book_factory_register_gdbus_object (
		factory,
		connection,
		"/org/gnome/evolution/dataserver/AddressBookFactory",
		&error);

	if (error)
		die ("Failed to register a BookFactory object", error);

	g_assert (registration_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
	GList *list = NULL;
	gchar *key;
	EDataBookFactory *factory = user_data;

	g_mutex_lock (factory->priv->connections_lock);
	while (g_hash_table_lookup_extended (
		factory->priv->connections, name,
		(gpointer) &key, (gpointer) &list)) {
		GList *copy = g_list_copy (list);

		/* this should trigger the book's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		g_list_foreach (copy, remove_data_book_cb, NULL);
		g_list_free (copy);
	}

	g_mutex_unlock (factory->priv->connections_lock);
}

#ifndef G_OS_WIN32
static void
term_signal (gint sig)
{
	g_return_if_fail (sig == SIGTERM);

	g_print ("Received terminate signal...\n");
	g_main_loop_quit (loop);
}

static void
setup_term_signal (void)
{
	struct sigaction sa, osa;

	sigaction (SIGTERM, NULL, &osa);

	sa.sa_flags = 0;
	sigemptyset (&sa.sa_mask);
	sa.sa_handler = term_signal;
	sigaction (SIGTERM, &sa, NULL);
}
#endif

static GOptionEntry entries[] = {

	/* FIXME Have the description translated for 3.2, but this
	 *       option is to aid in testing and development so it
	 *       doesn't really matter. */
	{ "keep-running", 'r', 0, G_OPTION_ARG_NONE, &opt_keep_running,
	  "Keep running after the last client is closed", NULL },
	{ NULL }
};

gint
main (gint argc, gchar **argv)
{
	EOfflineListener *eol;
	GOptionContext *context;
	EDataBookFactory *factory;
	guint owner_id;
	GError *error = NULL;

#ifdef G_OS_WIN32
	/* Reduce risks */
	{
		typedef BOOL (WINAPI *t_SetDllDirectoryA) (LPCSTR lpPathName);
		t_SetDllDirectoryA p_SetDllDirectoryA;

		p_SetDllDirectoryA = GetProcAddress (GetModuleHandle ("kernel32.dll"), "SetDllDirectoryA");
		if (p_SetDllDirectoryA)
			(*p_SetDllDirectoryA) ("");
	}
#ifndef _WIN64
	{
		typedef BOOL (WINAPI *t_SetProcessDEPPolicy) (DWORD dwFlags);
		t_SetProcessDEPPolicy p_SetProcessDEPPolicy;

		p_SetProcessDEPPolicy = GetProcAddress (GetModuleHandle ("kernel32.dll"), "SetProcessDEPPolicy");
		if (p_SetProcessDEPPolicy)
			(*p_SetProcessDEPPolicy) (PROCESS_DEP_ENABLE|PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION);
	}
#endif
#endif

	g_type_init ();
	g_set_prgname (E_PRGNAME);
	if (!g_thread_supported ()) g_thread_init (NULL);

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		exit (EXIT_FAILURE);
	}

	factory = g_object_new (E_TYPE_DATA_BOOK_FACTORY, NULL);

	loop = g_main_loop_new (NULL, FALSE);

	eol = e_offline_listener_new ();
	offline_state_changed_cb (eol, factory);
	g_signal_connect (
		eol, "changed",
		G_CALLBACK (offline_state_changed_cb), factory);

	owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		G_BUS_NAME_OWNER_FLAGS_NONE,
		on_bus_acquired,
		on_name_acquired,
		on_name_lost,
		factory,
		NULL);

	/* Migrate user data from ~/.evolution to XDG base directories. */
	e_data_book_migrate_basedir ();

#ifndef G_OS_WIN32
	setup_term_signal ();
#endif

	g_print ("Server is up and running...\n");

	g_main_loop_run (loop);

	g_bus_unown_name (owner_id);
	g_object_unref (eol);
	g_object_unref (factory);

	g_print ("Bye.\n");

	return 0;
}
