/*
 * e-authentication-mediator.h
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

#ifndef E_AUTHENTICATION_MEDIATOR_H
#define E_AUTHENTICATION_MEDIATOR_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_AUTHENTICATION_MEDIATOR \
	(e_authentication_mediator_get_type ())
#define E_AUTHENTICATION_MEDIATOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_AUTHENTICATION_MEDIATOR, EAuthenticationMediator))
#define E_AUTHENTICATION_MEDIATOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_AUTHENTICATION_MEDIATOR, EAuthenticationMediatorClass))
#define E_IS_AUTHENTICATION_MEDIATOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_AUTHENTICATION_MEDIATOR))
#define E_IS_AUTHENTICATION_MEDIATOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_AUTHENTICATION_MEDIATOR))
#define E_AUTHENTICATION_MEDIATOR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_AUTHENTICATION_MEDIATOR, EAuthenticationMediatorClass))

G_BEGIN_DECLS

typedef struct _EAuthenticationMediator EAuthenticationMediator;
typedef struct _EAuthenticationMediatorClass EAuthenticationMediatorClass;
typedef struct _EAuthenticationMediatorPrivate EAuthenticationMediatorPrivate;

/**
 * EAuthenticationMediator:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _EAuthenticationMediator {
	/*< private >*/
	GObject parent;
	EAuthenticationMediatorPrivate *priv;
};

/**
 * EAuthenticationMediatorClass:
 *
 * Class structure for the #EAuthenticationMediator object
 *
 * Since: 3.6
 */
struct _EAuthenticationMediatorClass {
	/*< private >*/
	GObjectClass parent_class;
};

GType		e_authentication_mediator_get_type
					(void) G_GNUC_CONST;
ESourceAuthenticator *
		e_authentication_mediator_new
					(GDBusConnection *connection,
					 const gchar *object_path,
					 const gchar *sender,
					 GError **error);
GDBusConnection *
		e_authentication_mediator_get_connection
					(EAuthenticationMediator *mediator);
const gchar *	e_authentication_mediator_get_object_path
					(EAuthenticationMediator *mediator);
const gchar *	e_authentication_mediator_get_sender
					(EAuthenticationMediator *mediator);
gboolean	e_authentication_mediator_wait_for_client_sync
					(EAuthenticationMediator *mediator,
					 GCancellable *cancellable,
					 GError **error);
void		e_authentication_mediator_wait_for_client
					(EAuthenticationMediator *mediator,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
gboolean	e_authentication_mediator_wait_for_client_finish
					(EAuthenticationMediator *mediator,
					 GAsyncResult *result,
					 GError **error);
void		e_authentication_mediator_dismiss
					(EAuthenticationMediator *mediator);
void		e_authentication_mediator_server_error
					(EAuthenticationMediator *mediator,
					 const GError *error);

G_END_DECLS

#endif /* E_AUTHENTICATION_MEDIATOR_H */

