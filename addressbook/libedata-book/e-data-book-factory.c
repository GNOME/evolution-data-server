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
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib-bindings.h>
#include <libebackend/e-data-server-module.h>
#include <libebackend/e-offline-listener.h>
#include "e-book-backend-factory.h"
#include "e-data-book-factory.h"
#include "e-data-book.h"
#include "e-book-backend.h"
#include "e-book-backend-factory.h"

#define d(x)

static void impl_BookFactory_getBook(EDataBookFactory *factory, const gchar *IN_uri, DBusGMethodInvocation *context);
#include "e-data-book-factory-glue.h"

static GMainLoop *loop;
static EDataBookFactory *factory;
extern DBusGConnection *connection;

/* Convenience macro to test and set a GError/return on failure */
#define g_set_error_val_if_fail(test, returnval, error, domain, code) G_STMT_START{ \
		if G_LIKELY (test) {} else {				\
			g_set_error (error, domain, code, #test);	\
			g_warning(#test " failed");			\
			return (returnval);				\
		}							\
	}G_STMT_END

G_DEFINE_TYPE(EDataBookFactory, e_data_book_factory, G_TYPE_OBJECT);

#define E_DATA_BOOK_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate))

struct _EDataBookFactoryPrivate {
	/* TODO: as the factory is not threaded these locks could be removed */
	GMutex *backends_lock;
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
 * e_data_book_factory_register_backend:
 * @factory: an #EDataBookFactory
 * @backend_factory: an #EBookBackendFactory
 *
 * Registers @backend_factory with @factory.
 **/
static void
e_data_book_factory_register_backend (EDataBookFactory      *book_factory,
				      EBookBackendFactory   *backend_factory)
{
	const gchar *proto;

	g_return_if_fail (E_IS_DATA_BOOK_FACTORY (book_factory));
	g_return_if_fail (E_IS_BOOK_BACKEND_FACTORY (backend_factory));

	proto = E_BOOK_BACKEND_FACTORY_GET_CLASS (backend_factory)->get_protocol (backend_factory);

	if (g_hash_table_lookup (book_factory->priv->backends, proto) != NULL) {
		g_warning ("e_data_book_factory_register_backend: "
			   "Proto \"%s\" already registered!\n", proto);
	}

	g_hash_table_insert (book_factory->priv->backends,
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

		e_data_book_factory_register_backend (book_factory, g_object_ref (backend_factory));
	}

	e_data_server_extension_list_free (factories);
	e_data_server_module_remove_unused ();
}

static void
set_backend_online_status (gpointer key, gpointer value, gpointer data)
{
	GList *books;
	EBookBackend *backend = NULL;

	for (books = (GList *) value; books; books = g_list_next (books)) {
		backend =  E_BOOK_BACKEND (books->data);
		e_book_backend_set_mode (backend,  GPOINTER_TO_INT (data));
	}
}

/**
 * e_data_book_factory_set_backend_mode:
 * @factory: A bookendar factory.
 * @mode: Online mode to set.
 *
 * Sets the online mode for all backends created by the given factory.
 */
void
e_data_book_factory_set_backend_mode (EDataBookFactory *factory, gint mode)
{
	EDataBookFactoryPrivate *priv = factory->priv;

	priv->mode = mode;
	g_mutex_lock (priv->connections_lock);
	g_hash_table_foreach (priv->connections, set_backend_online_status, GINT_TO_POINTER (priv->mode));
	g_mutex_unlock (priv->connections_lock);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *e_data_book_factory_class)
{
	g_type_class_add_private (e_data_book_factory_class, sizeof (EDataBookFactoryPrivate));
	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (e_data_book_factory_class), &dbus_glib_e_data_book_factory_object_info);
}

/* Instance init */
static void
e_data_book_factory_init (EDataBookFactory *factory)
{
	factory->priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (factory);

	factory->priv->backends_lock = g_mutex_new ();
	factory->priv->backends = g_hash_table_new (g_str_hash, g_str_equal);

	factory->priv->books_lock = g_mutex_new ();
	factory->priv->books = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	factory->priv->connections_lock = g_mutex_new ();
	factory->priv->connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	e_data_server_module_init ();
	e_data_book_factory_register_backends (factory);
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
					    const gchar     *uri)
{
	EBookBackendFactory *backend_factory;
	gchar                *proto;

	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	proto = e_data_book_factory_extract_proto_from_uri (uri);
	if (proto == NULL) {
		g_warning ("Cannot extract protocol from URI %s", uri);
		return NULL;
	}

	backend_factory = g_hash_table_lookup (factory->priv->backends, proto);

	g_free (proto);

	return backend_factory;
}

static gchar *
construct_book_factory_path (void)
{
	static volatile gint counter = 1;

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/addressbook/%d/%u",
		getpid (),
		g_atomic_int_exchange_and_add (&counter, 1));
}

static void
my_remove (gchar *key, GObject *dead)
{
	EDataBookFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("%s (%p) is dead", key, dead));

	g_hash_table_remove (priv->books, key);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *books = hvalue;

		if (g_list_find (books, dead)) {
			books = g_list_remove (books, dead);
			if (books) {
				g_hash_table_insert (priv->connections, g_strdup (hkey), books);
			} else {
				g_hash_table_remove (priv->connections, hkey);
			}

			break;
		}
	}

	g_free (key);

	/* If there are no open books, start a timer to quit */
	if (priv->exit_timeout == 0 && g_hash_table_size (priv->books) == 0) {
		priv->exit_timeout = g_timeout_add (10000, (GSourceFunc)g_main_loop_quit, loop);
	}
}

static void
impl_BookFactory_getBook(EDataBookFactory *factory, const gchar *IN_source, DBusGMethodInvocation *context)
{
	EDataBook *book;
	EBookBackend *backend;
	EDataBookFactoryPrivate *priv = factory->priv;
	ESource *source;
	gchar *uri, *path, *sender;
	GList *list;

	if (IN_source == NULL || IN_source[0] == '\0') {
		dbus_g_method_return_error (context, g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Empty URI")));
		return;
	}

	/* Remove a pending exit */
	if (priv->exit_timeout) {
		g_source_remove (priv->exit_timeout);
		priv->exit_timeout = 0;
	}

	g_mutex_lock (priv->backends_lock);

	source = e_source_new_from_standalone_xml (IN_source);
	if (!source) {
		g_mutex_unlock (priv->backends_lock);
		dbus_g_method_return_error (context, g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Invalid source")));
		return;
	}

	uri = e_source_get_uri (source);

	g_mutex_lock (priv->books_lock);
	backend = g_hash_table_lookup (priv->backends, uri);

	if (!backend) {
		EBookBackendFactory *backend_factory = e_data_book_factory_lookup_backend_factory (factory, uri);

		if (backend_factory)
			backend = e_book_backend_factory_new_backend (backend_factory);

		if (backend) {
			g_hash_table_insert (priv->backends, g_strdup (uri), backend);
			e_book_backend_set_mode (backend, priv->mode);
		}
	}

	if (!backend) {
		g_free (uri);
		g_object_unref (source);

		g_mutex_unlock (priv->books_lock);
		g_mutex_unlock (priv->backends_lock);
		dbus_g_method_return_error (context, g_error_new (E_DATA_BOOK_ERROR, E_DATA_BOOK_STATUS_NO_SUCH_BOOK, _("Invalid source")));
		return;
	}

	path = construct_book_factory_path ();
	book = e_data_book_new (backend, source);
	g_hash_table_insert (priv->books, g_strdup (path), book);
	e_book_backend_add_client (backend, book);
	dbus_g_connection_register_g_object (connection, path, G_OBJECT (book));
	g_object_weak_ref (G_OBJECT (book), (GWeakNotify)my_remove, g_strdup (path));

	/* Update the hash of open connections */
	g_mutex_lock (priv->connections_lock);
	sender = dbus_g_method_get_sender (context);
	list = g_hash_table_lookup (priv->connections, sender);
	list = g_list_prepend (list, book);
	g_hash_table_insert (priv->connections, sender, list);
	g_mutex_unlock (priv->connections_lock);

	g_mutex_unlock (priv->books_lock);
	g_mutex_unlock (priv->backends_lock);

	g_object_unref (source);
	g_free (uri);

	dbus_g_method_return (context, path);
}

static void
name_owner_changed (DBusGProxy *proxy,
                    const gchar *name,
                    const gchar *prev_owner,
                    const gchar *new_owner,
                    EDataBookFactory *factory)
{
	if (strcmp (new_owner, "") == 0 && strcmp (name, prev_owner) == 0) {
		gchar *key;
		GList *list = NULL;
		g_mutex_lock (factory->priv->connections_lock);
		while (g_hash_table_lookup_extended (factory->priv->connections, prev_owner, (gpointer)&key, (gpointer)&list)) {
			/* this should trigger the book's weak ref notify
			 * function, which will remove it from the list before
			 * it's freed, and will remove the connection from
			 * priv->connections once they're all gone */
			g_object_unref (list->data);
		}

		g_mutex_unlock (factory->priv->connections_lock);
	}
}

/* Convenience function to print an error and exit */
G_GNUC_NORETURN static void
die (const gchar *prefix, GError *error)
{
	g_error("%s: %s", prefix, error->message);
	g_error_free (error);
	exit(1);
}

static void
offline_state_changed_cb (EOfflineListener *eol, EDataBookFactory *factory)
{
	EOfflineListenerState state = e_offline_listener_get_state (eol);

	g_return_if_fail (state == EOL_STATE_ONLINE || state == EOL_STATE_OFFLINE);

	e_data_book_factory_set_backend_mode (factory, state == EOL_STATE_ONLINE ? E_DATA_BOOK_MODE_REMOTE : E_DATA_BOOK_MODE_LOCAL);
}

#define E_DATA_BOOK_FACTORY_SERVICE_NAME "org.gnome.evolution.dataserver.AddressBook"

gint
main (gint argc, gchar **argv)
{
	GError *error = NULL;
	DBusGProxy *bus_proxy;
	guint32 request_name_ret;
	EOfflineListener *eol;

	g_type_init ();
	g_set_prgname (E_PRGNAME);
	if (!g_thread_supported ()) g_thread_init (NULL);
	dbus_g_thread_init ();

	loop = g_main_loop_new (NULL, FALSE);

	/* Obtain a connection to the session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL)
		die ("Failed to open connection to bus", error);

	bus_proxy = dbus_g_proxy_new_for_name (connection,
					       DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS,
					       DBUS_INTERFACE_DBUS);

	if (!org_freedesktop_DBus_request_name (bus_proxy, E_DATA_BOOK_FACTORY_SERVICE_NAME,
						0, &request_name_ret, &error))
		die ("Failed to get name", error);

	if (request_name_ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_error ("Got result code %u from requesting name", request_name_ret);
		exit (1);
	}

	factory = g_object_new (E_TYPE_DATA_BOOK_FACTORY, NULL);
	dbus_g_connection_register_g_object (connection,
					     "/org/gnome/evolution/dataserver/addressbook/BookFactory",
					     G_OBJECT (factory));

	dbus_g_proxy_add_signal (bus_proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (bus_proxy, "NameOwnerChanged", G_CALLBACK (name_owner_changed), factory, NULL);

	eol = e_offline_listener_new ();
	offline_state_changed_cb (eol, factory);
	g_signal_connect (eol, "changed", G_CALLBACK (offline_state_changed_cb), factory);

	printf ("Server is up and running...\n");

	g_main_loop_run (loop);

	g_object_unref (eol);

	dbus_g_connection_unref (connection);

	printf ("Bye.\n");

	return 0;
}
