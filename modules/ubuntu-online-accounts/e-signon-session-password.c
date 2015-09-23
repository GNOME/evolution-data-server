/*
 * e-signon-session-password.c
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
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
	SignonAuthSession *signon_auth_session;
	EAuthenticationSessionResult session_result;
	AgAuthData *ag_auth_data;
	GCancellable *cancellable;
	GString *password;
};

G_DEFINE_DYNAMIC_TYPE (ESignonSessionPassword, e_signon_session_password, E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL)

static void
async_context_free (AsyncContext *async_context)
{
	g_clear_object (&async_context->signon_auth_session);
	g_clear_object (&async_context->cancellable);

	if (async_context->ag_auth_data != NULL)
		ag_auth_data_unref (async_context->ag_auth_data);

	if (async_context->password) {
		if (async_context->password->len)
			memset (async_context->password->str, 0, async_context->password->len);
		g_string_free (async_context->password, TRUE);
	}

	g_slice_free (AsyncContext, async_context);
}

static void
e_signon_session_password_msg (ESource *source,
                               const gchar *format,
			       ...)
{
	GString *buffer;
	const gchar *source_uid;
	va_list args;

	buffer = g_string_sized_new (256);

	source_uid = e_source_get_uid (source);
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
                                          ESource *source)
{
	e_signon_session_password_msg (source, "(signond) %s", message);
}

static ESource *
e_uoa_signon_session_password_ref_credentials_source (ESourceCredentialsProvider *provider,
						      ESource *source)
{
	ESource *adept, *cred_source = NULL;

	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER (provider), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	adept = g_object_ref (source);

	while (adept && !e_source_has_extension (adept, E_SOURCE_EXTENSION_UOA)) {
		ESource *parent;

		if (!e_source_get_parent (adept)) {
			break;
		}

		parent = e_source_credentials_provider_ref_source (provider, e_source_get_parent (adept));

		g_clear_object (&adept);
		adept = parent;
	}

	if (adept && e_source_has_extension (adept, E_SOURCE_EXTENSION_UOA)) {
		cred_source = g_object_ref (adept);
	}

	g_clear_object (&adept);

	if (!cred_source)
		cred_source = e_source_credentials_provider_ref_credentials_source (provider, source);

	return cred_source;
}

static AgAccountService *
signon_session_password_new_account_service (ESourceCredentialsProviderImpl *provider_impl,
                                             ESource *source,
                                             GError **error)
{
	ESignonSessionPasswordPrivate *priv;
	ESource *cred_source = NULL;
	ESourceUoa *extension = NULL;
	AgAccountId account_id;
	AgAccount *ag_account = NULL;
	AgAccountService *ag_account_service;
	GList *list;

	priv = E_SIGNON_SESSION_PASSWORD_GET_PRIVATE (provider_impl);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_UOA)) {
		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_UOA);
	} else {
		ESourceCredentialsProvider *provider;

		provider = e_source_credentials_provider_impl_get_provider (provider_impl);

		cred_source = e_uoa_signon_session_password_ref_credentials_source (provider, source);
		if (cred_source && e_source_has_extension (cred_source, E_SOURCE_EXTENSION_UOA))
			extension = e_source_get_extension (cred_source, E_SOURCE_EXTENSION_UOA);
	}

	if (!extension) {
		g_clear_object (&cred_source);
		return NULL;
	}

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

static gboolean
e_signon_session_password_can_process (ESourceCredentialsProviderImpl *provider_impl,
				       ESource *source)
{
	gboolean can_process;

	g_return_val_if_fail (E_IS_SIGNON_SESSION_PASSWORD (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	can_process = e_source_has_extension (source, E_SOURCE_EXTENSION_UOA);
	if (!can_process) {
		ESource *cred_source;

		cred_source = e_uoa_signon_session_password_ref_credentials_source (
			e_source_credentials_provider_impl_get_provider (provider_impl),
			source);

		if (cred_source) {
			can_process = e_source_has_extension (cred_source, E_SOURCE_EXTENSION_UOA);
			g_clear_object (&cred_source);
		}
	}

	return can_process;
}

static gboolean
e_signon_session_password_can_store (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_SIGNON_SESSION_PASSWORD (provider_impl), FALSE);

	return FALSE;
}

static gboolean
e_signon_session_password_can_prompt (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_SIGNON_SESSION_PASSWORD (provider_impl), FALSE);

	return FALSE;
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
		goto exit;
	}

	async_context->password = g_string_new (g_variant_get_string (secret, NULL));

	g_variant_unref (secret);

exit:
	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

static void
e_signon_session_password_get (ESourceCredentialsProviderImpl *provider_impl,
			       ESource *source,
			       gint io_priority,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	AgAccountService *ag_account_service;
	AgAuthData *ag_auth_data;
	SignonAuthSession *signon_auth_session;
	guint credentials_id;
	GError *error = NULL;

	e_signon_session_password_msg (source, "Initiated");

	async_context = g_slice_new0 (AsyncContext);

	if (G_IS_CANCELLABLE (cancellable))
		async_context->cancellable = g_object_ref (cancellable);

	simple = g_simple_async_result_new (
		G_OBJECT (provider_impl), callback, user_data,
		e_signon_session_password_get);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	ag_account_service = signon_session_password_new_account_service (provider_impl, source, &error);

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
			source);

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

static gboolean
e_signon_session_password_get_finish (ESourceCredentialsProviderImpl *provider_impl,
				      GAsyncResult *result,
				      GString *out_password,
				      GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	gboolean success;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (provider_impl),
		e_signon_session_password_get),
		FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error)) {
		success = FALSE;
	} else {
		success = async_context->password != NULL;
		if (success && out_password)
			g_string_assign (out_password, async_context->password->str);
	}

	return success;
}

static gboolean
e_signon_session_password_lookup_sync (ESourceCredentialsProviderImpl *provider_impl,
				       ESource *source,
				       GCancellable *cancellable,
				       ENamedParameters **out_credentials,
				       GError **error)
{
	EAsyncClosure *async_closure;
	GAsyncResult *async_result;
	gboolean success;
	GString *password;

	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (out_credentials != NULL, FALSE);

	async_closure = e_async_closure_new ();

	e_signon_session_password_get (provider_impl, source,
		G_PRIORITY_DEFAULT, cancellable,
		e_async_closure_callback, async_closure);

	async_result = e_async_closure_wait (async_closure);

	password = g_string_new ("");

	success = e_signon_session_password_get_finish (provider_impl, async_result, password, error);
	if (success) {
		*out_credentials = e_named_parameters_new ();
		e_named_parameters_set (*out_credentials, E_SOURCE_CREDENTIAL_PASSWORD, password->str);
	}

	if (password->str)
		memset (password->str, 0, password->len);
	g_string_free (password, TRUE);

	e_async_closure_free (async_closure);

	if (success) {
		e_signon_session_password_msg (source, "Complete (SUCCESS)");
	} else if (error && *error) {
		e_signon_session_password_msg (source, "Complete (ERROR - %s)", (*error)->message);
	} else {
		e_signon_session_password_msg (source, "Complete (ERROR)");
	}


	return success;
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

static void
e_signon_session_password_class_init (ESignonSessionPasswordClass *class)
{
	GObjectClass *object_class;
	ESourceCredentialsProviderImplClass *provider_impl_class;

	g_type_class_add_private (class, sizeof (ESignonSessionPasswordPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = signon_session_password_dispose;

	provider_impl_class = E_SOURCE_CREDENTIALS_PROVIDER_IMPL_CLASS (class);
	provider_impl_class->can_process = e_signon_session_password_can_process;
	provider_impl_class->can_store = e_signon_session_password_can_store;
	provider_impl_class->can_prompt = e_signon_session_password_can_prompt;
	provider_impl_class->lookup_sync = e_signon_session_password_lookup_sync;
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

