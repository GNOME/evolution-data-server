/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-cal-backend-factory.h
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
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifndef _E_CAL_BACKEND_FACTORY_H_
#define _E_CAL_BACKEND_FACTORY_H_

#include <glib-object.h>
#include "e-cal-backend.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_FACTORY        (e_cal_backend_factory_get_type ())
#define E_CAL_BACKEND_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_CAL_BACKEND_FACTORY, ECalBackendFactory))
#define E_CAL_BACKEND_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_CAL_BACKEND_FACTORY, ECalBackendFactoryClass))
#define E_IS_CAL_BACKEND_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_CAL_BACKEND_FACTORY))
#define E_IS_CAL_BACKEND_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_CAL_BACKEND_FACTORY))
#define E_CAL_BACKEND_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), E_TYPE_CAL_BACKEND_FACTORY, ECalBackendFactoryClass))

typedef struct _ECalBackendFactoryPrivate ECalBackendFactoryPrivate;

typedef struct {
	GObject            parent_object;
} ECalBackendFactory;

typedef struct {
	GObjectClass parent_class;

	icalcomponent_kind (*get_kind)     (ECalBackendFactory *factory);
	const gchar *        (*get_protocol) (ECalBackendFactory *factory);
	ECalBackend*       (*new_backend)  (ECalBackendFactory *factory, ESource *source);
} ECalBackendFactoryClass;

GType               e_cal_backend_factory_get_type              (void);

icalcomponent_kind  e_cal_backend_factory_get_kind              (ECalBackendFactory *factory);
const gchar *         e_cal_backend_factory_get_protocol          (ECalBackendFactory *factory);
ECalBackend*        e_cal_backend_factory_new_backend           (ECalBackendFactory *factory, ESource *source);

G_END_DECLS

#endif /* _E_CAL_BACKEND_FACTORY_H_ */
