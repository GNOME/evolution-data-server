/*
 * e-data-factory.h
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
 */

#ifndef E_DATA_FACTORY_H
#define E_DATA_FACTORY_H

#include <libebackend/e-backend.h>
#include <libebackend/e-dbus-server.h>

/* Standard GObject macros */
#define E_TYPE_DATA_FACTORY \
	(e_data_factory_get_type ())
#define E_DATA_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_FACTORY, EDataFactory))
#define E_DATA_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DATA_FACTORY, EDataFactoryClass))
#define E_IS_DATA_FACTORY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DATA_FACTORY))
#define E_IS_DATA_FACTORY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DATA_FACTORY))
#define E_DATA_FACTORY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DATA_FACTORY, EDataFactoryClass))

G_BEGIN_DECLS

typedef struct _EDataFactory EDataFactory;
typedef struct _EDataFactoryClass EDataFactoryClass;
typedef struct _EDataFactoryPrivate EDataFactoryPrivate;

/**
 * EDataFactory:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.4
 **/
struct _EDataFactory {
	EDBusServer parent;
	EDataFactoryPrivate *priv;
};

struct _EDataFactoryClass {
	EDBusServerClass parent_class;

	gpointer reserved[16];
};

GType		e_data_factory_get_type		(void) G_GNUC_CONST;
EBackend *	e_data_factory_get_backend	(EDataFactory *factory,
						 const gchar *hash_key,
						 ESource *source);
gboolean	e_data_factory_get_online	(EDataFactory *factory);
void		e_data_factory_set_online	(EDataFactory *factory,
						 gboolean online);

G_END_DECLS

#endif /* E_DATA_FACTORY_H */
