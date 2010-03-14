/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-cal-backend-loader-factory.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Chenthill Palanisamy <pchenthill@novell.com>
 */

#ifndef _E_CAL_BACKEND_LOADER_FACTORY_H_
#define _E_CAL_BACKEND_LOADER_FACTORY_H_

#include <glib-object.h>
#include "e-cal-backend-factory.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_LOADER_FACTORY        (e_cal_backend_loader_factory_get_type ())
#define E_CAL_BACKEND_LOADER_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CAL_BACKEND_LOADER_FACTORY, ECalBackendLoaderFactory))
#define E_CAL_BACKEND_LOADER_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_CAL_BACKEND_LOADER_FACTORY, ECalBackendLoaderFactoryClass))
#define E_IS_CAL_BACKEND_LOADER_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CAL_BACKEND_LOADER_FACTORY))
#define E_IS_CAL_BACKEND_LOADER_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CAL_BACKEND_LOADER_FACTORY))
#define E_CAL_BACKEND_LOADER_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_CAL_BACKEND_LOADER_FACTORY, ECalBackendLoaderFactoryClass))

typedef struct _ECalBackendLoaderFactoryPrivate ECalBackendLoaderFactoryPrivate;

/**
 * ECalBackendLoaderFactory:
 *
 * Since: 2.24
 **/
typedef struct {
	ECalBackendFactory	parent_object;
} ECalBackendLoaderFactory;

typedef struct {
	ECalBackendFactoryClass parent_class;

	GSList*	   (*get_protocol_list) (ECalBackendLoaderFactory *factory);
	ECalBackend*	   (*new_backend_with_protocol) (ECalBackendLoaderFactory *factory, ESource *source, const gchar *protocol);
} ECalBackendLoaderFactoryClass;

GType               e_cal_backend_loader_factory_get_type              (void);

G_END_DECLS

#endif /* _E_CAL_BACKEND_LOADER_FACTORY_H_ */
