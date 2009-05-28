/*
 * Evolution calendar - caldav backend factory
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Christian Kellner <gicmo@gnome.org>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-caldav-factory.h"
#include "e-cal-backend-caldav.h"

typedef struct {
	ECalBackendFactory parent_object;
} ECalBackendCalDAVFactory;

typedef struct {
	ECalBackendFactoryClass parent_class;
} ECalBackendCalDAVFactoryClass;

static void
ecb_caldav_factory_instance_init (ECalBackendCalDAVFactory *factory)
{
}

static const gchar *
_get_protocol (ECalBackendFactory *factory)
{
	return "caldav";
}

#define declare_functions(_type,_name)						\
										\
static ECalBackend*								\
_new_backend_ ## _type (ECalBackendFactory *factory, ESource *source)		\
{										\
	   return g_object_new (E_TYPE_CAL_BACKEND_CALDAV,			\
				"source", source,				\
				"kind", ICAL_ ## _type ## _COMPONENT,		\
				NULL);						\
}										\
										\
static icalcomponent_kind							\
_get_kind_ ## _type (ECalBackendFactory *factory)				\
{										\
	return ICAL_ ## _type ## _COMPONENT;					\
}										\
										\
static void									\
_backend_factory_class_init_ ## _type (ECalBackendCalDAVFactoryClass *klass)	\
{										\
	ECalBackendFactoryClass *bc =  E_CAL_BACKEND_FACTORY_CLASS (klass);	\
										\
	g_return_if_fail (bc != NULL);						\
										\
	bc->get_protocol = _get_protocol;					\
	bc->get_kind     = _get_kind_ ## _type;					\
	bc->new_backend  = _new_backend_ ## _type;				\
}										\
										\
static GType									\
backend_factory_get_type_ ## _type (GTypeModule *module)			\
{										\
	static GType type = 0;							\
										\
	GTypeInfo info = {							\
		sizeof (ECalBackendCalDAVFactoryClass),				\
		NULL, /* base_class_init */					\
		NULL, /* base_class_finalize */					\
		(GClassInitFunc)  _backend_factory_class_init_ ## _type,	\
		NULL, /* class_finalize */					\
		NULL, /* class_data */						\
		sizeof (ECalBackend),						\
		0,    /* n_preallocs */						\
		(GInstanceInitFunc) ecb_caldav_factory_instance_init		\
	   };									\
										\
	if (!type) {								\
		type = g_type_module_register_type (module,			\
			E_TYPE_CAL_BACKEND_FACTORY,				\
			_name,							\
			&info, 0);						\
	}									\
										\
	return type;								\
}										\

declare_functions (VEVENT,   "ECalBackendCalDAVEventsFactory")
declare_functions (VTODO,    "ECalBackendCalDAVTodosFactory")
declare_functions (VJOURNAL, "ECalBackendCalDAVMemosFactory")

static GType caldav_types[3];

void
eds_module_initialize (GTypeModule *module)
{
	caldav_types[0] = backend_factory_get_type_VEVENT   (module);
	caldav_types[1] = backend_factory_get_type_VTODO    (module);
	caldav_types[2] = backend_factory_get_type_VJOURNAL (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
	*types = caldav_types;
	*num_types = 3;
}
