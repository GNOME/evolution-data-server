/*
 * e-goa-password-based.c
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

#include "e-goa-password-based.h"

/* XXX Yeah, yeah... */
#define GOA_API_IS_SUBJECT_TO_CHANGE

#include <config.h>
#include <goa/goa.h>
#include <glib/gi18n-lib.h>

#define E_GOA_PASSWORD_BASED_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_GOA_PASSWORD_BASED, EGoaPasswordBasedPrivate))

struct _EGoaPasswordBasedPrivate {
	gint placeholder;
};

G_DEFINE_DYNAMIC_TYPE (
	EGoaPasswordBased,
	e_goa_password_based,
	E_TYPE_AUTHENTICATION_SESSION)

static GoaObject *
e_goa_password_based_ref_account (ESourceRegistryServer *server,
                                  ESource *source,
                                  GoaClient *goa_client)
{
	GoaObject *match = NULL;
	GList *list, *link;
	const gchar *extension_name;
	gchar *account_id = NULL;

	extension_name = E_SOURCE_EXTENSION_GOA;

	source = e_source_registry_server_find_extension (
		server, source, extension_name);

	if (source != NULL) {
		ESourceGoa *extension;

		extension = e_source_get_extension (source, extension_name);
		account_id = e_source_goa_dup_account_id (extension);

		g_object_unref (source);
	}

	if (account_id == NULL)
		return NULL;

	/* FIXME Use goa_client_lookup_by_id() once we require GOA 3.6. */
	list = goa_client_get_accounts (goa_client);

	for (link = list; link != NULL; link = g_list_next (link)) {
		GoaObject *goa_object;
		GoaAccount *goa_account;
		const gchar *candidate_id;

		goa_object = GOA_OBJECT (link->data);
		goa_account = goa_object_get_account (goa_object);
		candidate_id = goa_account_get_id (goa_account);

		if (g_strcmp0 (account_id, candidate_id) == 0)
			match = g_object_ref (goa_object);

		g_object_unref (goa_account);

		if (match != NULL)
			break;
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	return match;
}

static EAuthenticationSessionResult
e_goa_password_based_execute_sync (EAuthenticationSession *session,
                                   GCancellable *cancellable,
                                   GError **error)
{
	EAuthenticationSessionResult session_result;
	ESourceAuthenticationResult auth_result;
	ESourceAuthenticator *authenticator;
	ESourceRegistryServer *server;
	ESource *source = NULL;
	GoaClient *goa_client = NULL;
	GoaObject *goa_object = NULL;
	GoaAccount *goa_account = NULL;
	GoaPasswordBased *goa_password_based = NULL;
	GString *password_string;
	const gchar *extension_name;
	const gchar *source_uid;
	gchar *password = NULL;
	gboolean use_imap_password;
	gboolean use_smtp_password;
	gboolean success;

	goa_client = goa_client_new_sync (cancellable, error);
	if (goa_client == NULL) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		goto exit;
	}

	server = e_authentication_session_get_server (session);
	source_uid = e_authentication_session_get_source_uid (session);
	source = e_source_registry_server_ref_source (server, source_uid);

	if (source == NULL) {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("No such data source for UID '%s'"),
			source_uid);
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		goto exit;
	}

	goa_object = e_goa_password_based_ref_account (
		server, source, goa_client);

	if (goa_object == NULL) {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot find a corresponding account in "
			"the org.gnome.OnlineAccounts service from "
			"which to obtain a password for '%s'"),
			e_source_get_display_name (source));
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		goto exit;
	}

	goa_account = goa_object_get_account (goa_object);
	goa_password_based = goa_object_get_password_based (goa_object);

	/* XXX We should only be here if the account is password based. */
	g_return_val_if_fail (
		goa_password_based != NULL,
		E_AUTHENTICATION_SESSION_ERROR);

	success = goa_account_call_ensure_credentials_sync (
		goa_account, NULL, cancellable, error);
	if (!success) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		goto exit;
	}

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	use_imap_password = e_source_has_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	use_smtp_password = e_source_has_extension (source, extension_name);

	/* Use a suitable password ID for the ESource. */
	if (use_imap_password) {
		goa_password_based_call_get_password_sync (
			goa_password_based, "imap-password",
			&password, cancellable, error);
	} else if (use_smtp_password) {
		goa_password_based_call_get_password_sync (
			goa_password_based, "smtp-password",
			&password, cancellable, error);
	} else {
		/* Generic fallback - password ID is not used. */
		goa_password_based_call_get_password_sync (
			goa_password_based, "",
			&password, cancellable, error);
	}

	if (password == NULL) {
		session_result = E_AUTHENTICATION_SESSION_ERROR;
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		goto exit;
	}

	authenticator = e_authentication_session_get_authenticator (session);
	password_string = g_string_new (password);
	auth_result = e_source_authenticator_try_password_sync (
		authenticator, password_string, cancellable, error);
	g_string_free (password_string, TRUE);

	switch (auth_result) {
		case E_SOURCE_AUTHENTICATION_ERROR:
			session_result = E_AUTHENTICATION_SESSION_ERROR;
			break;

		case E_SOURCE_AUTHENTICATION_ACCEPTED:
			session_result = E_AUTHENTICATION_SESSION_SUCCESS;
			break;

		case E_SOURCE_AUTHENTICATION_REJECTED:
			/* FIXME Apparently applications are expected to post
			 *       a desktop-wide notification about the failed
			 *       authentication attempt. */
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_PERMISSION_DENIED,
				_("Invalid password for '%s'"),
				e_source_get_display_name (source));
			session_result = E_AUTHENTICATION_SESSION_ERROR;
			break;

		default:
			g_warn_if_reached ();
			session_result = E_AUTHENTICATION_SESSION_DISMISSED;
			break;
	}

exit:
	g_clear_object (&source);
	g_clear_object (&goa_client);
	g_clear_object (&goa_object);
	g_clear_object (&goa_account);
	g_clear_object (&goa_password_based);

	g_free (password);

	return session_result;
}

static void
e_goa_password_based_class_init (EGoaPasswordBasedClass *class)
{
	EAuthenticationSessionClass *authentication_session_class;

	g_type_class_add_private (class, sizeof (EGoaPasswordBasedPrivate));

	authentication_session_class =
		E_AUTHENTICATION_SESSION_CLASS (class);
	authentication_session_class->execute_sync =
		e_goa_password_based_execute_sync;
}

static void
e_goa_password_based_class_finalize (EGoaPasswordBasedClass *class)
{
}

static void
e_goa_password_based_init (EGoaPasswordBased *session)
{
	session->priv = E_GOA_PASSWORD_BASED_GET_PRIVATE (session);
}

void
e_goa_password_based_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_goa_password_based_register_type (type_module);
}

