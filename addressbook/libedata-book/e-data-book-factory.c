/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
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
 * Authors: Ross Burton <ross@linux.intel.com>
 */

/**
 * SECTION: e-data-book-factory
 * @include: libedata-book/libedata-book.h
 * @short_description: The main addressbook server object
 *
 * This class handles incomming D-Bus connections and creates
 * the #EDataBook layer for server side addressbooks to communicate
 * with client side #EBookClient objects.
 **/
#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

/* Private D-Bus classes. */
#include <e-dbus-address-book-factory.h>

#include "e-book-backend.h"
#include "e-book-backend-factory.h"
#include "e-data-book.h"
#include "e-data-book-factory.h"

#define d(x)

#define E_DATA_BOOK_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate))

struct _EDataBookFactoryPrivate {
	EDBusAddressBookFactory *dbus_factory;
};

/* Forward Declarations */
static void	e_data_book_factory_initable_init
						(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EDataBookFactory,
	e_data_book_factory,
	E_TYPE_DATA_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_book_factory_initable_init))

static GDBusInterfaceSkeleton *
data_book_factory_get_dbus_interface_skeleton (EDBusServer *server)
{
	EDataBookFactory *factory;

	factory = E_DATA_BOOK_FACTORY (server);

	return G_DBUS_INTERFACE_SKELETON (factory->priv->dbus_factory);
}

static const gchar *
data_book_get_factory_name (EBackendFactory *backend_factory)
{
	EBookBackendFactoryClass *class;

	class = E_BOOK_BACKEND_FACTORY_GET_CLASS (E_BOOK_BACKEND_FACTORY (backend_factory));

	return class->factory_name;
}

static void
data_book_complete_open (EDataFactory *data_factory,
			 GDBusMethodInvocation *invocation,
			 const gchar *object_path,
			 const gchar *bus_name,
			 const gchar *extension_name)
{
	EDataBookFactory *data_book_factory = E_DATA_BOOK_FACTORY (data_factory);

	e_dbus_address_book_factory_complete_open_address_book (
		data_book_factory->priv->dbus_factory, invocation, object_path, bus_name);
}

static gchar *overwrite_subprocess_book_path = NULL;

static gboolean
data_book_factory_handle_open_address_book_cb (EDBusAddressBookFactory *iface,
                                               GDBusMethodInvocation *invocation,
                                               const gchar *uid,
                                               EDataBookFactory *factory)
{
	EDataFactory *data_factory = E_DATA_FACTORY (factory);

	e_data_factory_spawn_subprocess_backend (
		data_factory, invocation, uid, E_SOURCE_EXTENSION_ADDRESS_BOOK,
		overwrite_subprocess_book_path ? overwrite_subprocess_book_path : SUBPROCESS_BOOK_BACKEND_PATH);

	return TRUE;
}

static void
data_book_factory_dispose (GObject *object)
{
	EDataBookFactory *factory;
	EDataBookFactoryPrivate *priv;

	factory = E_DATA_BOOK_FACTORY (object);
	priv = factory->priv;

	g_clear_object (&priv->dbus_factory);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose (object);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	const gchar *modules_directory = BACKENDDIR;
	const gchar *modules_directory_env;
	const gchar *subprocess_book_path_env;

	modules_directory_env = g_getenv (EDS_ADDRESS_BOOK_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	subprocess_book_path_env = g_getenv (EDS_SUBPROCESS_BOOK_PATH);
	if (subprocess_book_path_env &&
	    g_file_test (subprocess_book_path_env, G_FILE_TEST_IS_EXECUTABLE))
		overwrite_subprocess_book_path = g_strdup (subprocess_book_path_env);

	g_type_class_add_private (class, sizeof (EDataBookFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_book_factory_dispose;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = ADDRESS_BOOK_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_BOOK_BACKEND_FACTORY;
	data_factory_class->factory_object_path = "/org/gnome/evolution/dataserver/AddressBookFactory";
	data_factory_class->subprocess_object_path_prefix = "/org/gnome/evolution/dataserver/Subprocess/Backend/AddressBook";
	data_factory_class->subprocess_bus_name_prefix = "org.gnome.evolution.dataserver.Subprocess.Backend.AddressBook";
	data_factory_class->get_dbus_interface_skeleton = data_book_factory_get_dbus_interface_skeleton;
	data_factory_class->get_factory_name = data_book_get_factory_name;
	data_factory_class->complete_open = data_book_complete_open;
}

static void
e_data_book_factory_initable_init (GInitableIface *iface)
{
}

static void
e_data_book_factory_init (EDataBookFactory *factory)
{
	factory->priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (factory);

	factory->priv->dbus_factory =
		e_dbus_address_book_factory_skeleton_new ();

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-address-book",
		G_CALLBACK (data_book_factory_handle_open_address_book_cb),
		factory);
}

EDBusServer *
e_data_book_factory_new (GCancellable *cancellable,
                         GError **error)
{
	return g_initable_new (
		E_TYPE_DATA_BOOK_FACTORY,
		cancellable, error,
		"reload-supported", TRUE, NULL);
}
