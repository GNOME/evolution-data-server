/*
 * e-signon-session-password.c
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

#include "e-signon-session-password.h"

#include <config.h>
#include <glib/gi18n-lib.h>
#include <libsignon-glib/signon-glib.h>

#include "uoa-utils.h"

#define SIGNON_METHOD_PASSWORD    "password"
#define SIGNON_MECHANISM_PASSWORD "password"

#define E_SIGNON_SESSION_PASSWORD_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SIGNON_SESSION_PASSWORD, ESignonSessionPasswordPrivate))

typedef struct _AsyncContext AsyncContext;

struct _ESignonSessionPasswordPrivate {
	AgManager *ag_manager;
};

struct _AsyncContext {
	ESourceAuthenticator *authenticator;
	SignonAuthSession *signon_auth_session;
	EAuthenticationSessionResult session_result;
	AgAuthData *ag_auth_data;
	GCancellable *cancellable;
};

/* Forward Declarations */
static void	signon_session_password_msg
					(EAuthenticationSession *session,
					 const gchar *format,
					 ...) G_GNUC_PRINTF (2, 3);
static void	signon_session_password_process_cb
					(GObject *source_object,
					 GAsyncResult *result,
					 gpointer user_data);

G_DEFINE_DYNAMIC_TYPE (
	ESignonSessionPassword,
	e_signon_session_password,
	E_TYPE_AUTHENTICATION_SESSION)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->authenticator != NULL)
		g_object_unref (async_context->authenticator);

	if (async_context->signon_auth_session != NULL)
		g_object_unref (async_context->signon_auth_session);

	if (async_context->ag_auth_data != NULL)
		ag_auth_data_unref (async_context->ag_auth_data);

	if (async_context->cancellable != NULL)
		g_object_unref (async_context->cancellable);

	g_slice_free (AsyncContext, async_context);
}

static void
signon_session_password_msg (EAuthenticationSession *session,
                             const gchar *format,
                             ...)
{
	GString *buffer;
	const gchar *source_uid;
	va_list args;

	buffer = g_string_sized_new (256);

	source_uid = e_authentication_session_get_source_uid (session);
	g_string_append_printf (buffer, "AUTH (%s): ", source_uid);

	va_start (args, format);
	g_string_append_vprintf (buffer, format, args);
	va_end (args);

	e_source_registry_debug_print ("%s\n", buffer->str);

	g_string_free (buffer, TRUE);
}

static void
signon_session_password_state_changed_cb (SignonAuthSession *signon_auth_session,
                                          gint state,
                                          const gchar *message,
                                          EAuthenticationSession *session)
{
	signon_session_password_msg (session, "(signond) %s", message);
}

static AgAccountService *
signon_session_password_new_account_service (EAuthenticationSession *session,
                                             ESource *source,
                                             GError **error)
{
	ESignonSessionPasswordPrivate *priv;
	ESourceUoa *extension;
	AgAccountId account_id;
	AgAccount *ag_account = NULL;
	AgAccountService *ag_account_service;
	GList *list;
	gboolean has_uoa_extension;
	const gchar *extension_name;

	priv = E_SIGNON_SESSION_PASSWORD_GET_PRIVATE (session);

	/* XXX The ESource should be a collection source with an
	 *     [Ubuntu Online Accounts] extension.  Verify this. */
	extension_name = E_SOURCE_EXTENSION_UOA;
	has_uoa_extension = e_source_has_extension (source, extension_name);
	g_return_val_if_fail (has_uoa_extension, NULL);

	extension = e_source_get_extension (source, extension_name);
	account_id = e_source_uoa_get_account_id (extension);

	ag_account = ag_manager_load_account (
		priv->ag_manager, account_id, error);

	if (ag_account == NULL)
		return NULL;

	/* XXX We can't accurately determine the appropriate service
	 *     type from a collection source, but all services for an
	 *     account should be using the same authentication method
	 *     and mechanism so any service should work. */

	list = ag_account_list_services (ag_account);
	g_return_val_if_fail (list != NULL, NULL);

	ag_account_service = ag_account_service_new (ag_account, list->data);

	ag_service_list_free (list);

	g_object_unref (ag_account);

	return ag_account_service;
}

static void
signon_session_password_try_password_cb (GObject *source_object,
                                         GAsyncResult *result,
                                         gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	ESourceAuthenticationResult auth_result;
	GVariantBuilder builder;
	GVariant *session_data;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	auth_result = e_source_authenticator_try_password_finish (
		E_SOURCE_AUTHENTICATOR (source_object), result, &error);

	if (error != NULL) {
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete (simple);
		goto exit;
	}

	if (auth_result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		async_context->session_result =
			E_AUTHENTICATION_SESSION_SUCCESS;
		g_simple_async_result_complete (simple);
		goto exit;
	}

	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

	/* Force the signon service to prompt for a password by adding
	 * SIGNON_POLICY_REQUEST_PASSWORD to the session data dictionary. */
	g_variant_builder_add (
		&builder, "{sv}", SIGNON_SESSION_DATA_UI_POLICY,
		g_variant_new_int32 (SIGNON_POLICY_REQUEST_PASSWORD));

	/* This returns a floating reference. */
	session_data = ag_auth_data_get_login_parameters (
		async_context->ag_auth_data,
		g_variant_builder_end (&builder));

	signon_auth_session_process_async (
		async_context->signon_auth_session,
		session_data,
		SIGNON_MECHANISM_PASSWORD,
		async_context->cancellable,
		signon_session_password_process_cb,
		g_object_ref (simple));

exit:
	g_object_unref (simple);
}

static void
signon_session_password_process_cb (GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GVariant *session_data;
	GVariant *secret;
	GString *string = NULL;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	session_data = signon_auth_session_process_finish (
		SIGNON_AUTH_SESSION (source_object), result, &error);

	/* Sanity check. */
	g_return_if_fail (
		((session_data != NULL) && (error == NULL)) ||
		((session_data == NULL) && (error != NULL)));

	if (error != NULL) {
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete (simple);
		goto exit;
	}

	secret = g_variant_lookup_value (
		session_data,
		SIGNON_SESSION_DATA_SECRET,
		G_VARIANT_TYPE_STRING);

	g_variant_unref (session_data);

	if (secret == NULL) {
		g_simple_async_result_set_error (
			simple, SIGNON_ERROR,
			SIGNON_ERROR_MISSING_DATA,
			_("Signon service did not return a secret"));
		g_simple_async_result_complete (simple);
		goto exit;
	}

	/* XXX It occurs to me now a GVariant might have been a better
	 *     choice for the password parameter in ESourceAuthenticator. */
	string = g_string_new (g_variant_get_string (secret, NULL));

	e_source_authenticator_try_password (
		async_context->authenticator,
		string,
		async_context->cancellable,
		signon_session_password_try_password_cb,
		g_object_ref (simple));

	g_string_free (string, TRUE);
	g_variant_unref (secret);

exit:
	g_object_unref (simple);
}

static void
signon_session_password_dispose (GObject *object)
{
	ESignonSessionPasswordPrivate *priv;

	priv = E_SIGNON_SESSION_PASSWORD_GET_PRIVATE (object);

	g_clear_object (&priv->ag_manager);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_signon_session_password_parent_class)->
		dispose (object);
}

static EAuthenticationSessionResult
signon_session_password_execute_sync (EAuthenticationSession *session,
                                      GCancellable *cancellable,
                                      GError **error)
{
	EAuthenticationSessionResult auth_result;
	EAsyncClosure *async_closure;
	GAsyncResult *async_result;

	async_closure = e_async_closure_new ();

	e_authentication_session_execute (
		session, G_PRIORITY_DEFAULT, cancellable,
		e_async_closure_callback, async_closure);

	async_result = e_async_closure_wait (async_closure);

	auth_result = e_authentication_session_execute_finish (
		session, async_result, error);

	e_async_closure_free (async_closure);

	return auth_result;
}

static void
signon_session_password_execute (EAuthenticationSession *session,
                                 gint io_priority,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	ESourceAuthenticator *authenticator;
	ESourceRegistryServer *server;
	ESource *source;
	AgAccountService *ag_account_service;
	AgAuthData *ag_auth_data;
	SignonAuthSession *signon_auth_session;
	const gchar *source_uid;
	guint credentials_id;
	GError *error = NULL;

	signon_session_password_msg (session, "Initiated");

	authenticator = e_authentication_session_get_authenticator (session);

	async_context = g_slice_new0 (AsyncContext);
	async_context->authenticator = g_object_ref (authenticator);

	if (G_IS_CANCELLABLE (cancellable))
		async_context->cancellable = g_object_ref (cancellable);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		signon_session_password_execute);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	server = e_authentication_session_get_server (session);
	source_uid = e_authentication_session_get_source_uid (session);
	source = e_source_registry_server_ref_source (server, source_uid);

	if (source == NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR,
			G_IO_ERROR_NOT_FOUND,
			_("No such data source for UID '%s'"),
			source_uid);
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
		return;
	}

	ag_account_service =
		signon_session_password_new_account_service (
		session, source, &error);

	g_object_unref (source);

	/* Sanity check. */
	g_return_if_fail (
		((ag_account_service != NULL) && (error == NULL)) ||
		((ag_account_service == NULL) && (error != NULL)));

	if (error != NULL) {
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
		return;
	}

	ag_auth_data = ag_account_service_get_auth_data (ag_account_service);
	credentials_id = ag_auth_data_get_credentials_id (ag_auth_data);

	/* Hard-code the method and mechanism names.  If they disagree
	 * with AgAuthData then hopefully the signon process will fail
	 * with a suitable error message. */

	signon_auth_session = signon_auth_session_new (
		credentials_id, SIGNON_METHOD_PASSWORD, &error);

	/* Sanity check. */
	g_return_if_fail (
		((signon_auth_session != NULL) && (error == NULL)) ||
		((signon_auth_session == NULL) && (error != NULL)));

	if (signon_auth_session != NULL) {
		GVariant *session_data;

		g_signal_connect (
			signon_auth_session, "state-changed",
			G_CALLBACK (signon_session_password_state_changed_cb),
			session);

		/* Need to hold on to these in case of retries. */
		async_context->signon_auth_session = signon_auth_session;
		async_context->ag_auth_data = ag_auth_data_ref (ag_auth_data);

		/* This returns a floating reference. */
		session_data = ag_auth_data_get_login_parameters (
			async_context->ag_auth_data, NULL);

		signon_auth_session_process_async (
			async_context->signon_auth_session,
			session_data,
			SIGNON_MECHANISM_PASSWORD,
			async_context->cancellable,
			signon_session_password_process_cb,
			g_object_ref (simple));
	} else {
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete_in_idle (simple);
	}

	ag_auth_data_unref (ag_auth_data);
	g_object_unref (ag_account_service);

	g_object_unref (simple);
}

static EAuthenticationSessionResult
signon_session_password_execute_finish (EAuthenticationSession *session,
                                        GAsyncResult *result,
                                        GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	EAuthenticationSessionResult session_result;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		signon_session_password_execute),
		E_AUTHENTICATION_SESSION_DISMISSED);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		session_result = E_AUTHENTICATION_SESSION_ERROR;
	else
		session_result = async_context->session_result;

	switch (session_result) {
		case E_AUTHENTICATION_SESSION_ERROR:
			if (error != NULL && *error != NULL)
				signon_session_password_msg (
					session, "Complete (ERROR - %s)",
					(*error)->message);
			else
				signon_session_password_msg (
					session, "Complete (ERROR)");
			break;
		case E_AUTHENTICATION_SESSION_SUCCESS:
			signon_session_password_msg (
				session, "Complete (SUCCESS)");
			break;
		case E_AUTHENTICATION_SESSION_DISMISSED:
			signon_session_password_msg (
				session, "Complete (DISMISSED)");
			break;
		default:
			g_warn_if_reached ();
	}

	return session_result;
}

static void
e_signon_session_password_class_init (ESignonSessionPasswordClass *class)
{
	GObjectClass *object_class;
	EAuthenticationSessionClass *authentication_session_class;

	g_type_class_add_private (
		class, sizeof (ESignonSessionPasswordPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = signon_session_password_dispose;

	authentication_session_class =
		E_AUTHENTICATION_SESSION_CLASS (class);
	authentication_session_class->execute_sync =
		signon_session_password_execute_sync;
	authentication_session_class->execute =
		signon_session_password_execute;
	authentication_session_class->execute_finish =
		signon_session_password_execute_finish;
}

static void
e_signon_session_password_class_finalize (ESignonSessionPasswordClass *class)
{
}

static void
e_signon_session_password_init (ESignonSessionPassword *session)
{
	session->priv = E_SIGNON_SESSION_PASSWORD_GET_PRIVATE (session);

	session->priv->ag_manager = ag_manager_new ();
}

void
e_signon_session_password_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_signon_session_password_register_type (type_module);
}

