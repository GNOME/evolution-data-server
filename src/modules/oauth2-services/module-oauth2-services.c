/*
 * Copyright (C) 2018 Red Hat, Inc. (www.redhat.com)
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
 */

#include "evolution-data-server-config.h"

#include <gmodule.h>

#include <libedataserver/libedataserver.h>
#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_OAUTH2_SOURCE_MONITOR \
	(e_oauth2_source_monitor_get_type ())
#define E_OAUTH2_SOURCE_MONITOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OAUTH2_SOURCE_MONITOR, EOAuth2SourceMonitor))
#define E_IS_OAUTH2_SOURCE_MONITOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_OAUTH2_SOURCE_MONITOR))

typedef struct _EOAuth2SourceMonitor EOAuth2SourceMonitor;
typedef struct _EOAuth2SourceMonitorClass EOAuth2SourceMonitorClass;

struct _EOAuth2SourceMonitor {
	EExtension parent;

	EOAuth2Services *oauth2_services;
};

struct _EOAuth2SourceMonitorClass {
	EExtensionClass parent_class;
};

/* Forward Declarations */
GType e_oauth2_source_monitor_get_type (void);
static void e_oauth2_source_monitor_oauth2_support_init (EOAuth2SupportInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EOAuth2SourceMonitor, e_oauth2_source_monitor, E_TYPE_EXTENSION, 0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (E_TYPE_OAUTH2_SUPPORT, e_oauth2_source_monitor_oauth2_support_init))

static ESourceRegistryServer *
oauth2_source_monitor_get_registry_server (EOAuth2SourceMonitor *extension)
{
	return E_SOURCE_REGISTRY_SERVER (e_extension_get_extensible (E_EXTENSION (extension)));
}

static gboolean
oauth2_source_monitor_get_access_token_sync (EOAuth2Support *support,
					     ESource *source,
					     GCancellable *cancellable,
					     gchar **out_access_token,
					     gint *out_expires_in,
					     GError **error)
{
	EOAuth2ServiceRefSourceFunc ref_source;
	ESourceRegistryServer *registry_server;
	EOAuth2SourceMonitor *extension;
	EOAuth2Service *service;
	gboolean success;

	g_return_val_if_fail (E_IS_OAUTH2_SOURCE_MONITOR (support), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	extension = E_OAUTH2_SOURCE_MONITOR (support);
	service = e_oauth2_services_find (extension->oauth2_services, source);
	g_return_val_if_fail (service != NULL, FALSE);

	ref_source = (EOAuth2ServiceRefSourceFunc) e_source_registry_server_ref_source;
	registry_server = oauth2_source_monitor_get_registry_server (extension);

	success = e_oauth2_service_get_access_token_sync (service, source, ref_source, registry_server,
		out_access_token, out_expires_in, cancellable, error);

	g_clear_object (&service);

	return success;
}

static void
oauth2_source_monitor_update_source (EOAuth2SourceMonitor *extension,
				     ESource *source,
				     gboolean is_new_source);

static void
oauth2_source_monitor_method_changed_cb (ESourceExtension *auth_extension,
					 GParamSpec *param,
					 EOAuth2SourceMonitor *extension)
{
	ESource *source;

	g_return_if_fail (E_IS_SOURCE_EXTENSION (auth_extension));
	g_return_if_fail (E_IS_OAUTH2_SOURCE_MONITOR (extension));

	source = e_source_extension_ref_source (auth_extension);
	if (source) {
		oauth2_source_monitor_update_source (extension, source, FALSE);
		g_clear_object (&source);
	}
}

static void
oauth2_source_monitor_update_source (EOAuth2SourceMonitor *extension,
				     ESource *source,
				     gboolean is_new_source)
{
	ESourceAuthentication *authentication_extension;
	EServerSideSource *server_source;
	gchar *auth_method;

	g_return_if_fail (E_IS_OAUTH2_SOURCE_MONITOR (extension));
	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));

	if (!extension->oauth2_services ||
	    !e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION) ||
	    e_source_has_extension (source, E_SOURCE_EXTENSION_GOA) ||
	    e_source_has_extension (source, E_SOURCE_EXTENSION_UOA))
		return;

	server_source = E_SERVER_SIDE_SOURCE (source);
	authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	auth_method = e_source_authentication_dup_method (authentication_extension);

	if (e_oauth2_services_is_oauth2_alias (extension->oauth2_services, auth_method)) {
		e_server_side_source_set_oauth2_support (server_source, E_OAUTH2_SUPPORT (extension));
	} else {
		EOAuth2Support *existing;

		existing = e_server_side_source_ref_oauth2_support (server_source);
		if (existing == E_OAUTH2_SUPPORT (extension))
			e_server_side_source_set_oauth2_support (server_source, NULL);

		g_clear_object (&existing);
	}

	g_free (auth_method);

	if (is_new_source) {
		g_signal_connect (authentication_extension, "notify::method",
			G_CALLBACK (oauth2_source_monitor_method_changed_cb), extension);
	}
}

static void
oauth2_source_monitor_source_added_cb (ESourceRegistryServer *server,
				       ESource *source,
				       EOAuth2SourceMonitor *extension)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server));
	g_return_if_fail (E_IS_SERVER_SIDE_SOURCE (source));
	g_return_if_fail (E_IS_OAUTH2_SOURCE_MONITOR (extension));

	oauth2_source_monitor_update_source (extension, source, TRUE);
}

static void
oauth2_source_monitor_bus_acquired_cb (EDBusServer *dbus_server,
                                       GDBusConnection *connection,
                                       EOAuth2SourceMonitor *extension)
{
	ESourceRegistryServer *server;
	GList *sources, *link;

	g_return_if_fail (E_IS_OAUTH2_SOURCE_MONITOR (extension));

	server = oauth2_source_monitor_get_registry_server (extension);

	if (!server || !extension->oauth2_services)
		return;

	sources = e_source_registry_server_list_sources (server, NULL);
	for (link = sources; link; link = g_list_next (link)) {
		ESource *source = link->data;

		oauth2_source_monitor_source_added_cb (server, source, extension);
	}

	g_list_free_full (sources, g_object_unref);

	g_signal_connect (server, "source-added",
		G_CALLBACK (oauth2_source_monitor_source_added_cb), extension);
}

static void
oauth2_source_monitor_dispose (GObject *object)
{
	EOAuth2SourceMonitor *extension;
	ESourceRegistryServer *server;

	extension = E_OAUTH2_SOURCE_MONITOR (object);

	server = oauth2_source_monitor_get_registry_server (extension);
	if (server) {
		GList *sources, *link;

		sources = e_source_registry_server_list_sources (server, NULL);
		for (link = sources; link; link = g_list_next (link)) {
			ESource *source = link->data;

			if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
				ESourceAuthentication *auth_extension;

				auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
				g_signal_handlers_disconnect_by_func (auth_extension,
					G_CALLBACK (oauth2_source_monitor_method_changed_cb), extension);
			}
		}

		g_list_free_full (sources, g_object_unref);
	}

	g_clear_object (&extension->oauth2_services);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_oauth2_source_monitor_parent_class)->dispose (object);
}

static void
oauth2_source_monitor_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	/* Wait for the registry service to acquire its well-known
	 * bus name so we don't do anything destructive beforehand. */

	g_signal_connect (
		extensible, "bus-acquired",
		G_CALLBACK (oauth2_source_monitor_bus_acquired_cb),
		extension);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_oauth2_source_monitor_parent_class)->constructed (object);
}

static void
e_oauth2_source_monitor_class_init (EOAuth2SourceMonitorClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = oauth2_source_monitor_dispose;
	object_class->constructed = oauth2_source_monitor_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SOURCE_REGISTRY_SERVER;
}

static void
e_oauth2_source_monitor_oauth2_support_init (EOAuth2SupportInterface *iface)
{
	iface->get_access_token_sync = oauth2_source_monitor_get_access_token_sync;
}

static void
e_oauth2_source_monitor_class_finalize (EOAuth2SourceMonitorClass *class)
{
}

static void
e_oauth2_source_monitor_init (EOAuth2SourceMonitor *extension)
{
	extension->oauth2_services = e_oauth2_services_new ();
}

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_oauth2_source_monitor_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}
