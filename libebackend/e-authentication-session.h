/*
 * e-authentication-session.h
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

#ifndef E_AUTHENTICATION_SESSION_H
#define E_AUTHENTICATION_SESSION_H

#include <gio/gio.h>

#include <libedataserver/libedataserver.h>

#include <libebackend/e-backend-enums.h>

/* Standard GObject macros */
#define E_TYPE_AUTHENTICATION_SESSION \
	(e_authentication_session_get_type ())
#define E_AUTHENTICATION_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_AUTHENTICATION_SESSION, EAuthenticationSession))
#define E_AUTHENTICATION_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_AUTHENTICATION_SESSION, EAuthenticationSessionClass))
#define E_IS_AUTHENTICATION_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_AUTHENTICATION_SESSION))
#define E_IS_AUTHENTICATION_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_AUTHENTICATION_SESSION))
#define E_AUTHENTICATION_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_AUTHENTICATION_SESSION, EAuthenticationSessionClass))

G_BEGIN_DECLS

struct _ESourceRegistryServer;

typedef struct _EAuthenticationSession EAuthenticationSession;
typedef struct _EAuthenticationSessionClass EAuthenticationSessionClass;
typedef struct _EAuthenticationSessionPrivate EAuthenticationSessionPrivate;

/**
 * EAuthenticationSession:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _EAuthenticationSession {
	/*< private >*/
	GObject parent;
	EAuthenticationSessionPrivate *priv;
};

/**
 * EAuthenticationSessionClass:
 * @execute_sync: Authenticate synchronously
 * @execute: Initiate authentication
 * @execute_finish: Complete authentication
 *
 * Class structure for the #EAuthenticationSession object
 *
 * Since: 3.6
 */
struct _EAuthenticationSessionClass {
	/*< private >*/
	GObjectClass parent_class;

	/*< public >*/

	/* Methods */
	EAuthenticationSessionResult
			(*execute_sync)	(EAuthenticationSession *session,
					 GCancellable *cancellable,
					 GError **error);
	void		(*execute)	(EAuthenticationSession *session,
					 gint io_priority,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
	EAuthenticationSessionResult
			(*execute_finish)
					(EAuthenticationSession *session,
					 GAsyncResult *result,
					 GError **error);

	/*< private >*/
	/* Reserved slots. */
	gpointer reserved[16];
};

GQuark		e_authentication_session_error_quark
					(void) G_GNUC_CONST;
GType		e_authentication_session_get_type
					(void) G_GNUC_CONST;
struct _ESourceRegistryServer *
		e_authentication_session_get_server
					(EAuthenticationSession *session);
ESourceAuthenticator *
		e_authentication_session_get_authenticator
					(EAuthenticationSession *session);
const gchar *	e_authentication_session_get_source_uid
					(EAuthenticationSession *session);
const gchar *	e_authentication_session_get_prompt_title
					(EAuthenticationSession *session);
gchar *		e_authentication_session_dup_prompt_title
					(EAuthenticationSession *session);
void		e_authentication_session_set_prompt_title
					(EAuthenticationSession *session,
					 const gchar *prompt_title);
const gchar *	e_authentication_session_get_prompt_message
					(EAuthenticationSession *session);
gchar *		e_authentication_session_dup_prompt_message
					(EAuthenticationSession *session);
void		e_authentication_session_set_prompt_message
					(EAuthenticationSession *session,
					 const gchar *prompt_message);
const gchar *	e_authentication_session_get_prompt_description
					(EAuthenticationSession *session);
gchar *		e_authentication_session_dup_prompt_description
					(EAuthenticationSession *session);
void		e_authentication_session_set_prompt_description
					(EAuthenticationSession *session,
					 const gchar *prompt_description);
EAuthenticationSessionResult
		e_authentication_session_execute_sync
					(EAuthenticationSession *session,
					 GCancellable *cancellable,
					 GError **error);
void		e_authentication_session_execute
					(EAuthenticationSession *session,
					 gint io_priority,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
EAuthenticationSessionResult
		e_authentication_session_execute_finish
					(EAuthenticationSession *session,
					 GAsyncResult *result,
					 GError **error);

#ifndef EDS_DISABLE_DEPRECATED
/**
 * E_AUTHENTICATION_SESSION_KEYRING_ERROR:
 *
 * Error domain for password storage and retrieval.
 *
 * No longer used.
 *
 * Since: 3.6
 *
 * Deprecated: 3.8: The #SECRET_ERROR domain is now used instead.
 **/
#define E_AUTHENTICATION_SESSION_KEYRING_ERROR \
	(e_authentication_session_error_quark ())

EAuthenticationSession *
		e_authentication_session_new
					(struct _ESourceRegistryServer *server,
					 ESourceAuthenticator *authenticator,
					 const gchar *source_uid);
gboolean	e_authentication_session_store_password_sync
					(EAuthenticationSession *session,
					 const gchar *password,
					 gboolean permanently,
					 GCancellable *cancellable,
					 GError **error);
void		e_authentication_session_store_password
					(EAuthenticationSession *session,
					 const gchar *password,
					 gboolean permanently,
					 gint io_priority,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
gboolean	e_authentication_session_store_password_finish
					(EAuthenticationSession *session,
					 GAsyncResult *result,
					 GError **error);
gboolean	e_authentication_session_lookup_password_sync
					(EAuthenticationSession *session,
					 GCancellable *cancellable,
					 gchar **password,
					 GError **error);
void		e_authentication_session_lookup_password
					(EAuthenticationSession *session,
					 gint io_priority,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
gboolean	e_authentication_session_lookup_password_finish
					(EAuthenticationSession *session,
					 GAsyncResult *result,
					 gchar **password,
					 GError **error);
gboolean	e_authentication_session_delete_password_sync
					(EAuthenticationSession *session,
					 GCancellable *cancellable,
					 GError **error);
void		e_authentication_session_delete_password
					(EAuthenticationSession *session,
					 gint io_priority,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
gboolean	e_authentication_session_delete_password_finish
					(EAuthenticationSession *session,
					 GAsyncResult *result,
					 GError **error);
#endif /* EDS_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* E_AUTHENTICATION_SESSION_H */

