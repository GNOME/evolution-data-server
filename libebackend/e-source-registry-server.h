/*
 * e-source-registry-server.h
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

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_SOURCE_REGISTRY_SERVER_H
#define E_SOURCE_REGISTRY_SERVER_H

#include <libedataserver/libedataserver.h>

#include <libebackend/e-authentication-session.h>
#include <libebackend/e-backend-enums.h>
#include <libebackend/e-data-factory.h>
#include <libebackend/e-collection-backend.h>
#include <libebackend/e-collection-backend-factory.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_REGISTRY_SERVER \
	(e_source_registry_server_get_type ())
#define E_SOURCE_REGISTRY_SERVER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_REGISTRY_SERVER, ESourceRegistryServer))
#define E_SOURCE_REGISTRY_SERVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_REGISTRY_SERVER, ESourceRegistryServerClass))
#define E_IS_SOURCE_REGISTRY_SERVER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_REGISTRY_SERVER))
#define E_IS_SOURCE_REGISTRY_SERVER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_REGISTRY_SERVER))
#define E_SOURCE_REGISTRY_SERVER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_REGISTRY_SERVER, ESourceRegistryServerClass))

/**
 * E_SOURCE_REGISTRY_SERVER_OBJECT_PATH:
 *
 * D-Bus object path of the data source server.
 *
 * Since: 3.6
 **/
#define E_SOURCE_REGISTRY_SERVER_OBJECT_PATH \
	"/org/gnome/evolution/dataserver/SourceManager"

G_BEGIN_DECLS

typedef struct _ESourceRegistryServer ESourceRegistryServer;
typedef struct _ESourceRegistryServerClass ESourceRegistryServerClass;
typedef struct _ESourceRegistryServerPrivate ESourceRegistryServerPrivate;

/**
 * ESourceRegistryServer:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ESourceRegistryServer {
	EDataFactory parent;
	ESourceRegistryServerPrivate *priv;
};

struct _ESourceRegistryServerClass {
	EDataFactoryClass parent_class;

	/* Signals */
	void		(*load_error)		(ESourceRegistryServer *server,
						 GFile *file,
						 const GError *error);
	void		(*files_loaded)		(ESourceRegistryServer *server);
	void		(*source_added)		(ESourceRegistryServer *server,
						 ESource *source);
	void		(*source_removed)	(ESourceRegistryServer *server,
						 ESource *source);
};

GType		e_source_registry_server_get_type
						(void) G_GNUC_CONST;
EDBusServer *	e_source_registry_server_new	(void);
void		e_source_registry_server_add_source
						(ESourceRegistryServer *server,
						 ESource *source);
void		e_source_registry_server_remove_source
						(ESourceRegistryServer *server,
						 ESource *source);
gboolean	e_source_registry_server_load_all
						(ESourceRegistryServer *server,
						 GError **error);
gboolean	e_source_registry_server_load_directory
						(ESourceRegistryServer *server,
						 const gchar *path,
						 ESourcePermissionFlags flags,
						 GError **error);
ESource *	e_source_registry_server_load_file
						(ESourceRegistryServer *server,
						 GFile *file,
						 ESourcePermissionFlags flags,
						 GError **error);
void		e_source_registry_server_load_error
						(ESourceRegistryServer *server,
						 GFile *file,
						 const GError *error);
ESource *	e_source_registry_server_ref_source
						(ESourceRegistryServer *server,
						 const gchar *uid);
GList *		e_source_registry_server_list_sources
						(ESourceRegistryServer *server,
						 const gchar *extension_name);
ECollectionBackend *
		e_source_registry_server_ref_backend
						(ESourceRegistryServer *server,
						 ESource *source);
ECollectionBackendFactory *
		e_source_registry_server_ref_backend_factory
						(ESourceRegistryServer *server,
						 ESource *source);
gboolean	e_source_registry_server_authenticate_sync
						(ESourceRegistryServer *server,
						 EAuthenticationSession *session,
						 GCancellable *cancellable,
						 GError **error);
void		e_source_registry_server_authenticate
						(ESourceRegistryServer *server,
						 EAuthenticationSession *session,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_source_registry_server_authenticate_finish
						(ESourceRegistryServer *server,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_SOURCE_REGISTRY_SERVER_H */
