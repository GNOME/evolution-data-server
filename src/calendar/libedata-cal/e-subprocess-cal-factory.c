/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2014 Red Hat, Inc. (www.redhat.com)
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
 * Authors: Fabiano Fidêncio <fidencio@redhat.com>
 */

/*
 * This class handles and creates #EBackend objects from inside
 * their own subprocesses and also serves as the layer that does
 * the communication between #EDataCalFactory and #EBackend
 */

#include "evolution-data-server-config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>

#include "e-cal-backend.h"
#include "e-cal-backend-factory.h"
#include "e-data-cal.h"
#include "e-subprocess-cal-factory.h"

#include <e-dbus-subprocess-backend.h>

/* Forward Declarations */
static void	e_subprocess_cal_factory_initable_init
						(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	ESubprocessCalFactory,
	e_subprocess_cal_factory,
	E_TYPE_SUBPROCESS_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_subprocess_cal_factory_initable_init))

static gchar *
subprocess_cal_factory_open (ESubprocessFactory *subprocess_factory,
			     EBackend *backend,
			     GDBusConnection *connection,
			     gpointer data,
			     GCancellable *cancellable,
			     GError **error)
{
	EDataCal *data_cal;
	gchar *object_path;

	/* If the backend already has an EDataCal installed, return its
	 * object path.  Otherwise we need to install a new EDataCal. */
	data_cal = e_cal_backend_ref_data_cal (E_CAL_BACKEND (backend));

	if (data_cal != NULL) {
		object_path = g_strdup (e_data_cal_get_object_path (data_cal));
	} else {
		object_path = e_subprocess_factory_construct_path ();

		/* The EDataCal will attach itself to ECalBackend,
		 * so no need to call e_cal_backend_set_data_cal(). */
		data_cal = e_data_cal_new (
			E_CAL_BACKEND (backend),
			connection, object_path, error);

		if (data_cal != NULL) {
			e_subprocess_factory_set_backend_callbacks (
				subprocess_factory, backend, data);
		} else {
			g_free (object_path);
			object_path = NULL;
		}
	}

	g_clear_object (&data_cal);

	return object_path;
}

static EBackend *
subprocess_cal_factory_ref_backend (ESourceRegistry *registry,
				     ESource *source,
				     const gchar *backend_factory_type_name)
{
	ECalBackendFactoryClass *backend_factory_class;
	GType backend_factory_type;

	backend_factory_type = g_type_from_name (backend_factory_type_name);
	if (!backend_factory_type)
		return NULL;

	backend_factory_class = g_type_class_ref (backend_factory_type);
	if (!backend_factory_class)
		return NULL;

	return g_object_new (
		backend_factory_class->backend_type,
		"kind", backend_factory_class->component_kind,
		"registry", registry,
		"source", source, NULL);
}

static void
e_subprocess_cal_factory_class_init (ESubprocessCalFactoryClass *class)
{
	ESubprocessFactoryClass *subprocess_factory_class;

	subprocess_factory_class = E_SUBPROCESS_FACTORY_CLASS (class);
	subprocess_factory_class->ref_backend = subprocess_cal_factory_ref_backend;
	subprocess_factory_class->open_data = subprocess_cal_factory_open;
}

static void
e_subprocess_cal_factory_initable_init (GInitableIface *iface)
{
}

static void
e_subprocess_cal_factory_init (ESubprocessCalFactory *subprocess_factory)
{
}

ESubprocessCalFactory *
e_subprocess_cal_factory_new (GCancellable *cancellable,
			       GError **error)
{
	i_cal_set_unknown_token_handling_setting (I_CAL_DISCARD_TOKEN);

	return g_initable_new (
		E_TYPE_SUBPROCESS_CAL_FACTORY,
		cancellable, error, NULL);
}
