/*
 * e-backend.h
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

#ifndef E_BACKEND_H
#define E_BACKEND_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_BACKEND \
	(e_backend_get_type ())
#define E_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BACKEND, EBackend))
#define E_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BACKEND, EBackendClass))
#define E_IS_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BACKEND))
#define E_IS_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BACKEND))
#define E_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_BACKEND, EBackendClass))

G_BEGIN_DECLS

/* forward declaration */
struct _EUserPrompter;

typedef struct _EBackend EBackend;
typedef struct _EBackendClass EBackendClass;
typedef struct _EBackendPrivate EBackendPrivate;

/**
 * EBackend:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.4
 **/
struct _EBackend {
	/*< private >*/
	GObject parent;
	EBackendPrivate *priv;
};

/**
 * EBackendClass:
 * @authenticate_sync: Authenticate synchronously
 * @authenticate: Initiate authentication
 * @authenticate_finish: Complete authentication
 * @get_destination_address: Fetch the destination address
 *
 * Base class structure for the #EBackend class
 *
 * Since: 3.4
 **/
struct _EBackendClass {
	/*< private >*/
	GObjectClass parent_class;

	/*< public >*/
	/* Methods */
	gboolean	(*authenticate_sync)	(EBackend *backend,
						 ESourceAuthenticator *auth,
						 GCancellable *cancellable,
						 GError **error);
	void		(*authenticate)		(EBackend *backend,
						 ESourceAuthenticator *auth,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*authenticate_finish)	(EBackend *backend,
						 GAsyncResult *result,
						 GError **error);

	gboolean	(*get_destination_address)
						(EBackend *backend,
						 gchar **host,
						 guint16 *port);

	/*< private >*/
	gpointer reserved[12];
};

GType		e_backend_get_type		(void) G_GNUC_CONST;
gboolean	e_backend_get_online		(EBackend *backend);
void		e_backend_set_online		(EBackend *backend,
						 gboolean online);
ESource *	e_backend_get_source		(EBackend *backend);
GSocketConnectable *
		e_backend_ref_connectable	(EBackend *backend);
void		e_backend_set_connectable	(EBackend *backend,
						 GSocketConnectable *connectable);
GMainContext *	e_backend_ref_main_context	(EBackend *backend);
gboolean	e_backend_authenticate_sync	(EBackend *backend,
						 ESourceAuthenticator *auth,
						 GCancellable *cancellable,
						 GError **error);
void		e_backend_authenticate		(EBackend *backend,
						 ESourceAuthenticator *auth,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_backend_authenticate_finish	(EBackend *backend,
						 GAsyncResult *result,
						 GError **error);
struct _EUserPrompter *
		e_backend_get_user_prompter	(EBackend *backend);
ETrustPromptResponse
		e_backend_trust_prompt_sync	(EBackend *backend,
						 const ENamedParameters *parameters,
						 GCancellable *cancellable,
						 GError **error);
void		e_backend_trust_prompt		(EBackend *backend,
						 const ENamedParameters *parameters,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
ETrustPromptResponse
		e_backend_trust_prompt_finish	(EBackend *backend,
						 GAsyncResult *result,
						 GError **error);

gboolean	e_backend_get_destination_address
						(EBackend *backend,
						 gchar **host,
						 guint16 *port);
gboolean	e_backend_is_destination_reachable
						(EBackend *backend,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* E_BACKEND_H */
