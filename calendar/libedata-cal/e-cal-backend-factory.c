/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-factory.h"

static void
e_cal_backend_factory_instance_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_factory_class_init (ECalBackendFactoryClass *klass)
{
}

GType
e_cal_backend_factory_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (ECalBackendFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_cal_backend_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ECalBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_cal_backend_factory_instance_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "ECalBackendFactory", &info, 0);
	}

	return type;
}

/**
 * e_cal_backend_factory_get_kind:
 * @factory: An #ECalBackendFactory object.
 *
 * Gets the component type of the factory.
 *
 * Return value: The kind of factory.
 */
icalcomponent_kind
e_cal_backend_factory_get_kind (ECalBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_FACTORY (factory), 0/*XXX*/);

	return E_CAL_BACKEND_FACTORY_GET_CLASS (factory)->get_kind (factory);
}

/**
 * e_cal_backend_factory_get_protocol:
 * @factory: An #ECalBackendFactory object.
 *
 * Gets the protocol used by the factory.
 *
 * Return value: The protocol.
 */
const char*
e_cal_backend_factory_get_protocol (ECalBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_FACTORY (factory), NULL);

	return E_CAL_BACKEND_FACTORY_GET_CLASS (factory)->get_protocol (factory);
}

/**
 * e_cal_backend_factory_new_backend:
 * @factory: An #ECalBackendFactory object.
 * @source: An #ESource.
 *
 * Creates a new backend for the given @source.
 *
 * Return value: The newly created backend, or NULL if there was an error.
 */
ECalBackend*
e_cal_backend_factory_new_backend (ECalBackendFactory *factory, ESource *source)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_FACTORY (factory), NULL);

	return E_CAL_BACKEND_FACTORY_GET_CLASS (factory)->new_backend (factory, source);
}
