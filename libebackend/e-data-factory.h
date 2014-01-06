/*
 * e-data-factory.h
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_DATA_FACTORY_H
#define E_DATA_FACTORY_H

#include <libebackend/e-dbus-server.h>
#include <libebackend/e-backend-factory.h>

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

	GType backend_factory_type;

	/* Signals */
	void		(*backend_created)	(EDataFactory *data_factory,
						 EBackend *backend);

	gpointer reserved[15];
};

GType		e_data_factory_get_type		(void) G_GNUC_CONST;
EBackend *	e_data_factory_ref_backend	(EDataFactory *data_factory,
						 const gchar *hash_key,
						 ESource *source);
EBackend *	e_data_factory_ref_initable_backend
						(EDataFactory *data_factory,
						 const gchar *hash_key,
						 ESource *source,
						 GCancellable *cancellable,
						 GError **error);
EBackendFactory *
		e_data_factory_ref_backend_factory
						(EDataFactory *data_factory,
						 const gchar *hash_key);

G_END_DECLS

#endif /* E_DATA_FACTORY_H */
