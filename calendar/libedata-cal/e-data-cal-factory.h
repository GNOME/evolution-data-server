/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef E_DATA_CAL_FACTORY_H
#define E_DATA_CAL_FACTORY_H

#include <glib-object.h>

G_BEGIN_DECLS



#define E_TYPE_DATA_CAL_FACTORY            (e_data_cal_factory_get_type ())
#define E_DATA_CAL_FACTORY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_DATA_CAL_FACTORY, EDataCalFactory))
#define E_DATA_CAL_FACTORY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_DATA_CAL_FACTORY,  EDataCalFactoryClass))
#define E_IS_DATA_CAL_FACTORY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_DATA_CAL_FACTORY))
#define E_IS_DATA_CAL_FACTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_DATA_CAL_FACTORY))

typedef struct _EDataCalFactory EDataCalFactory;
typedef struct _EDataCalFactoryClass EDataCalFactoryClass;

typedef struct _EDataCalFactoryPrivate EDataCalFactoryPrivate;

struct _EDataCalFactory {
	GObject parent;
	EDataCalFactoryPrivate *priv;
};

struct _EDataCalFactoryClass {
	GObjectClass parent;
};

typedef enum {
	E_DATA_CAL_FACTORY_ERROR_GENERIC
} EDataCalFactoryError;

GQuark e_data_cal_factory_error_quark (void);
#define E_DATA_CAL_FACTORY_ERROR e_data_cal_factory_error_quark ()

GType       e_data_cal_factory_get_type        (void);

void        e_data_cal_factory_register_backend  (EDataCalFactory *factory,
						  ECalBackendFactory *backend_factory);

void	    e_data_cal_factory_register_backends    (EDataCalFactory    *factory);

gint         e_data_cal_factory_get_n_backends       (EDataCalFactory *factory);
void        e_data_cal_factory_dump_active_backends (EDataCalFactory *factory);
void        e_data_cal_factory_set_backend_mode (EDataCalFactory *factory, gint mode);

G_END_DECLS

#endif
