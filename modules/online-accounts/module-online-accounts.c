/*
 * module-online-accounts.c
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

/* XXX Yeah, yeah... */
#define GOA_API_IS_SUBJECT_TO_CHANGE

#include <config.h>
#include <goa/goa.h>
#include <gnome-keyring.h>
#include <libsoup/soup.h>

#include <libebackend/libebackend.h>

#include "goaewsclient.h"

/* Standard GObject macros */
#define E_TYPE_ONLINE_ACCOUNTS \
	(e_online_accounts_get_type ())
#define E_ONLINE_ACCOUNTS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_ONLINE_ACCOUNTS, EOnlineAccounts))

#define CAMEL_OAUTH_MECHANISM_NAME  "XOAUTH"

typedef struct _EOnlineAccounts EOnlineAccounts;
typedef struct _EOnlineAccountsClass EOnlineAccountsClass;

struct _EOnlineAccounts {
	EExtension parent;

	GoaClient *goa_client;
	GCancellable *create_client;

	/* GoaAccount ID -> ESource UID */
	GHashTable *goa_to_eds;
};

struct _EOnlineAccountsClass {
	EExtensionClass parent_class;
};

/* The keyring definintions are copied from e-authentication-session.c */

#define KEYRING_ITEM_ATTRIBUTE_NAME	"e-source-uid"
#define KEYRING_ITEM_DISPLAY_FORMAT	"Evolution Data Source %s"

#ifdef HAVE_GOA_PASSWORD_BASED
static GnomeKeyringPasswordSchema schema = {
	GNOME_KEYRING_ITEM_GENERIC_SECRET,
	{
		{ KEYRING_ITEM_ATTRIBUTE_NAME,
		  GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
		{ NULL, 0 }
	}
};
#endif /* HAVE_GOA_PASSWORD_BASED */

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_online_accounts_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EOnlineAccounts,
	e_online_accounts,
	E_TYPE_EXTENSION)

static const gchar *
online_accounts_get_backend_name (const gchar *goa_provider_type)
{
	const gchar *eds_backend_name = NULL;

	/* This is a mapping between GoaAccount provider types and
	 * ESourceCollection backend names.  It requires knowledge
	 * of other registry modules, possibly even from 3rd party
	 * packages.  No way around it. */

	if (g_strcmp0 (goa_provider_type, "exchange") == 0)
		eds_backend_name = "ews";

	if (g_strcmp0 (goa_provider_type, "google") == 0)
		eds_backend_name = "google";

	else if (g_strcmp0 (goa_provider_type, "yahoo") == 0)
		eds_backend_name = "yahoo";

	return eds_backend_name;
}

static ESourceRegistryServer *
online_accounts_get_server (EOnlineAccounts *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_SOURCE_REGISTRY_SERVER (extensible);
}

static gboolean
online_accounts_provider_type_to_backend_name (GBinding *binding,
                                               const GValue *source_value,
                                               GValue *target_value,
                                               gpointer unused)
{
	const gchar *provider_type;
	const gchar *backend_name;

	provider_type = g_value_get_string (source_value);
	backend_name = online_accounts_get_backend_name (provider_type);
	g_return_val_if_fail (backend_name != NULL, FALSE);
	g_value_set_string (target_value, backend_name);

	return TRUE;
}

static gboolean
online_accounts_object_is_non_null (GBinding *binding,
                                    const GValue *source_value,
                                    GValue *target_value,
                                    gpointer unused)
{
	gpointer v_object;

	v_object = g_value_get_object (source_value);
	g_value_set_boolean (target_value, v_object != NULL);

	return TRUE;
}

static ESource *
online_accounts_new_source (EOnlineAccounts *extension)
{
	ESourceRegistryServer *server;
	ESource *source;
	GFile *file;
	GError *error = NULL;

	/* This being a brand new data source, creating the instance
	 * should never fail but we'll check for errors just the same. */
	server = online_accounts_get_server (extension);
	file = e_server_side_source_new_user_file (NULL);
	source = e_server_side_source_new (server, file, &error);
	g_object_unref (file);

	if (error != NULL) {
		g_warn_if_fail (source == NULL);
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}

	return source;
}

#ifdef HAVE_GOA_PASSWORD_BASED
static void
replace_host (gchar **url,
              const gchar *host)
{
	SoupURI *uri;

	uri = soup_uri_new (*url);
	if (!uri)
		return;

	soup_uri_set_host (uri, host);

	g_free (*url);
	*url = soup_uri_to_string (uri, FALSE);

	soup_uri_free (uri);
}
#endif /* HAVE_GOA_PASSWORD_BASED */

static void
online_accounts_config_exchange (EOnlineAccounts *extension,
                                 ESource *source,
                                 GoaObject *goa_object)
{
#ifdef HAVE_GOA_PASSWORD_BASED
	GoaExchange *goa_exchange;
	ESourceExtension *source_extension;
	const gchar *extension_name;
	gchar *as_url = NULL;
	gchar *oab_url = NULL;
	gpointer class;
	GError *error = NULL;

	goa_exchange = goa_object_peek_exchange (goa_object);
	if (goa_exchange == NULL)
		return;

	/* This should force the ESourceCamelEws type to be registered.
	 * It will also tell us if Evolution-EWS is even installed. */
	class = g_type_class_ref (g_type_from_name ("EEwsBackend"));
	if (class != NULL) {
		g_type_class_unref (class);
	} else {
		g_critical (
			"%s: Could not locate EEwsBackendClass. "
			"Is Evolution-EWS installed?", G_STRFUNC);
		return;
	}

	/* XXX GNOME Online Accounts already runs autodiscover to test
	 *     the user-entered values but doesn't share the discovered
	 *     URLs.  It only provides us a host name and expects us to
	 *     re-run autodiscover for ourselves.
	 *
	 *     So I've copied a slab of code from GOA which was in turn
	 *     copied from Evolution-EWS which does the autodiscovery.
	 *
	 *     I've already complained to Debarshi Ray about the lack
	 *     of useful info in GOA's Exchange interface so hopefully
	 *     it will someday publish discovered URLs and then we can
	 *     remove this hack. */

	goa_ews_autodiscover_sync (
		goa_object, &as_url, &oab_url, NULL, &error);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
		return;
	}

	g_return_if_fail (as_url != NULL);
	g_return_if_fail (oab_url != NULL);

	/* XXX We don't have direct access to CamelEwsSettings from here
	 *     since it's defined in Evolution-EWS.  But we can find out
	 *     its extension name and set properties by name. */

	extension_name = e_source_camel_get_extension_name ("ews");
	source_extension = e_source_get_extension (source, extension_name);

	/* This will be NULL if Evolution-EWS is not installed. */
	if (source_extension != NULL) {
		GoaAccount *goa_account;
		CamelSettings *settings;
		gchar *host, *user, *email;

		goa_account = goa_object_peek_account (goa_object);
		host = goa_exchange_dup_host (goa_exchange);
		user = goa_account_dup_identity (goa_account);
		email = goa_account_dup_presentation_identity (goa_account);

		if (host && *host) {
			replace_host (&as_url, host);
			replace_host (&oab_url, host);
		}

		g_object_set (
			source_extension,
			"hosturl", as_url,
			"oaburl", oab_url,
			"email", email,
			NULL);

		settings = e_source_camel_get_settings (
			E_SOURCE_CAMEL (source_extension));

		g_object_set (
			settings,
			"host", host,
			"user", user,
			"email", email,
			NULL);

		g_free (host);
		g_free (user);
		g_free (email);
	} else {
		g_critical (
			"%s: Failed to create [%s] extension",
			G_STRFUNC, extension_name);
	}

	g_free (as_url);
	g_free (oab_url);
#endif /* HAVE_GOA_PASSWORD_BASED */
}

static void
online_accounts_config_oauth (EOnlineAccounts *extension,
                              ESource *source,
                              GoaObject *goa_object)
{
	ESourceExtension *source_extension;
	const gchar *extension_name;

	if (goa_object_peek_oauth_based (goa_object) == NULL)
		return;

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	source_extension = e_source_get_extension (source, extension_name);

	e_source_authentication_set_method (
		E_SOURCE_AUTHENTICATION (source_extension),
		CAMEL_OAUTH_MECHANISM_NAME);
}

static void
online_accounts_config_password (EOnlineAccounts *extension,
                                 ESource *source,
                                 GoaObject *goa_object)
{
#ifdef HAVE_GOA_PASSWORD_BASED
	GoaAccount *goa_account;
	GoaPasswordBased *goa_password_based;
	GnomeKeyringResult keyring_result;
	EAsyncClosure *closure;
	GAsyncResult *result;
	const gchar *uid;
	gchar *arg_id;
	gchar *display_name;
	gchar *password = NULL;
	GError *error = NULL;

	/* If the GNOME Online Account is password-based, we use its
	 * password to seed our own keyring entry for the collection
	 * source which avoids having to special-case authentication
	 * like we do for OAuth.  Plus, if the stored password is no
	 * good we'll prompt for a new one instead of just giving up. */

	goa_password_based = goa_object_get_password_based (goa_object);

	if (goa_password_based == NULL)
		return;

	closure = e_async_closure_new ();

	/* XXX The GOA documentation doesn't explain the string
	 *     argument in goa_password_based_get_password() so
	 *     we'll pass in the identity and hope for the best. */
	goa_account = goa_object_get_account (goa_object);
	arg_id = goa_account_dup_identity (goa_account);
	g_object_unref (goa_account);

	goa_password_based_call_get_password (
		goa_password_based, arg_id, NULL,
		e_async_closure_callback, closure);

	g_free (arg_id);

	result = e_async_closure_wait (closure);

	goa_password_based_call_get_password_finish (
		goa_password_based, &password, result, &error);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
		goto exit;
	}

	uid = e_source_get_uid (source);
	display_name = g_strdup_printf (KEYRING_ITEM_DISPLAY_FORMAT, uid);

	/* XXX Just call gnome-keyring synchronously.  I know it's
	 *     evil, but I want to know the password has been stored
	 *     before returning from this function.  We'll be moving
	 *     to libsecret soon anyway, which is more GIO-based, so
	 *     we could then reuse the EAsyncClosure here. */
	keyring_result = gnome_keyring_store_password_sync (
		&schema, GNOME_KEYRING_DEFAULT, display_name,
		password, KEYRING_ITEM_ATTRIBUTE_NAME, uid, NULL);

	g_free (display_name);

	/* If we fail to store the password, we'll just end up prompting
	 * for a password like normal.  Annoying, maybe, but not the end
	 * of the world.  Still leave a breadcrumb for debugging though. */
	if (keyring_result != GNOME_KEYRING_RESULT_OK) {
		const gchar *message;
		message = gnome_keyring_result_to_message (keyring_result);
		g_warning ("%s: %s", G_STRFUNC, message);
	}

exit:
	e_async_closure_free (closure);
	g_object_unref (goa_password_based);
#endif /* HAVE_GOA_PASSWORD_BASED */
}

static void
online_accounts_config_collection (EOnlineAccounts *extension,
                                   ESource *source,
                                   GoaObject *goa_object)
{
	GoaAccount *goa_account;
	ESourceExtension *source_extension;
	const gchar *extension_name;

	goa_account = goa_object_get_account (goa_object);

	g_object_bind_property (
		goa_account, "presentation-identity",
		source, "display-name",
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_GOA;
	source_extension = e_source_get_extension (source, extension_name);

	g_object_bind_property (
		goa_account, "id",
		source_extension, "account-id",
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	source_extension = e_source_get_extension (source, extension_name);

	g_object_bind_property_full (
		goa_account, "provider-type",
		source_extension, "backend-name",
		G_BINDING_SYNC_CREATE,
		online_accounts_provider_type_to_backend_name,
		NULL,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property (
		goa_account, "identity",
		source_extension, "identity",
		G_BINDING_SYNC_CREATE);

	g_object_bind_property_full (
		goa_object, "calendar",
		source_extension, "calendar-enabled",
		G_BINDING_SYNC_CREATE,
		online_accounts_object_is_non_null,
		NULL,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property_full (
		goa_object, "contacts",
		source_extension, "contacts-enabled",
		G_BINDING_SYNC_CREATE,
		online_accounts_object_is_non_null,
		NULL,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property_full (
		goa_object, "mail",
		source_extension, "mail-enabled",
		G_BINDING_SYNC_CREATE,
		online_accounts_object_is_non_null,
		NULL,
		NULL, (GDestroyNotify) NULL);

	g_object_unref (goa_account);

	/* Handle optional GOA interfaces. */
	online_accounts_config_exchange (extension, source, goa_object);
	online_accounts_config_password (extension, source, goa_object);

	/* The data source should not be removable by clients. */
	e_server_side_source_set_removable (
		E_SERVER_SIDE_SOURCE (source), FALSE);
}

static void
online_accounts_config_mail_account (EOnlineAccounts *extension,
                                     ESource *source,
                                     GoaObject *goa_object)
{
	EServerSideSource *server_side_source;

	online_accounts_config_oauth (extension, source, goa_object);

	/* XXX Need to defer the network security settings to the
	 *     provider-specific module since "imap-use-tls" tells
	 *     us neither the port number, nor whether to use IMAP
	 *     over SSL versus STARTTLS.  The module will know. */

	/* Clients may change the source by may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
online_accounts_config_mail_identity (EOnlineAccounts *extension,
                                      ESource *source,
                                      GoaObject *goa_object)
{
	GoaMail *goa_mail;
	ESourceExtension *source_extension;
	EServerSideSource *server_side_source;
	const gchar *extension_name;

	goa_mail = goa_object_get_mail (goa_object);
	g_return_if_fail (goa_mail != NULL);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	source_extension = e_source_get_extension (source, extension_name);

	g_object_bind_property (
		goa_mail, "email-address",
		source_extension, "address",
		G_BINDING_SYNC_CREATE);

	g_object_unref (goa_mail);

	/* Clients may change the source by may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
online_accounts_config_mail_transport (EOnlineAccounts *extension,
                                       ESource *source,
                                       GoaObject *goa_object)
{
	EServerSideSource *server_side_source;

	online_accounts_config_oauth (extension, source, goa_object);

	/* XXX Need to defer the network security settings to the
	 *     provider-specific module since "smtp-use-tls" tells
	 *     us neither the port number, nor whether to use SMTP
	 *     over SSL versus STARTTLS.  The module will know. */

	/* Clients may change the source by may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
online_accounts_create_collection (EOnlineAccounts *extension,
                                   EBackendFactory *backend_factory,
                                   GoaObject *goa_object)
{
	GoaAccount *goa_account;
	ESourceRegistryServer *server;
	ESource *collection_source;
	ESource *mail_account_source;
	ESource *mail_identity_source;
	ESource *mail_transport_source;
	const gchar *account_id;
	const gchar *parent_uid;

	server = online_accounts_get_server (extension);

	collection_source = online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (collection_source));

	mail_account_source = online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (mail_account_source));

	mail_identity_source = online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (mail_identity_source));

	mail_transport_source = online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (mail_transport_source));

	/* Configure parent/child relationships. */
	parent_uid = e_source_get_uid (collection_source);
	e_source_set_parent (mail_account_source, parent_uid);
	e_source_set_parent (mail_identity_source, parent_uid);
	e_source_set_parent (mail_transport_source, parent_uid);

	/* Give the factory first crack at mail configuration. */
	e_collection_backend_factory_prepare_mail (
		E_COLLECTION_BACKEND_FACTORY (backend_factory),
		mail_account_source,
		mail_identity_source,
		mail_transport_source);

	/* Now it's our turn. */
	online_accounts_config_collection (
		extension, collection_source, goa_object);
	online_accounts_config_mail_account (
		extension, mail_account_source, goa_object);
	online_accounts_config_mail_identity (
		extension, mail_identity_source, goa_object);
	online_accounts_config_mail_transport (
		extension, mail_transport_source, goa_object);

	/* Export the new source collection. */
	e_source_registry_server_add_source (server, collection_source);
	e_source_registry_server_add_source (server, mail_account_source);
	e_source_registry_server_add_source (server, mail_identity_source);
	e_source_registry_server_add_source (server, mail_transport_source);

	goa_account = goa_object_get_account (goa_object);
	account_id = goa_account_get_id (goa_account);

	g_hash_table_insert (
		extension->goa_to_eds,
		g_strdup (account_id),
		g_strdup (parent_uid));

	g_object_unref (goa_account);

	g_object_unref (collection_source);
	g_object_unref (mail_account_source);
	g_object_unref (mail_identity_source);
	g_object_unref (mail_transport_source);
}

static void
online_accounts_remove_collection (EOnlineAccounts *extension,
                                   ESource *source)
{
	GError *error = NULL;

	/* This removes the entire subtree rooted at source.
	 * Deletes the corresponding on-disk key files too. */
	e_source_remove_sync (source, NULL, &error);

	if (error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
	}
}

static void
online_accounts_account_added_cb (GoaClient *goa_client,
                                  GoaObject *goa_object,
                                  EOnlineAccounts *extension)
{
	GoaAccount *goa_account;
	ESourceRegistryServer *server;
	EBackendFactory *backend_factory = NULL;
	const gchar *provider_type;
	const gchar *backend_name;
	const gchar *account_id;
	const gchar *source_uid;

	server = online_accounts_get_server (extension);

	goa_account = goa_object_get_account (goa_object);
	provider_type = goa_account_get_provider_type (goa_account);
	backend_name = online_accounts_get_backend_name (provider_type);

	account_id = goa_account_get_id (goa_account);
	source_uid = g_hash_table_lookup (extension->goa_to_eds, account_id);

	if (source_uid == NULL && backend_name != NULL)
		backend_factory = e_data_factory_ref_backend_factory (
			E_DATA_FACTORY (server), backend_name);

	if (backend_factory != NULL) {
		online_accounts_create_collection (
			extension, backend_factory, goa_object);
		g_object_unref (backend_factory);
	}

	g_object_unref (goa_account);
}

static void
online_accounts_account_removed_cb (GoaClient *goa_client,
                                    GoaObject *goa_object,
                                    EOnlineAccounts *extension)
{
	ESource *source = NULL;
	ESourceRegistryServer *server;
	GoaAccount *goa_account;
	const gchar *account_id;
	const gchar *source_uid;

	server = online_accounts_get_server (extension);

	goa_account = goa_object_get_account (goa_object);

	account_id = goa_account_get_id (goa_account);
	source_uid = g_hash_table_lookup (extension->goa_to_eds, account_id);

	if (source_uid != NULL)
		source = e_source_registry_server_ref_source (
			server, source_uid);

	if (source != NULL) {
		online_accounts_remove_collection (extension, source);
		g_object_unref (source);
	}

	g_object_unref (goa_account);
}

static gint
online_accounts_compare_id (GoaObject *goa_object,
                            const gchar *target_id)
{
	GoaAccount *goa_account;
	const gchar *account_id;
	gint result;

	goa_account = goa_object_get_account (goa_object);
	account_id = goa_account_get_id (goa_account);
	result = g_strcmp0 (account_id, target_id);
	g_object_unref (goa_account);

	return result;
}

static void
online_accounts_populate_accounts_table (EOnlineAccounts *extension,
                                         GList *goa_objects)
{
	ESourceRegistryServer *server;
	GQueue trash = G_QUEUE_INIT;
	GList *list, *link;
	const gchar *extension_name;

	server = online_accounts_get_server (extension);

	extension_name = E_SOURCE_EXTENSION_GOA;
	list = e_source_registry_server_list_sources (server, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source;
		ESourceGoa *goa_ext;
		const gchar *account_id;
		const gchar *source_uid;
		GList *match;

		source = E_SOURCE (link->data);
		source_uid = e_source_get_uid (source);

		extension_name = E_SOURCE_EXTENSION_GOA;
		goa_ext = e_source_get_extension (source, extension_name);
		account_id = e_source_goa_get_account_id (goa_ext);

		if (account_id == NULL)
			continue;

		/* Verify the GOA account still exists. */
		match = g_list_find_custom (
			goa_objects, account_id,
			(GCompareFunc) online_accounts_compare_id);

		/* If a matching GoaObject was found, add its ID
		 * to our accounts hash table.  Otherwise remove
		 * the ESource after we finish looping. */
		if (match != NULL) {
			GoaObject *goa_object;

			g_hash_table_insert (
				extension->goa_to_eds,
				g_strdup (account_id),
				g_strdup (source_uid));

			goa_object = GOA_OBJECT (match->data);
			online_accounts_config_collection (
				extension, source, goa_object);
		} else {
			g_queue_push_tail (&trash, source);
		}
	}

	/* Empty the trash. */
	while (!g_queue_is_empty (&trash)) {
		ESource *source = g_queue_pop_head (&trash);
		online_accounts_remove_collection (extension, source);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
online_accounts_create_client_cb (GObject *source_object,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
	EOnlineAccounts *extension;
	GoaClient *goa_client;
	GList *list, *link;
	GError *error = NULL;

	/* If we get back a G_IO_ERROR_CANCELLED then it means the
	 * EOnlineAccounts is already finalized, so be careful not
	 * to touch it until after we have a valid GoaClient. */

	goa_client = goa_client_new_finish (result, &error);

	if (error != NULL) {
		g_warn_if_fail (goa_client == NULL);
		g_warning (
			"Unable to connect to the GNOME Online "
			"Accounts service: %s", error->message);
		g_error_free (error);
		return;
	}

	g_return_if_fail (GOA_IS_CLIENT (goa_client));

	/* Should be safe to dereference the EOnlineAccounts now. */

	extension = E_ONLINE_ACCOUNTS (user_data);
	extension->goa_client = goa_client;  /* takes ownership */

	/* Don't need the GCancellable anymore. */
	g_object_unref (extension->create_client);
	extension->create_client = NULL;

	list = goa_client_get_accounts (extension->goa_client);

	/* This populates a hash table of GOA ID -> ESource UID strings by
	 * searching through available data sources for ones with a "GNOME
	 * Online Accounts" extension.  If such an extension is found, but
	 * no corresponding GoaAccount (presumably meaning the GOA account
	 * was somehow deleted between E-D-S sessions) then the ESource in
	 * which the extension was found gets deleted. */
	online_accounts_populate_accounts_table (extension, list);

	for (link = list; link != NULL; link = g_list_next (link))
		online_accounts_account_added_cb (
			extension->goa_client,
			GOA_OBJECT (link->data),
			extension);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Listen for Online Account changes. */
	g_signal_connect (
		extension->goa_client, "account-added",
		G_CALLBACK (online_accounts_account_added_cb), extension);
	g_signal_connect (
		extension->goa_client, "account-removed",
		G_CALLBACK (online_accounts_account_removed_cb), extension);
}

static void
online_accounts_bus_acquired_cb (EDBusServer *server,
                                 GDBusConnection *connection,
                                 EOnlineAccounts *extension)
{
	/* Connect to the GNOME Online Accounts service. */

	/* Note we don't reference the extension.  If the
	 * extension gets destroyed before this completes
	 * we cancel the operation from dispose(). */
	goa_client_new (
		extension->create_client,
		online_accounts_create_client_cb,
		extension);
}

static void
online_accounts_dispose (GObject *object)
{
	EOnlineAccounts *extension;

	extension = E_ONLINE_ACCOUNTS (object);

	if (extension->goa_client != NULL) {
		g_signal_handlers_disconnect_matched (
			extension->goa_client,
			G_SIGNAL_MATCH_DATA,
			0, 0, NULL, NULL, object);
		g_object_unref (extension->goa_client);
		extension->goa_client = NULL;
	}

	/* This cancels goa_client_new() in case it still
	 * hasn't completed.  We're no longer interested. */
	if (extension->create_client != NULL) {
		g_cancellable_cancel (extension->create_client);
		g_object_unref (extension->create_client);
		extension->create_client = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_online_accounts_parent_class)->dispose (object);
}

static void
online_accounts_finalize (GObject *object)
{
	EOnlineAccounts *extension;

	extension = E_ONLINE_ACCOUNTS (object);

	g_hash_table_destroy (extension->goa_to_eds);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_online_accounts_parent_class)->finalize (object);
}

static void
online_accounts_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	/* Wait for the registry service to acquire its well-known
	 * bus name so we don't do anything destructive beforehand. */

	g_signal_connect (
		extensible, "bus-acquired",
		G_CALLBACK (online_accounts_bus_acquired_cb), extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_online_accounts_parent_class)->constructed (object);
}

static void
e_online_accounts_class_init (EOnlineAccountsClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = online_accounts_dispose;
	object_class->finalize = online_accounts_finalize;
	object_class->constructed = online_accounts_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SOURCE_REGISTRY_SERVER;
}

static void
e_online_accounts_class_finalize (EOnlineAccountsClass *class)
{
}

static void
e_online_accounts_init (EOnlineAccounts *extension)
{
	/* Used to cancel unfinished goa_client_new(). */
	extension->create_client = g_cancellable_new ();

	extension->goa_to_eds = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_free);
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_online_accounts_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

