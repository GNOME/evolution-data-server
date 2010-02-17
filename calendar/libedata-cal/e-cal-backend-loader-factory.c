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

G_DEFINE_TYPE (ECalBackendLoaderFactory, e_cal_backend_loader_factory, E_TYPE_CAL_BACKEND_FACTORY)

static GObjectClass *parent_class = NULL;

static void
e_cal_backend_loader_factory_init (ECalBackendLoaderFactory *factory)
{
}

static void
e_cal_backend_loader_factory_class_init (ECalBackendLoaderFactoryClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);

}
