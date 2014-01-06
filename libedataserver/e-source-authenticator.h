/*
 * e-source-authenticator.h
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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_AUTHENTICATOR_H
#define E_SOURCE_AUTHENTICATOR_H

#include <libedataserver/e-source.h>
#include <libedataserver/e-source-enums.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_AUTHENTICATOR \
	(e_source_authenticator_get_type ())
#define E_SOURCE_AUTHENTICATOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_AUTHENTICATOR, ESourceAuthenticator))
#define E_IS_SOURCE_AUTHENTICATOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_AUTHENTICATOR))
#define E_SOURCE_AUTHENTICATOR_GET_INTERFACE(obj) \
	(G_TYPE_INSTANCE_GET_INTERFACE \
	((obj), E_TYPE_SOURCE_AUTHENTICATOR, ESourceAuthenticatorInterface))

G_BEGIN_DECLS

/**
 * ESourceAuthenticator:
 *
 * Since: 3.6
 **/
typedef struct _ESourceAuthenticator ESourceAuthenticator;
typedef struct _ESourceAuthenticatorInterface ESourceAuthenticatorInterface;

/**
 * ESourceAuthenticatorInterface:
 *
 * Since: 3.6
 **/
struct _ESourceAuthenticatorInterface {
	GTypeInterface parent_interface;

	void		(*get_prompt_strings)	(ESourceAuthenticator *auth,
						 ESource *source,
						 gchar **prompt_title,
						 gchar **prompt_message,
						 gchar **prompt_description);

	gboolean	(*get_without_password)	(ESourceAuthenticator *auth);

	/* Synchronous I/O Methods */
	ESourceAuthenticationResult
			(*try_password_sync)	(ESourceAuthenticator *auth,
						 const GString *password,
						 GCancellable *cancellable,
						 GError **error);

	/* Asynchronous I/O Methods (all have defaults) */
	void		(*try_password)		(ESourceAuthenticator *auth,
						 const GString *password,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	ESourceAuthenticationResult
			(*try_password_finish)	(ESourceAuthenticator *auth,
						 GAsyncResult *result,
						 GError **error);
};

GType		e_source_authenticator_get_type	(void) G_GNUC_CONST;
void		e_source_authenticator_get_prompt_strings
						(ESourceAuthenticator *auth,
						 ESource *source,
						 gchar **prompt_title,
						 gchar **prompt_message,
						 gchar **prompt_description);
gboolean	e_source_authenticator_get_without_password
						(ESourceAuthenticator *auth);
ESourceAuthenticationResult
		e_source_authenticator_try_password_sync
						(ESourceAuthenticator *auth,
						 const GString *password,
						 GCancellable *cancellable,
						 GError **error);
void		e_source_authenticator_try_password
						(ESourceAuthenticator *auth,
						 const GString *password,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
ESourceAuthenticationResult
		e_source_authenticator_try_password_finish
						(ESourceAuthenticator *auth,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* E_SOURCE_AUTHENTICATOR_H */
