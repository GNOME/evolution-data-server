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

#include <bonobo/bonobo-object.h>
#include <libical/ical.h>
#include <libedata-cal/Evolution-DataServer-Calendar.h>
#include <libedata-cal/e-cal-backend-loader-factory.h>

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
	BonoboObject object;

	/* Private data */
	EDataCalFactoryPrivate *priv;
};

struct _EDataCalFactoryClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Calendar_CalFactory__epv epv;

	/* Notification signals */
	void (* last_calendar_gone) (EDataCalFactory *factory);
};

GType       e_data_cal_factory_get_type        (void);
EDataCalFactory *e_data_cal_factory_new             (void);

gboolean    e_data_cal_factory_register_storage (EDataCalFactory *factory, const gchar *iid);
void        e_data_cal_factory_register_backend  (EDataCalFactory *factory,
						  ECalBackendFactory *backend_factory);

void	    e_data_cal_factory_register_backends    (EDataCalFactory    *factory);

gint         e_data_cal_factory_get_n_backends       (EDataCalFactory *factory);
void        e_data_cal_factory_dump_active_backends (EDataCalFactory *factory);
void        e_data_cal_factory_set_backend_mode (EDataCalFactory *factory, gint mode);

G_END_DECLS

#endif
