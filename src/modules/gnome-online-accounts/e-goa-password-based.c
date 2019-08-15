/*
 * e-goa-password-based.c
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

#include "evolution-data-server-config.h"

/* XXX Yeah, yeah... */
#define GOA_API_IS_SUBJECT_TO_CHANGE

#include <goa/goa.h>
#include <glib/gi18n-lib.h>

#include "e-goa-password-based.h"

struct _EGoaPasswordBasedPrivate {
	GoaClient *goa_client;
	GMutex lock;
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED (EGoaPasswordBased,
				e_goa_password_based,
				E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL,
				0,
				G_ADD_PRIVATE_DYNAMIC (EGoaPasswordBased))

static GoaClient *
e_goa_password_based_ref_goa_client_sync (EGoaPasswordBased *goa_password_based,
					  GCancellable *cancellable,
					  GError **error)
{
	GoaClient *goa_client;

	g_return_val_if_fail (E_IS_GOA_PASSWORD_BASED (goa_password_based), NULL);

	g_mutex_lock (&goa_password_based->priv->lock);

	if (goa_password_based->priv->goa_client) {
		GDBusObjectManager *object_manager;
		gchar *owner_name = NULL;

		object_manager = goa_client_get_object_manager (goa_password_based->priv->goa_client);
		if (object_manager)
			owner_name = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (object_manager));

		if (!owner_name)
			g_clear_object (&goa_password_based->priv->goa_client);

		g_free (owner_name);
	}

	if (!goa_password_based->priv->goa_client)
		goa_password_based->priv->goa_client = goa_client_new_sync (cancellable, error);

	if (goa_password_based->priv->goa_client)
		goa_client = g_object_ref (goa_password_based->priv->goa_client);
	else
		goa_client = NULL;

	g_mutex_unlock (&goa_password_based->priv->lock);

	return goa_client;
}

static ESource *
e_goa_password_based_ref_credentials_source (ESourceCredentialsProvider *provider,
					     ESource *source)
{
	ESource *adept, *cred_source = NULL;

	g_return_val_if_fail (E_IS_SOURCE_CREDENTIALS_PROVIDER (provider), NULL);
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	adept = g_object_ref (source);

	while (adept && !e_source_has_extension (adept, E_SOURCE_EXTENSION_GOA)) {
		ESource *parent;

		if (!e_source_get_parent (adept)) {
			break;
		}

		parent = e_source_credentials_provider_ref_source (provider, e_source_get_parent (adept));

		g_clear_object (&adept);
		adept = parent;
	}

	if (adept && e_source_has_extension (adept, E_SOURCE_EXTENSION_GOA)) {
		cred_source = g_object_ref (adept);
	}

	g_clear_object (&adept);

	if (!cred_source)
		cred_source = e_source_credentials_provider_ref_credentials_source (provider, source);

	return cred_source;
}

static GoaObject *
e_goa_password_based_ref_account (ESourceCredentialsProvider *provider,
				  ESource *source,
                                  GoaClient *goa_client)
{
	ESource *cred_source = NULL;
	GoaObject *match = NULL;
	GList *list, *link;
	gchar *account_id = NULL;
	ESourceGoa *extension = NULL;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_GOA)) {
		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_GOA);
	} else {
		cred_source = e_goa_password_based_ref_credentials_source (provider, source);
		if (cred_source && e_source_has_extension (cred_source, E_SOURCE_EXTENSION_GOA))
			extension = e_source_get_extension (cred_source, E_SOURCE_EXTENSION_GOA);
	}

	if (!extension) {
		g_clear_object (&cred_source);
		return NULL;
	}

	account_id = e_source_goa_dup_account_id (extension);

	g_clear_object (&cred_source);

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
	g_free (account_id);

	return match;
}

static gboolean
e_goa_password_based_can_process (ESourceCredentialsProviderImpl *provider_impl,
				  ESource *source)
{
	gboolean can_process;

	g_return_val_if_fail (E_IS_GOA_PASSWORD_BASED (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	can_process = e_source_has_extension (source, E_SOURCE_EXTENSION_GOA);
	if (!can_process) {
		ESource *cred_source;

		cred_source = e_goa_password_based_ref_credentials_source (
			e_source_credentials_provider_impl_get_provider (provider_impl),
			source);

		if (cred_source) {
			can_process = e_source_has_extension (cred_source, E_SOURCE_EXTENSION_GOA);
			g_clear_object (&cred_source);
		}
	}

	return can_process;
}

static gboolean
e_goa_password_based_can_store (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_GOA_PASSWORD_BASED (provider_impl), FALSE);

	return FALSE;
}

static gboolean
e_goa_password_based_can_prompt (ESourceCredentialsProviderImpl *provider_impl)
{
	g_return_val_if_fail (E_IS_GOA_PASSWORD_BASED (provider_impl), FALSE);

	return FALSE;
}

static gboolean
e_goa_password_based_lookup_sync (ESourceCredentialsProviderImpl *provider_impl,
				  ESource *source,
				  GCancellable *cancellable,
				  ENamedParameters **out_credentials,
				  GError **error)
{
	GoaClient *goa_client = NULL;
	GoaObject *goa_object = NULL;
	GoaAccount *goa_account = NULL;
	GoaPasswordBased *goa_password_based = NULL;
	gchar *password = NULL;
	gboolean use_imap_password;
	gboolean use_smtp_password;
	gboolean success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_GOA_PASSWORD_BASED (provider_impl), FALSE);
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
	g_return_val_if_fail (out_credentials, FALSE);

	goa_client = e_goa_password_based_ref_goa_client_sync (E_GOA_PASSWORD_BASED (provider_impl), cancellable, error);
	if (goa_client == NULL) {
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		goto exit;
	}

	goa_object = e_goa_password_based_ref_account (
		e_source_credentials_provider_impl_get_provider (provider_impl),
		source, goa_client);

	if (goa_object == NULL) {
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot find a corresponding account in "
			"the org.gnome.OnlineAccounts service from "
			"which to obtain a password for “%s”"),
			e_source_get_display_name (source));
		goto exit;
	}

	goa_account = goa_object_get_account (goa_object);
	goa_password_based = goa_object_get_password_based (goa_object);

	if (!goa_password_based) {
		/* Can be OAuth/2 based, thus return empty credentials. */
		*out_credentials = e_named_parameters_new ();
		success = TRUE;
		goto exit;
	}

	success = goa_account_call_ensure_credentials_sync (goa_account, NULL, cancellable, &local_error);
	if (!success) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
			g_clear_error (&local_error);
		} else if (local_error) {
			g_dbus_error_strip_remote_error (local_error);
			g_propagate_error (error, local_error);

			goto exit;
		}
	}

	use_imap_password = e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	use_smtp_password = e_source_has_extension (source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);

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
		success = FALSE;
		if (error && *error)
			g_dbus_error_strip_remote_error (*error);
		goto exit;
	}

	*out_credentials = e_named_parameters_new ();
	e_named_parameters_set (*out_credentials, E_SOURCE_CREDENTIAL_PASSWORD, password);

 exit:
	g_clear_object (&goa_client);
	g_clear_object (&goa_object);
	g_clear_object (&goa_account);
	g_clear_object (&goa_password_based);

	e_util_safe_free_string (password);

	if (!success)
		g_prefix_error (error, "%s", _("Failed to get password from GOA: "));

	return success;
}

static void
e_goa_password_based_dispose (GObject *object)
{
	EGoaPasswordBased *goa_password_based = E_GOA_PASSWORD_BASED (object);

	g_mutex_lock (&goa_password_based->priv->lock);

	g_clear_object (&goa_password_based->priv->goa_client);

	g_mutex_unlock (&goa_password_based->priv->lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_goa_password_based_parent_class)->dispose (object);
}

static void
e_goa_password_based_finalize (GObject *object)
{
	EGoaPasswordBased *goa_password_based = E_GOA_PASSWORD_BASED (object);

	g_clear_object (&goa_password_based->priv->goa_client);
	g_mutex_clear (&goa_password_based->priv->lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (e_goa_password_based_parent_class)->finalize (object);
}

static void
e_goa_password_based_class_init (EGoaPasswordBasedClass *class)
{
	ESourceCredentialsProviderImplClass *provider_impl_class;
	GObjectClass *object_class;

	provider_impl_class = E_SOURCE_CREDENTIALS_PROVIDER_IMPL_CLASS (class);
	provider_impl_class->can_process = e_goa_password_based_can_process;
	provider_impl_class->can_store = e_goa_password_based_can_store;
	provider_impl_class->can_prompt = e_goa_password_based_can_prompt;
	provider_impl_class->lookup_sync = e_goa_password_based_lookup_sync;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = e_goa_password_based_dispose;
	object_class->finalize = e_goa_password_based_finalize;
}

static void
e_goa_password_based_class_finalize (EGoaPasswordBasedClass *class)
{
}

static void
e_goa_password_based_init (EGoaPasswordBased *session)
{
	session->priv = e_goa_password_based_get_instance_private (session);

	g_mutex_init (&session->priv->lock);
}

void
e_goa_password_based_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE_EXTENDED declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_goa_password_based_register_type (type_module);
}

