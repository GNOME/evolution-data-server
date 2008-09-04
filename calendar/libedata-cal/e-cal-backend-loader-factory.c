/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chenthill Palanisamy (pchenthill@novell.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-cal-backend-loader-factory.h"

static GObjectClass *parent_class = NULL;
	
static void
e_cal_backend_loader_factory_instance_init (ECalBackendLoaderFactory *factory)
{
}

static void
e_cal_backend_loader_factory_class_init (ECalBackendLoaderFactoryClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);

}

GType
e_cal_backend_loader_factory_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (ECalBackendLoaderFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_cal_backend_loader_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (ECalBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_cal_backend_loader_factory_instance_init
		};

		type = g_type_register_static (E_TYPE_CAL_BACKEND_FACTORY, "ECalBackendLoaderFactory", &info, 0);
	}

	return type;
}

