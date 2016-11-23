/*
 * module-ubuntu-online-accounts.c
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

#include <glib/gi18n-lib.h>
#include <libsignon-glib/signon-glib.h>
#include <libaccounts-glib/accounts-glib.h>

#include "uoa-utils.h"

/* Standard GObject macros */
#define E_TYPE_UBUNTU_ONLINE_ACCOUNTS \
	(e_ubuntu_online_accounts_get_type ())
#define E_UBUNTU_ONLINE_ACCOUNTS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_UBUNTU_ONLINE_ACCOUNTS, EUbuntuOnlineAccounts))

#define CAMEL_OAUTH2_MECHANISM_NAME "XOAUTH2"

typedef struct _EUbuntuOnlineAccounts EUbuntuOnlineAccounts;
typedef struct _EUbuntuOnlineAccountsClass EUbuntuOnlineAccountsClass;
typedef struct _AsyncContext AsyncContext;

struct _EUbuntuOnlineAccounts {
	EExtension parent;

	AgManager *ag_manager;

	/* AgAccountId -> ESource UID */
	GHashTable *uoa_to_eds;
};

struct _EUbuntuOnlineAccountsClass {
	EExtensionClass parent_class;
};

struct _AsyncContext {
	EUbuntuOnlineAccounts *extension;
	EBackendFactory *backend_factory;
	gchar *access_token;
	gint expires_in;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_ubuntu_online_accounts_get_type (void);
static void e_ubuntu_online_accounts_oauth2_support_init
					(EOAuth2SupportInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EUbuntuOnlineAccounts,
	e_ubuntu_online_accounts,
	E_TYPE_EXTENSION,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_OAUTH2_SUPPORT,
		e_ubuntu_online_accounts_oauth2_support_init))

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->extension != NULL)
		g_object_unref (async_context->extension);

	if (async_context->backend_factory != NULL)
		g_object_unref (async_context->backend_factory);

	g_free (async_context->access_token);

	g_slice_free (AsyncContext, async_context);
}

static const gchar *
ubuntu_online_accounts_get_backend_name (const gchar *uoa_provider_name)
{
	const gchar *eds_backend_name = NULL;

	/* This is a mapping between AgAccount provider names and
	 * ESourceCollection backend names.  It requires knowledge
	 * of other registry modules, possibly even from 3rd party
	 * packages.
	 *
	 * FIXME Put the EDS backend name in the .service config
	 *       files so we're not hard-coding the providers we
	 *       support.  This isn't GNOME Online Accounts. */

	if (g_strcmp0 (uoa_provider_name, "google") == 0)
		eds_backend_name = "google";

	if (g_strcmp0 (uoa_provider_name, "windows-live") == 0)
		eds_backend_name = "outlook";

	if (g_strcmp0 (uoa_provider_name, "yahoo") == 0)
		eds_backend_name = "yahoo";

	return eds_backend_name;
}

static ESourceRegistryServer *
ubuntu_online_accounts_get_server (EUbuntuOnlineAccounts *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_SOURCE_REGISTRY_SERVER (extensible);
}

static gboolean
ubuntu_online_accounts_provider_name_to_backend_name (GBinding *binding,
                                                      const GValue *source_value,
                                                      GValue *target_value,
                                                      gpointer unused)
{
	const gchar *provider_name;
	const gchar *backend_name;

	provider_name = g_value_get_string (source_value);
	backend_name = ubuntu_online_accounts_get_backend_name (provider_name);
	g_return_val_if_fail (backend_name != NULL, FALSE);
	g_value_set_string (target_value, backend_name);

	return TRUE;
}

static AgAccountService *
ubuntu_online_accounts_ref_account_service (EUbuntuOnlineAccounts *extension,
                                            ESource *source)
{
	GHashTable *account_services;
	ESourceRegistryServer *server;
	AgAccountService *ag_account_service = NULL;
	const gchar *extension_name;
	const gchar *service_type;

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_COLLECTION)) {
		/* Asking for credentials on the main (collection) source, which
		   doesn't belong to any particular service, thus try to pick any
		   enabled service, expecting the same password/token being
		   used for all other services. */
		service_type = NULL;
		account_services = g_object_get_data (G_OBJECT (source), "ag-account-services");
		g_warn_if_fail (account_services != NULL);
		if (account_services) {
			AgAccountService *ag_service;

			ag_service = g_hash_table_lookup (account_services, E_AG_SERVICE_TYPE_CALENDAR);
			if (ag_service && ag_account_service_get_enabled (ag_service))
				service_type = E_AG_SERVICE_TYPE_CALENDAR;

			if (!service_type) {
				ag_service = g_hash_table_lookup (account_services, E_AG_SERVICE_TYPE_CONTACTS);
				if (ag_service && ag_account_service_get_enabled (ag_service))
					service_type = E_AG_SERVICE_TYPE_CONTACTS;
			}

			if (!service_type) {
				ag_service = g_hash_table_lookup (account_services, E_AG_SERVICE_TYPE_MAIL);
				if (ag_service && ag_account_service_get_enabled (ag_service))
					service_type = E_AG_SERVICE_TYPE_MAIL;
			}

			if (!service_type)
				return NULL;
		}
	} else {
		service_type = e_source_get_ag_service_type (source);
	}

	g_return_val_if_fail (service_type != NULL, NULL);

	extension_name = E_SOURCE_EXTENSION_UOA;
	server = ubuntu_online_accounts_get_server (extension);

	source = e_source_registry_server_find_extension (
		server, source, extension_name);

	if (source != NULL) {
		account_services = g_object_get_data (
			G_OBJECT (source), "ag-account-services");
		g_warn_if_fail (account_services != NULL);

		if (account_services != NULL) {
			ag_account_service = g_hash_table_lookup (
				account_services, service_type);
			if (ag_account_service != NULL)
				g_object_ref (ag_account_service);
		}

		g_object_unref (source);
	}

	return ag_account_service;
}

static gboolean
ubuntu_online_accounts_supports_oauth2 (AgAccountService *ag_account_service)
{
	AgAuthData *ag_auth_data;
	gboolean supports_oauth2 = FALSE;
	const gchar *method;

	ag_auth_data = ag_account_service_get_auth_data (ag_account_service);
	method = ag_auth_data_get_method (ag_auth_data);
	supports_oauth2 = (g_strcmp0 (method, "oauth2") == 0);
	ag_auth_data_unref (ag_auth_data);

	return supports_oauth2;
}

static ESource *
ubuntu_online_accounts_new_source (EUbuntuOnlineAccounts *extension)
{
	ESourceRegistryServer *server;
	ESource *source;
	GFile *file;
	GError *local_error = NULL;

	/* This being a brand new data source, creating the instance
	 * should never fail but we'll check for errors just the same. */
	server = ubuntu_online_accounts_get_server (extension);
	file = e_server_side_source_new_user_file (NULL);
	source = e_server_side_source_new (server, file, &local_error);
	g_object_unref (file);

	if (local_error != NULL) {
		g_warn_if_fail (source == NULL);
		g_warning ("%s: %s", G_STRFUNC, local_error->message);
		g_error_free (local_error);
	}

	return source;
}

static GHashTable *
ubuntu_online_accounts_new_account_services (EUbuntuOnlineAccounts *extension,
                                             AgAccount *ag_account)
{
	GHashTable *account_services;
	GList *list, *link;

	account_services = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	/* Populate the hash table with AgAccountService instances by
	 * service type.  There should only be one AgService per type.
	 *
	 * XXX We really should not have to create AgAccountService
	 *     instances ourselves.  The AgAccount itself should own
	 *     them and provide functions for listing them.  Instead
	 *     it only provides functions for listing its AgServices,
	 *     which is decidedly less useful. */
	list = ag_account_list_services (ag_account);
	for (link = list; link != NULL; link = g_list_next (link)) {
		AgService *ag_service = link->data;
		const gchar *service_type;

		service_type = ag_service_get_service_type (ag_service);

		g_hash_table_insert (
			account_services,
			g_strdup (service_type),
			ag_account_service_new (ag_account, ag_service));
	}
	ag_service_list_free (list);

	return account_services;
}

static void
ubuntu_online_accounts_config_oauth2 (EUbuntuOnlineAccounts *extension,
                                      ESource *source,
                                      GHashTable *account_services)
{
	AgAccountService *ag_account_service;
	ESourceExtension *source_extension;
	const gchar *extension_name;

	ag_account_service = g_hash_table_lookup (
		account_services, E_AG_SERVICE_TYPE_MAIL);
	if (ag_account_service == NULL)
		return;

	if (!ubuntu_online_accounts_supports_oauth2 (ag_account_service))
		return;

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	source_extension = e_source_get_extension (source, extension_name);

	e_source_authentication_set_method (
		E_SOURCE_AUTHENTICATION (source_extension),
		CAMEL_OAUTH2_MECHANISM_NAME);
}

static void
ubuntu_online_accounts_config_collection (EUbuntuOnlineAccounts *extension,
                                          ESource *source,
                                          AgAccount *ag_account,
                                          GHashTable *account_services,
                                          const gchar *user_identity)
{
	AgAccountService *ag_account_service;
	ESourceExtension *source_extension;
	gboolean supports_oauth2 = FALSE;
	const gchar *extension_name;

	e_binding_bind_property (
		ag_account, "display-name",
		source, "display-name",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		ag_account, "enabled",
		source, "enabled",
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_UOA;
	source_extension = e_source_get_extension (source, extension_name);

	e_binding_bind_property (
		ag_account, "id",
		source_extension, "account-id",
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	source_extension = e_source_get_extension (source, extension_name);

	e_binding_bind_property_full (
		ag_account, "provider",
		source_extension, "backend-name",
		G_BINDING_SYNC_CREATE,
		ubuntu_online_accounts_provider_name_to_backend_name,
		NULL,
		NULL, (GDestroyNotify) NULL);

	if (user_identity != NULL)
		e_source_collection_set_identity (
			E_SOURCE_COLLECTION (source_extension),
			user_identity);

	ag_account_service = g_hash_table_lookup (
		account_services, E_AG_SERVICE_TYPE_MAIL);
	if (ag_account_service != NULL) {
		e_binding_bind_property (
			ag_account_service , "enabled",
			source_extension, "mail-enabled",
			G_BINDING_SYNC_CREATE);
		supports_oauth2 |=
			ubuntu_online_accounts_supports_oauth2 (
			ag_account_service);
	}

	ag_account_service = g_hash_table_lookup (
		account_services, E_AG_SERVICE_TYPE_CALENDAR);
	if (ag_account_service != NULL) {
		e_binding_bind_property (
			ag_account_service, "enabled",
			source_extension, "calendar-enabled",
			G_BINDING_SYNC_CREATE);
		supports_oauth2 |=
			ubuntu_online_accounts_supports_oauth2 (
			ag_account_service);
	}

	ag_account_service = g_hash_table_lookup (
		account_services, E_AG_SERVICE_TYPE_CONTACTS);
	if (ag_account_service != NULL) {
		e_binding_bind_property (
			ag_account_service, "enabled",
			source_extension, "contacts-enabled",
			G_BINDING_SYNC_CREATE);
		supports_oauth2 |=
			ubuntu_online_accounts_supports_oauth2 (
			ag_account_service);
	}

	/* Stash the AgAccountService hash table in the ESource
	 * to keep the property bindings alive.  The hash table
	 * will be destroyed along with the ESource. */
	g_object_set_data_full (
		G_OBJECT (source),
		"ag-account-services",
		g_hash_table_ref (account_services),
		(GDestroyNotify) g_hash_table_unref);

	e_server_side_source_set_writable (E_SERVER_SIDE_SOURCE (source), TRUE);

	/* The data source should not be removable by clients. */
	e_server_side_source_set_removable (E_SERVER_SIDE_SOURCE (source), FALSE);

	if (supports_oauth2) {
		/* This module provides OAuth 2.0 support to the collection.
		 * Note, children of the collection source will automatically
		 * inherit our EOAuth2Support through the property binding in
		 * collection_backend_child_added(). */
		e_server_side_source_set_oauth2_support (
			E_SERVER_SIDE_SOURCE (source),
			E_OAUTH2_SUPPORT (extension));
	}
}

static void
ubuntu_online_accounts_config_mail_account (EUbuntuOnlineAccounts *extension,
                                            ESource *source,
                                            GHashTable *account_services,
                                            const gchar *imap_user_name)
{
	EServerSideSource *server_side_source;

	ubuntu_online_accounts_config_oauth2 (
		extension, source, account_services);

	if (imap_user_name != NULL) {
		ESourceAuthentication *source_extension;

		source_extension = e_source_get_extension (
			source, E_SOURCE_EXTENSION_AUTHENTICATION);
		e_source_authentication_set_user (
			source_extension, imap_user_name);
	}

	/* Clients may change the source but may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
ubuntu_online_accounts_config_mail_identity (EUbuntuOnlineAccounts *extension,
                                             ESource *source,
                                             GHashTable *account_services,
                                             const gchar *email_address)
{
	EServerSideSource *server_side_source;
	ESourceMailSubmission *mail_submission;
	ESourceMailComposition *mail_composition;
	gchar *tmp;

	if (email_address != NULL) {
		ESourceMailIdentity *source_extension;

		source_extension = e_source_get_extension (
			source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
		e_source_mail_identity_set_address (
			source_extension, email_address);
	}

	/* Set default Sent folder to the On This Computer/Sent */
	mail_submission = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_SUBMISSION);
	tmp = e_source_mail_submission_dup_sent_folder (mail_submission);
	if (!tmp || !*tmp)
		e_source_mail_submission_set_sent_folder (mail_submission, "folder://local/Sent");
	g_free (tmp);

	/* Set default Drafts folder to the On This Computer/Drafts */
	mail_composition = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_COMPOSITION);
	tmp = e_source_mail_composition_dup_drafts_folder (mail_composition);
	if (!tmp || !*tmp)
		e_source_mail_composition_set_drafts_folder (mail_composition, "folder://local/Drafts");
	g_free (tmp);

	/* Clients may change the source but may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
ubuntu_online_accounts_config_mail_transport (EUbuntuOnlineAccounts *extension,
                                              ESource *source,
                                              GHashTable *account_services,
                                              const gchar *smtp_user_name)
{
	EServerSideSource *server_side_source;

	ubuntu_online_accounts_config_oauth2 (
		extension, source, account_services);

	if (smtp_user_name != NULL) {
		ESourceAuthentication *source_extension;

		source_extension = e_source_get_extension (
			source, E_SOURCE_EXTENSION_AUTHENTICATION);
		e_source_authentication_set_user (
			source_extension, smtp_user_name);
	}

	/* Clients may change the source but may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
ubuntu_online_accounts_config_sources (EUbuntuOnlineAccounts *extension,
                                       ESource *source,
                                       AgAccount *ag_account)
{
	ESourceRegistryServer *server;
	ECollectionBackend *backend;
	GHashTable *account_services;
	GList *list, *link;

	account_services = ubuntu_online_accounts_new_account_services (
		extension, ag_account);

	ubuntu_online_accounts_config_collection (
		extension, source, ag_account, account_services, NULL);

	server = ubuntu_online_accounts_get_server (extension);
	backend = e_source_registry_server_ref_backend (server, source);
	g_return_if_fail (backend != NULL);

	list = e_collection_backend_list_mail_sources (backend);

	for (link = list; link != NULL; link = g_list_next (link)) {
		const gchar *extension_name;

		source = E_SOURCE (link->data);

		extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
		if (e_source_has_extension (source, extension_name))
			ubuntu_online_accounts_config_mail_account (
				extension, source, account_services, NULL);

		extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
		if (e_source_has_extension (source, extension_name))
			ubuntu_online_accounts_config_mail_identity (
				extension, source, account_services, NULL);

		extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
		if (e_source_has_extension (source, extension_name))
			ubuntu_online_accounts_config_mail_transport (
				extension, source, account_services, NULL);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	g_object_unref (backend);

	g_hash_table_unref (account_services);
}

static void
ubuntu_online_accounts_create_collection (EUbuntuOnlineAccounts *extension,
                                          EBackendFactory *backend_factory,
                                          AgAccount *ag_account,
                                          const gchar *user_identity,
                                          const gchar *email_address,
                                          const gchar *imap_user_name,
                                          const gchar *smtp_user_name)
{
	ESourceRegistryServer *server;
	ESource *collection_source;
	ESource *mail_account_source;
	ESource *mail_identity_source;
	ESource *mail_transport_source;
	GHashTable *account_services;
	const gchar *parent_uid;

	server = ubuntu_online_accounts_get_server (extension);

	collection_source = ubuntu_online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (collection_source));

	mail_account_source = ubuntu_online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (mail_account_source));

	mail_identity_source = ubuntu_online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (mail_identity_source));

	mail_transport_source = ubuntu_online_accounts_new_source (extension);
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
	account_services = ubuntu_online_accounts_new_account_services (
		extension, ag_account);
	ubuntu_online_accounts_config_collection (
		extension, collection_source, ag_account,
		account_services, user_identity);
	ubuntu_online_accounts_config_mail_account (
		extension, mail_account_source,
		account_services, imap_user_name);
	ubuntu_online_accounts_config_mail_identity (
		extension, mail_identity_source,
		account_services, email_address);
	ubuntu_online_accounts_config_mail_transport (
		extension, mail_transport_source,
		account_services, smtp_user_name);
	g_hash_table_unref (account_services);

	/* Export the new source collection. */
	e_source_registry_server_add_source (server, collection_source);
	e_source_registry_server_add_source (server, mail_account_source);
	e_source_registry_server_add_source (server, mail_identity_source);
	e_source_registry_server_add_source (server, mail_transport_source);

	g_hash_table_insert (
		extension->uoa_to_eds,
		GUINT_TO_POINTER (ag_account->id),
		g_strdup (parent_uid));

	g_object_unref (collection_source);
	g_object_unref (mail_account_source);
	g_object_unref (mail_identity_source);
	g_object_unref (mail_transport_source);
}

static void
ubuntu_online_accounts_got_userinfo_cb (GObject *source_object,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
	AgAccount *ag_account;
	AsyncContext *async_context = user_data;
	gchar *user_identity = NULL;
	gchar *email_address = NULL;
	gchar *imap_user_name = NULL;
	gchar *smtp_user_name = NULL;
	GError *local_error = NULL;

	ag_account = AG_ACCOUNT (source_object);

	e_ag_account_collect_userinfo_finish (
		ag_account, result,
		&user_identity,
		&email_address,
		&imap_user_name,
		&smtp_user_name,
		&local_error);

	if (local_error == NULL) {
		ubuntu_online_accounts_create_collection (
			async_context->extension,
			async_context->backend_factory,
			ag_account,
			user_identity,
			email_address,
			imap_user_name,
			smtp_user_name);
	} else {
		g_warning (
			"%s: Failed to create ESource "
			"collection for AgAccount '%s': %s",
			G_STRFUNC,
			ag_account_get_display_name (ag_account),
			local_error->message);
		g_error_free (local_error);
	}

	g_free (user_identity);
	g_free (email_address);
	g_free (imap_user_name);
	g_free (smtp_user_name);

	async_context_free (async_context);
}

static void
ubuntu_online_accounts_collect_userinfo (EUbuntuOnlineAccounts *extension,
                                         EBackendFactory *backend_factory,
                                         AgAccount *ag_account)
{
	AsyncContext *async_context;

	/* Before we create a collection we need to collect user info from
	 * the online service.  GNOME Online Accounts does this for us, but
	 * no such luck with libaccounts-glib or libsignon-glib. */

	async_context = g_slice_new0 (AsyncContext);
	async_context->extension = g_object_ref (extension);
	async_context->backend_factory = g_object_ref (backend_factory);

	e_ag_account_collect_userinfo (
		ag_account, NULL,
		ubuntu_online_accounts_got_userinfo_cb,
		async_context);
}

static void
ubuntu_online_accounts_remove_collection (EUbuntuOnlineAccounts *extension,
                                          ESource *source)
{
	GError *local_error = NULL;

	/* This removes the entire subtree rooted at source.
	 * Deletes the corresponding on-disk key files too. */
	e_source_remove_sync (source, NULL, &local_error);

	if (local_error != NULL) {
		g_warning ("%s: %s", G_STRFUNC, local_error->message);
		g_error_free (local_error);
	}
}

static void
ubuntu_online_accounts_account_created_cb (AgManager *ag_manager,
                                           AgAccountId ag_account_id,
                                           EUbuntuOnlineAccounts *extension)
{
	AgAccount *ag_account;
	ESourceRegistryServer *server;
	EBackendFactory *backend_factory = NULL;
	const gchar *provider_name;
	const gchar *backend_name;
	const gchar *source_uid;

	server = ubuntu_online_accounts_get_server (extension);

	ag_account = ag_manager_get_account (ag_manager, ag_account_id);
	g_return_if_fail (ag_account != NULL);

	provider_name = ag_account_get_provider_name (ag_account);
	backend_name = ubuntu_online_accounts_get_backend_name (provider_name);

	source_uid = g_hash_table_lookup (
		extension->uoa_to_eds,
		GUINT_TO_POINTER (ag_account_id));

	if (source_uid == NULL && backend_name != NULL)
		backend_factory = e_data_factory_ref_backend_factory (
			E_DATA_FACTORY (server), backend_name, E_SOURCE_EXTENSION_COLLECTION);

	if (backend_factory != NULL) {
		ubuntu_online_accounts_collect_userinfo (
			extension, backend_factory, ag_account);
		g_object_unref (backend_factory);
	}

	g_object_unref (ag_account);
}

static void
ubuntu_online_accounts_account_deleted_cb (AgManager *ag_manager,
                                           AgAccountId ag_account_id,
                                           EUbuntuOnlineAccounts *extension)
{
	ESource *source = NULL;
	ESourceRegistryServer *server;
	const gchar *source_uid;

	server = ubuntu_online_accounts_get_server (extension);

	source_uid = g_hash_table_lookup (
		extension->uoa_to_eds,
		GUINT_TO_POINTER (ag_account_id));

	if (source_uid != NULL)
		source = e_source_registry_server_ref_source (
			server, source_uid);

	if (source != NULL) {
		ubuntu_online_accounts_remove_collection (extension, source);
		g_object_unref (source);
	}
}

static void
ubuntu_online_accounts_populate_accounts_table (EUbuntuOnlineAccounts *extension,
                                                GList *ag_account_ids)
{
	ESourceRegistryServer *server;
	GQueue trash = G_QUEUE_INIT;
	GList *list, *link;
	const gchar *extension_name;

	server = ubuntu_online_accounts_get_server (extension);

	extension_name = E_SOURCE_EXTENSION_UOA;
	list = e_source_registry_server_list_sources (server, extension_name);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESource *source;
		ESourceUoa *uoa_ext;
		AgAccount *ag_account = NULL;
		AgAccountId ag_account_id;
		const gchar *source_uid;
		GList *match;

		source = E_SOURCE (link->data);
		source_uid = e_source_get_uid (source);

		extension_name = E_SOURCE_EXTENSION_UOA;
		uoa_ext = e_source_get_extension (source, extension_name);
		ag_account_id = e_source_uoa_get_account_id (uoa_ext);

		if (ag_account_id == 0)
			continue;

		if (g_hash_table_lookup (extension->uoa_to_eds, GUINT_TO_POINTER (ag_account_id))) {
			/* There are more ESource-s referencing the same UOA account;
			   delete the later. */
			g_queue_push_tail (&trash, source);
			continue;
		}

		/* Verify the UOA account still exists. */
		match = g_list_find (
			ag_account_ids,
			GUINT_TO_POINTER (ag_account_id));
		if (match != NULL)
			ag_account = ag_manager_get_account (
				extension->ag_manager, ag_account_id);

		/* If a matching AgAccountId was found, add it
		 * to our accounts hash table.  Otherwise remove
		 * the ESource after we finish looping. */
		if (ag_account != NULL) {
			g_hash_table_insert (
				extension->uoa_to_eds,
				GUINT_TO_POINTER (ag_account_id),
				g_strdup (source_uid));

			ubuntu_online_accounts_config_sources (
				extension, source, ag_account);
		} else {
			g_queue_push_tail (&trash, source);
		}
	}

	/* Empty the trash. */
	while (!g_queue_is_empty (&trash)) {
		ESource *source = g_queue_pop_head (&trash);
		ubuntu_online_accounts_remove_collection (extension, source);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
ubuntu_online_accounts_bus_acquired_cb (EDBusServer *server,
                                        GDBusConnection *connection,
                                        EUbuntuOnlineAccounts *extension)
{
	GList *list, *link;

	extension->ag_manager = ag_manager_new ();

	list = ag_manager_list (extension->ag_manager);

	/* This populates a hash table of UOA ID -> ESource UID strings by
	 * searching through available data sources for ones with a "Ubuntu
	 * Online Accounts" extension.  If such an extension is found, but
	 * no corresponding AgAccount (presumably meaning the UOA account
	 * was somehow deleted between E-D-S sessions) then the ESource in
	 * which the extension was found gets deleted. */
	ubuntu_online_accounts_populate_accounts_table (extension, list);

	for (link = list; link != NULL; link = g_list_next (link))
		ubuntu_online_accounts_account_created_cb (
			extension->ag_manager,
			GPOINTER_TO_UINT (link->data),
			extension);

	ag_manager_list_free (list);

	/* Listen for Online Account changes. */
	g_signal_connect (
		extension->ag_manager, "account-created",
		G_CALLBACK (ubuntu_online_accounts_account_created_cb),
		extension);
	g_signal_connect (
		extension->ag_manager, "account-deleted",
		G_CALLBACK (ubuntu_online_accounts_account_deleted_cb),
		extension);
}

static void
ubuntu_online_accounts_dispose (GObject *object)
{
	EUbuntuOnlineAccounts *extension;

	extension = E_UBUNTU_ONLINE_ACCOUNTS (object);

	if (extension->ag_manager != NULL) {
		g_signal_handlers_disconnect_matched (
			extension->ag_manager,
			G_SIGNAL_MATCH_DATA,
			0, 0, NULL, NULL, object);
		g_object_unref (extension->ag_manager);
		extension->ag_manager = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_ubuntu_online_accounts_parent_class)->
		dispose (object);
}

static void
ubuntu_online_accounts_finalize (GObject *object)
{
	EUbuntuOnlineAccounts *extension;

	extension = E_UBUNTU_ONLINE_ACCOUNTS (object);

	g_hash_table_destroy (extension->uoa_to_eds);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_ubuntu_online_accounts_parent_class)->
		finalize (object);
}

static void
ubuntu_online_accounts_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	/* Wait for the registry service to acquire its well-known
	 * bus name so we don't do anything destructive beforehand.
	 * Run last so that all the sources get loaded first. */

	g_signal_connect_after (
		extensible, "bus-acquired",
		G_CALLBACK (ubuntu_online_accounts_bus_acquired_cb),
		extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_ubuntu_online_accounts_parent_class)->constructed (object);
}

static gboolean
ubuntu_online_accounts_get_access_token_sync (EOAuth2Support *support,
                                              ESource *source,
                                              GCancellable *cancellable,
                                              gchar **out_access_token,
                                              gint *out_expires_in,
                                              GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = e_async_closure_new ();

	e_oauth2_support_get_access_token (
		support, source, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_oauth2_support_get_access_token_finish (
		support, result, out_access_token, out_expires_in, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for ubuntu_online_accounts_get_access_token() */
static void
ubuntu_online_accounts_session_process_cb (GObject *source_object,
                                           GAsyncResult *result,
                                           gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	GVariant *session_data;
	GError *local_error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	session_data = signon_auth_session_process_finish (
		SIGNON_AUTH_SESSION (source_object), result, &local_error);

	/* Sanity check. */
	g_return_if_fail (
		((session_data != NULL) && (local_error == NULL)) ||
		((session_data == NULL) && (local_error != NULL)));

	if (session_data != NULL) {
		g_variant_lookup (
			session_data, "AccessToken", "s",
			&async_context->access_token);

		g_variant_lookup (
			session_data, "ExpiresIn", "i",
			&async_context->expires_in);

		g_warn_if_fail (async_context->access_token != NULL);
		g_variant_unref (session_data);
	}

	if (local_error != NULL)
		g_simple_async_result_take_error (simple, local_error);

	g_simple_async_result_complete (simple);

	g_object_unref (simple);
}

static void
ubuntu_online_accounts_get_access_token (EOAuth2Support *support,
                                         ESource *source,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	SignonAuthSession *session;
	AgAccountService *ag_account_service;
	AgAuthData *ag_auth_data;
	GError *local_error = NULL;

	async_context = g_slice_new0 (AsyncContext);

	simple = g_simple_async_result_new (
		G_OBJECT (support), callback, user_data,
		ubuntu_online_accounts_get_access_token);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	ag_account_service = ubuntu_online_accounts_ref_account_service (
		E_UBUNTU_ONLINE_ACCOUNTS (support), source);

	if (ag_account_service == NULL) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot find a corresponding account "
			"service in the accounts database from "
			"which to obtain an access token for “%s”"),
			e_source_get_display_name (source));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
		return;
	}

	/* XXX This should never happen.  But because libaccounts-glib
	 *     splits authentication method by service-type instead of
	 *     by provider, and because we broadcast OAuth 2.0 support
	 *     across the entire collection (spanning multiple service
	 *     types), it's conceivable that not all service-types for
	 *     a provider use OAuth 2.0, and an ESource for one of the
	 *     ones that DOESN'T could mistakenly request the token. */
	if (!ubuntu_online_accounts_supports_oauth2 (ag_account_service)) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			_("Data source “%s” does not "
			"support OAuth 2.0 authentication"),
			e_source_get_display_name (source));
		g_simple_async_result_complete_in_idle (simple);
		g_object_unref (simple);
		return;
	}

	ag_auth_data = ag_account_service_get_auth_data (ag_account_service);

	session = signon_auth_session_new (
		ag_auth_data_get_credentials_id (ag_auth_data),
		ag_auth_data_get_method (ag_auth_data), &local_error);

	/* Sanity check. */
	g_return_if_fail (
		((session != NULL) && (local_error == NULL)) ||
		((session == NULL) && (local_error != NULL)));

	if (session != NULL) {
		signon_auth_session_process_async (
			session,
			ag_auth_data_get_login_parameters (ag_auth_data, NULL),
			ag_auth_data_get_mechanism (ag_auth_data),
			cancellable,
			ubuntu_online_accounts_session_process_cb,
			g_object_ref (simple));
		g_object_unref (session);
	} else {
		g_simple_async_result_take_error (simple, local_error);
		g_simple_async_result_complete_in_idle (simple);
	}

	ag_auth_data_unref (ag_auth_data);

	g_object_unref (ag_account_service);
	g_object_unref (simple);
}

static gboolean
ubuntu_online_accounts_get_access_token_finish (EOAuth2Support *support,
                                                GAsyncResult *result,
                                                gchar **out_access_token,
                                                gint *out_expires_in,
                                                GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (support),
		ubuntu_online_accounts_get_access_token), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	g_return_val_if_fail (async_context->access_token != NULL, FALSE);

	if (out_access_token != NULL) {
		*out_access_token = async_context->access_token;
		async_context->access_token = NULL;
	}

	if (out_expires_in != NULL)
		*out_expires_in = async_context->expires_in;

	return TRUE;
}

static void
e_ubuntu_online_accounts_class_init (EUbuntuOnlineAccountsClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = ubuntu_online_accounts_dispose;
	object_class->finalize = ubuntu_online_accounts_finalize;
	object_class->constructed = ubuntu_online_accounts_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SOURCE_REGISTRY_SERVER;
}

static void
e_ubuntu_online_accounts_class_finalize (EUbuntuOnlineAccountsClass *class)
{
}

static void
e_ubuntu_online_accounts_oauth2_support_init (EOAuth2SupportInterface *iface)
{
	iface->get_access_token_sync = ubuntu_online_accounts_get_access_token_sync;
	iface->get_access_token = ubuntu_online_accounts_get_access_token;
	iface->get_access_token_finish = ubuntu_online_accounts_get_access_token_finish;
}

static void
e_ubuntu_online_accounts_init (EUbuntuOnlineAccounts *extension)
{
	extension->uoa_to_eds = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) g_free);
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_ubuntu_online_accounts_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

