/*
 * module-gnome-online-accounts.c
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
#include <libsoup/soup.h>

#include <libebackend/libebackend.h>

#include "goaewsclient.h"
#include "e-goa-client.h"

/* Standard GObject macros */
#define E_TYPE_GNOME_ONLINE_ACCOUNTS \
	(e_gnome_online_accounts_get_type ())
#define E_GNOME_ONLINE_ACCOUNTS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GNOME_ONLINE_ACCOUNTS, EGnomeOnlineAccounts))
#define E_IS_GNOME_ONLINE_ACCOUNTS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_GNOME_ONLINE_ACCOUNTS))

#define CAMEL_IMAP_PROVIDER_NAME    "imapx"
#define CAMEL_SMTP_PROVIDER_NAME    "smtp"

#define CAMEL_SMTP_MECHANISM_NAME   "PLAIN"
#define CAMEL_OAUTH_MECHANISM_NAME  "XOAUTH"
#define CAMEL_OAUTH2_MECHANISM_NAME "XOAUTH2"

static void
e_goa_debug_printf (const gchar *format,
		    ...) G_GNUC_PRINTF (1, 2);

static void
e_goa_debug_printf (const gchar *format,
		    ...)
{
	static gint goa_debug = -1;
	va_list args;

	if (goa_debug == -1)
		goa_debug = g_strcmp0 (g_getenv ("GOA_DEBUG"), "1") == 0 ? 1 : 0;

	if (!goa_debug)
		return;

	va_start (args, format);
	e_util_debug_printv ("EDS-GOA", format, args);
	va_end (args);
}

typedef struct _EGnomeOnlineAccounts EGnomeOnlineAccounts;
typedef struct _EGnomeOnlineAccountsClass EGnomeOnlineAccountsClass;

struct _EGnomeOnlineAccounts {
	EExtension parent;

	EGoaClient *goa_client;
	gulong account_added_handler_id;
	gulong account_removed_handler_id;
	gulong account_swapped_handler_id;

	GCancellable *create_client;

	/* GoaAccount ID -> ESource UID */
	GHashTable *goa_to_eds;
};

struct _EGnomeOnlineAccountsClass {
	EExtensionClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_gnome_online_accounts_get_type (void);
static void e_gnome_online_accounts_oauth2_support_init
					(EOAuth2SupportInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EGnomeOnlineAccounts,
	e_gnome_online_accounts,
	E_TYPE_EXTENSION,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_OAUTH2_SUPPORT,
		e_gnome_online_accounts_oauth2_support_init))

static const gchar *
gnome_online_accounts_get_backend_name (const gchar *goa_provider_type)
{
	const gchar *eds_backend_name = NULL;

	g_return_val_if_fail (goa_provider_type != NULL, NULL);

	/* This is a mapping between GoaAccount provider types and
	 * ESourceCollection backend names.  It requires knowledge
	 * of other registry modules, possibly even from 3rd party
	 * packages.  No way around it. */

	if (g_str_equal (goa_provider_type, "exchange"))
		eds_backend_name = "ews";

	if (g_str_equal (goa_provider_type, "google"))
		eds_backend_name = "google";

	if (g_str_equal (goa_provider_type, "imap_smtp"))
		eds_backend_name = "none";

	if (g_str_equal (goa_provider_type, "owncloud"))
		eds_backend_name = "webdav";

	if (g_str_equal (goa_provider_type, "windows_live"))
		eds_backend_name = "outlook";

	if (g_str_equal (goa_provider_type, "yahoo"))
		eds_backend_name = "yahoo";

	return eds_backend_name;
}

static const gchar *
gnome_online_accounts_get_smtp_auth (GoaMail *goa_mail)
{
	if (!goa_mail_get_smtp_use_auth (goa_mail))
		return NULL;

#if GOA_CHECK_VERSION(3,11,5)
	/* XXX I guess check these in order of our own preference?
	 *     GOA relays the server's authentication capabilities
	 *     as a set of flags, but we can only choose one. */

	if (goa_mail_get_smtp_auth_xoauth2 (goa_mail))
		return "XOAUTH2";

	if (goa_mail_get_smtp_auth_plain (goa_mail))
		return "PLAIN";

	if (goa_mail_get_smtp_auth_login (goa_mail))
		return "LOGIN";
#endif

	/* Hard-coded fallback option. */
	return CAMEL_SMTP_MECHANISM_NAME;
}

static ESourceRegistryServer *
gnome_online_accounts_get_server (EGnomeOnlineAccounts *extension)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	return E_SOURCE_REGISTRY_SERVER (extensible);
}

static gboolean
gnome_online_accounts_provider_type_to_backend_name (GBinding *binding,
                                                     const GValue *source_value,
                                                     GValue *target_value,
                                                     gpointer unused)
{
	const gchar *provider_type;
	const gchar *backend_name;

	provider_type = g_value_get_string (source_value);
	backend_name = gnome_online_accounts_get_backend_name (provider_type);
	g_return_val_if_fail (backend_name != NULL, FALSE);
	g_value_set_string (target_value, backend_name);

	return TRUE;
}

static GoaObject *
gnome_online_accounts_ref_account (EGnomeOnlineAccounts *extension,
                                   ESource *source)
{
	ESourceRegistryServer *server;
	GoaObject *match = NULL;
	const gchar *extension_name;
	gchar *account_id = NULL;

	extension_name = E_SOURCE_EXTENSION_GOA;
	server = gnome_online_accounts_get_server (extension);

	source = e_source_registry_server_find_extension (
		server, source, extension_name);

	if (source != NULL) {
		ESourceGoa *goa_ext;

		goa_ext = e_source_get_extension (source, extension_name);
		account_id = e_source_goa_dup_account_id (goa_ext);

		g_object_unref (source);
	}

	if (account_id != NULL) {
		match = e_goa_client_lookup_by_id (
			extension->goa_client, account_id);
		g_free (account_id);
	}

	return match;
}

static ESource *
gnome_online_accounts_new_source (EGnomeOnlineAccounts *extension)
{
	ESourceRegistryServer *server;
	ESource *source;
	GFile *file;
	GError *error = NULL;

	/* This being a brand new data source, creating the instance
	 * should never fail but we'll check for errors just the same. */
	server = gnome_online_accounts_get_server (extension);
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

static void
goa_ews_autodiscover_done_cb (GObject *source_object,
			      GAsyncResult *result,
			      gpointer user_data)
{
	GoaObject *goa_object;
	ESource *source = user_data;
	ESourceExtension *source_extension;
	const gchar *extension_name;
	gchar *as_url = NULL;
	gchar *oab_url = NULL;
	GError *error = NULL;

	g_return_if_fail (GOA_IS_OBJECT (source_object));
	g_return_if_fail (E_IS_SOURCE (source));

	goa_object = GOA_OBJECT (source_object);

	if (!goa_ews_autodiscover_finish (goa_object, result, &as_url, &oab_url, &error) || !as_url) {
		g_message ("Failed to autodiscover EWS data: %s", error ? error->message : "Unknown error");
		g_clear_error (&error);
		g_object_unref (source);
		return;
	}

	/* XXX We don't have direct access to CamelEwsSettings from here
	 *     since it's defined in Evolution-EWS.  But we can find out
	 *     its extension name and set properties by name. */

	extension_name = e_source_camel_get_extension_name ("ews");
	source_extension = e_source_get_extension (source, extension_name);

	/* This will be NULL if Evolution-EWS is not installed. */
	if (source_extension != NULL) {
		GoaAccount *goa_account;
		CamelSettings *settings;
		SoupURI *suri;
		gchar *user, *email;

		goa_account = goa_object_peek_account (goa_object);
		user = goa_account_dup_identity (goa_account);
		email = goa_account_dup_presentation_identity (goa_account);

		suri = soup_uri_new (as_url);

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
			"host", soup_uri_get_host (suri),
			"user", user,
			"email", email,
			NULL);

		soup_uri_free (suri);
		g_free (user);
		g_free (email);
	} else {
		g_critical (
			"%s: Failed to create [%s] extension",
			G_STRFUNC, extension_name);
	}

	g_object_unref (source);
	g_free (as_url);
	g_free (oab_url);
}

static void
gnome_online_accounts_config_exchange (EGnomeOnlineAccounts *extension,
                                       ESource *source,
                                       GoaObject *goa_object)
{
	GoaExchange *goa_exchange;
	gpointer class;

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

	/* This function is called in the main thread and the autodiscovery
	   can block it, thus use the asynchronous/non-blocking version. */
	goa_ews_autodiscover (goa_object, NULL, goa_ews_autodiscover_done_cb, g_object_ref (source));
}

static void
gnome_online_accounts_config_imap (EGnomeOnlineAccounts *extension,
                                   ESource *source,
                                   GoaObject *goa_object)
{
	GoaMail *goa_mail;
	ESourceCamel *camel_extension;
	ESourceBackend *backend_extension;
	GSocketConnectable *network_address;
	CamelSettings *settings;
	const gchar *extension_name;
	const gchar *provider_name;
	gboolean use_ssl;
	gboolean use_tls;
	GError *error = NULL;

	goa_mail = goa_object_peek_mail (goa_object);

	if (goa_mail == NULL)
		return;

	if (!goa_mail_get_imap_supported (goa_mail))
		return;

	use_ssl = goa_mail_get_imap_use_ssl (goa_mail);
	use_tls = goa_mail_get_imap_use_tls (goa_mail);

	/* Check that the host string is parsable. */
	network_address = g_network_address_parse (
		goa_mail_get_imap_host (goa_mail),
		use_ssl ? 993 : 143, &error);

	/* Sanity check. */
	g_return_if_fail (
		((network_address != NULL) && (error == NULL)) ||
		((network_address == NULL) && (error != NULL)));

	if (error != NULL) {
		/* XXX Mail account will be broken if we fail. */
		g_critical ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
		return;
	}

	provider_name = CAMEL_IMAP_PROVIDER_NAME;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	backend_extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (backend_extension, provider_name);

	extension_name = e_source_camel_get_extension_name (provider_name);
	camel_extension = e_source_get_extension (source, extension_name);
	settings = e_source_camel_get_settings (camel_extension);

	camel_network_settings_set_host (
		CAMEL_NETWORK_SETTINGS (settings),
		g_network_address_get_hostname (
		G_NETWORK_ADDRESS (network_address)));

	camel_network_settings_set_port (
		CAMEL_NETWORK_SETTINGS (settings),
		g_network_address_get_port (
		G_NETWORK_ADDRESS (network_address)));

	camel_network_settings_set_user (
		CAMEL_NETWORK_SETTINGS (settings),
		goa_mail_get_imap_user_name (goa_mail));

	/* Prefer "use_ssl" over "use_tls" if both are set. */
	camel_network_settings_set_security_method (
		CAMEL_NETWORK_SETTINGS (settings),
		use_ssl ? CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT :
		use_tls ? CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT :
		CAMEL_NETWORK_SECURITY_METHOD_NONE);

	g_object_unref (network_address);
}

static void
gnome_online_accounts_config_smtp (EGnomeOnlineAccounts *extension,
                                   ESource *source,
                                   GoaObject *goa_object)
{
	GoaMail *goa_mail;
	ESourceCamel *camel_extension;
	ESourceBackend *backend_extension;
	GSocketConnectable *network_address;
	CamelSettings *settings;
	const gchar *extension_name;
	const gchar *provider_name;
	gboolean use_ssl;
	gboolean use_tls;
	GError *error = NULL;

	goa_mail = goa_object_peek_mail (goa_object);

	if (goa_mail == NULL)
		return;

	if (!goa_mail_get_smtp_supported (goa_mail))
		return;

	use_ssl = goa_mail_get_smtp_use_ssl (goa_mail);
	use_tls = goa_mail_get_smtp_use_tls (goa_mail);

	/* Check that the host string is parsable. */
	network_address = g_network_address_parse (
		goa_mail_get_smtp_host (goa_mail),
		use_ssl ? 465 : 587, &error);

	/* Sanity check. */
	g_return_if_fail (
		((network_address != NULL) && (error == NULL)) ||
		((network_address == NULL) && (error != NULL)));

	if (error != NULL) {
		/* XXX Mail account will be broken if we fail. */
		g_critical ("%s: %s", G_STRFUNC, error->message);
		g_error_free (error);
		return;
	}

	provider_name = CAMEL_SMTP_PROVIDER_NAME;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	backend_extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (backend_extension, provider_name);

	extension_name = e_source_camel_get_extension_name (provider_name);
	camel_extension = e_source_get_extension (source, extension_name);
	settings = e_source_camel_get_settings (camel_extension);

	camel_network_settings_set_host (
		CAMEL_NETWORK_SETTINGS (settings),
		g_network_address_get_hostname (
		G_NETWORK_ADDRESS (network_address)));

	camel_network_settings_set_port (
		CAMEL_NETWORK_SETTINGS (settings),
		g_network_address_get_port (
		G_NETWORK_ADDRESS (network_address)));

	camel_network_settings_set_user (
		CAMEL_NETWORK_SETTINGS (settings),
		goa_mail_get_smtp_user_name (goa_mail));

	camel_network_settings_set_auth_mechanism (
		CAMEL_NETWORK_SETTINGS (settings),
		gnome_online_accounts_get_smtp_auth (goa_mail));

	/* Prefer "use_ssl" over "use_tls" if both are set. */
	camel_network_settings_set_security_method (
		CAMEL_NETWORK_SETTINGS (settings),
		use_ssl ? CAMEL_NETWORK_SECURITY_METHOD_SSL_ON_ALTERNATE_PORT :
		use_tls ? CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT :
		CAMEL_NETWORK_SECURITY_METHOD_NONE);

	g_object_unref (network_address);
}

static void
gnome_online_accounts_config_oauth (EGnomeOnlineAccounts *extension,
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
gnome_online_accounts_config_oauth2 (EGnomeOnlineAccounts *extension,
                                     ESource *source,
                                     GoaObject *goa_object)
{
	ESourceExtension *source_extension;
	const gchar *extension_name;

	if (goa_object_peek_oauth2_based (goa_object) == NULL)
		return;

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	source_extension = e_source_get_extension (source, extension_name);

	e_source_authentication_set_method (
		E_SOURCE_AUTHENTICATION (source_extension),
		CAMEL_OAUTH2_MECHANISM_NAME);
}

static void
gnome_online_accounts_config_collection (EGnomeOnlineAccounts *extension,
                                         ESource *source,
                                         GoaObject *goa_object)
{
	GoaAccount *goa_account;
	GoaCalendar *goa_calendar;
	GoaContacts *goa_contacts;
	ESourceExtension *source_extension;
	const gchar *extension_name;

	goa_account = goa_object_get_account (goa_object);
	goa_calendar = goa_object_get_calendar (goa_object);
	goa_contacts = goa_object_get_contacts (goa_object);

	e_binding_bind_property (
		goa_account, "presentation-identity",
		source, "display-name",
		G_BINDING_SYNC_CREATE);

	source_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_authentication_set_is_external (E_SOURCE_AUTHENTICATION (source_extension), TRUE);

	extension_name = E_SOURCE_EXTENSION_GOA;
	source_extension = e_source_get_extension (source, extension_name);

	e_binding_bind_property (
		goa_account, "id",
		source_extension, "account-id",
		G_BINDING_SYNC_CREATE);

	if (goa_calendar != NULL) {
		e_binding_bind_property (
			goa_calendar, "uri",
			source_extension, "calendar-url",
			G_BINDING_SYNC_CREATE);
	}

	if (goa_contacts != NULL) {
		e_binding_bind_property (
			goa_contacts, "uri",
			source_extension, "contacts-url",
			G_BINDING_SYNC_CREATE);
	}

	extension_name = E_SOURCE_EXTENSION_COLLECTION;
	source_extension = e_source_get_extension (source, extension_name);

	e_binding_bind_property_full (
		goa_account, "provider-type",
		source_extension, "backend-name",
		G_BINDING_SYNC_CREATE,
		gnome_online_accounts_provider_type_to_backend_name,
		NULL,
		NULL, (GDestroyNotify) NULL);

	e_binding_bind_property (
		goa_account, "identity",
		source_extension, "identity",
		G_BINDING_SYNC_CREATE);

	e_binding_bind_property (
		goa_account, "calendar-disabled",
		source_extension, "calendar-enabled",
		G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

	e_binding_bind_property (
		goa_account, "contacts-disabled",
		source_extension, "contacts-enabled",
		G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

	e_binding_bind_property (
		goa_account, "mail-disabled",
		source_extension, "mail-enabled",
		G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

	g_clear_object (&goa_account);
	g_clear_object (&goa_calendar);
	g_clear_object (&goa_contacts);

	/* Handle optional GOA interfaces. */
	gnome_online_accounts_config_exchange (extension, source, goa_object);

	e_server_side_source_set_writable (E_SERVER_SIDE_SOURCE (source), TRUE);

	/* The data source should not be removable by clients. */
	e_server_side_source_set_removable (E_SERVER_SIDE_SOURCE (source), FALSE);

	if (goa_object_peek_oauth2_based (goa_object) != NULL) {
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
gnome_online_accounts_config_mail_account (EGnomeOnlineAccounts *extension,
                                           ESource *source,
                                           GoaObject *goa_object)
{
	EServerSideSource *server_side_source;

	/* This DOES NOT set the auth mechanism. */
	gnome_online_accounts_config_imap (extension, source, goa_object);

	/* Only one or the other should be present, not both. */
	gnome_online_accounts_config_oauth (extension, source, goa_object);
	gnome_online_accounts_config_oauth2 (extension, source, goa_object);

	/* Clients may change the source by may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static gboolean
e_goa_transform_only_when_original_same_cb (GBinding *binding,
					    const GValue *from_value,
					    GValue *to_value,
					    gpointer user_data)
{
	EGnomeOnlineAccounts *extension = user_data;
	ESourceMailIdentity *mail_identity;
	ESourceRegistryServer *registry_server;
	ESource *collection, *source;
	const gchar *new_value;
	gboolean to_value_set = FALSE;

	g_return_val_if_fail (E_IS_GNOME_ONLINE_ACCOUNTS (extension), TRUE);

	new_value = g_value_get_string (from_value);
	if (new_value && !*new_value)
		new_value = NULL;

	mail_identity = E_SOURCE_MAIL_IDENTITY (g_binding_get_target (binding));
	source = e_source_extension_ref_source (E_SOURCE_EXTENSION (mail_identity));

	registry_server = gnome_online_accounts_get_server (extension);
	collection = e_source_registry_server_ref_source (registry_server, e_source_get_parent (source));

	/* The collection can be NULL when the account was just created. */
	if (source && collection) {
		ESourceGoa *goa_extension;
		gchar *set_value = NULL, *old_value = NULL;
		const gchar *prop_name;
		gboolean changed;

		g_warn_if_fail (e_source_has_extension (collection, E_SOURCE_EXTENSION_GOA));

		prop_name = g_binding_get_target_property (binding);
		goa_extension = e_source_get_extension (collection, E_SOURCE_EXTENSION_GOA);

		g_object_get (G_OBJECT (goa_extension), prop_name, &old_value, NULL);

		changed = g_strcmp0 (old_value, new_value) != 0;

		if (changed) {
			g_object_set (G_OBJECT (goa_extension), prop_name, new_value, NULL);

			g_object_get (G_OBJECT (mail_identity), prop_name, &set_value, NULL);

			if (g_strcmp0 (set_value, old_value) != 0) {
				to_value_set = TRUE;

				g_value_set_string (to_value, set_value);
			}
		} else {
			g_object_get (G_OBJECT (mail_identity), prop_name, &set_value, NULL);
			to_value_set = TRUE;
			g_value_set_string (to_value, set_value);
		}

		g_free (set_value);
		g_free (old_value);
	}

	g_clear_object (&collection);
	g_clear_object (&source);

	if (!to_value_set)
		g_value_set_string (to_value, new_value);

	return TRUE;
}

static void
gnome_online_accounts_config_mail_identity (EGnomeOnlineAccounts *extension,
                                            ESource *source,
                                            GoaObject *goa_object)
{
	GoaMail *goa_mail;
	EServerSideSource *server_side_source;
	ESourceMailIdentity *mail_identity;
	ESourceMailSubmission *mail_submission;
	ESourceMailComposition *mail_composition;
	const gchar *extension_name;
	gchar *tmp;

	goa_mail = goa_object_get_mail (goa_object);
	/* NULL, when the Mail part is disabled */
	if (!goa_mail)
		return;

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	mail_identity = e_source_get_extension (source, extension_name);

	e_binding_bind_property_full (
		goa_mail, "name",
		mail_identity, "name",
		G_BINDING_SYNC_CREATE,
		e_goa_transform_only_when_original_same_cb,
		NULL,
		g_object_ref (extension),
		g_object_unref);

	e_binding_bind_property_full (
		goa_mail, "email-address",
		mail_identity, "address",
		G_BINDING_SYNC_CREATE,
		e_goa_transform_only_when_original_same_cb,
		NULL,
		g_object_ref (extension),
		g_object_unref);

	g_object_unref (goa_mail);

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

	/* Clients may change the source by may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
gnome_online_accounts_config_mail_transport (EGnomeOnlineAccounts *extension,
                                             ESource *source,
                                             GoaObject *goa_object)
{
	EServerSideSource *server_side_source;

	/* This DOES set the auth mechanism. */
	gnome_online_accounts_config_smtp (extension, source, goa_object);

	/* Clients may change the source by may not remove it. */
	server_side_source = E_SERVER_SIDE_SOURCE (source);
	e_server_side_source_set_writable (server_side_source, TRUE);
	e_server_side_source_set_removable (server_side_source, FALSE);
}

static void
gnome_online_accounts_config_sources (EGnomeOnlineAccounts *extension,
                                      ESource *source,
                                      GoaObject *goa_object)
{
	ESourceRegistryServer *server;
	ECollectionBackend *backend;
	GList *list, *link;

	/* XXX This function was primarily intended to smooth the
	 *     transition of mail accounts from XOAUTH to XOAUTH2,
	 *     but it may be useful for other types of migration. */

	gnome_online_accounts_config_collection (extension, source, goa_object);

	server = gnome_online_accounts_get_server (extension);
	backend = e_source_registry_server_ref_backend (server, source);
	g_return_if_fail (backend != NULL);

	list = e_collection_backend_list_mail_sources (backend);

	for (link = list; link != NULL; link = g_list_next (link)) {
		const gchar *extension_name;

		source = E_SOURCE (link->data);

		extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
		if (e_source_has_extension (source, extension_name))
			gnome_online_accounts_config_mail_account (
				extension, source, goa_object);

		extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
		if (e_source_has_extension (source, extension_name))
			gnome_online_accounts_config_mail_identity (
				extension, source, goa_object);

		extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
		if (e_source_has_extension (source, extension_name))
			gnome_online_accounts_config_mail_transport (
				extension, source, goa_object);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	g_object_unref (backend);
}

static void
gnome_online_accounts_create_collection (EGnomeOnlineAccounts *extension,
                                         EBackendFactory *backend_factory,
                                         GoaObject *goa_object)
{
	GoaAccount *goa_account;
	GoaMail *goa_mail;
	ESourceRegistryServer *server;
	ESource *collection_source;
	ESource *mail_account_source = NULL;
	ESource *mail_identity_source = NULL;
	ESource *mail_transport_source = NULL;
	const gchar *account_id;
	const gchar *parent_uid;

	server = gnome_online_accounts_get_server (extension);

	collection_source = gnome_online_accounts_new_source (extension);
	g_return_if_fail (E_IS_SOURCE (collection_source));

	gnome_online_accounts_config_collection (
		extension, collection_source, goa_object);
	parent_uid = e_source_get_uid (collection_source);

	goa_mail = goa_object_get_mail (goa_object);
	if (goa_mail) {
		ESourceGoa *goa_extension;
		gchar *name = NULL, *address = NULL;

		goa_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_GOA);

		g_object_get (G_OBJECT (goa_mail),
			"name", &name,
			"email-address", &address,
			NULL);

		g_object_set (G_OBJECT (goa_extension),
			"name", name,
			"address", address,
			NULL);

		g_object_unref (goa_mail);
		g_free (name);
		g_free (address);

		mail_account_source = gnome_online_accounts_new_source (extension);
		g_return_if_fail (E_IS_SOURCE (mail_account_source));

		mail_identity_source = gnome_online_accounts_new_source (extension);
		g_return_if_fail (E_IS_SOURCE (mail_identity_source));

		mail_transport_source = gnome_online_accounts_new_source (extension);
		g_return_if_fail (E_IS_SOURCE (mail_transport_source));

		/* Configure parent/child relationships. */
		e_source_set_parent (mail_account_source, parent_uid);
		e_source_set_parent (mail_identity_source, parent_uid);
		e_source_set_parent (mail_transport_source, parent_uid);

		/* Give the factory first crack at mail configuration. */
		e_collection_backend_factory_prepare_mail (
			E_COLLECTION_BACKEND_FACTORY (backend_factory),
			mail_account_source,
			mail_identity_source,
			mail_transport_source);

		gnome_online_accounts_config_mail_account (
			extension, mail_account_source, goa_object);
		gnome_online_accounts_config_mail_identity (
			extension, mail_identity_source, goa_object);
		gnome_online_accounts_config_mail_transport (
			extension, mail_transport_source, goa_object);
	}

	/* Export the new source collection. */
	e_source_registry_server_add_source (server, collection_source);

	if (mail_account_source != NULL) {
		e_source_registry_server_add_source (
			server, mail_account_source);
		g_object_unref (mail_account_source);
	}

	if (mail_identity_source != NULL) {
		e_source_registry_server_add_source (
			server, mail_identity_source);
		g_object_unref (mail_identity_source);
	}

	if (mail_transport_source != NULL) {
		e_source_registry_server_add_source (
			server, mail_transport_source);
		g_object_unref (mail_transport_source);
	}

	goa_account = goa_object_get_account (goa_object);
	account_id = goa_account_get_id (goa_account);

	g_hash_table_insert (
		extension->goa_to_eds,
		g_strdup (account_id),
		g_strdup (parent_uid));

	g_object_unref (goa_account);
	g_object_unref (collection_source);
}

static void
gnome_online_accounts_remove_collection (EGnomeOnlineAccounts *extension,
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
gnome_online_accounts_account_added_cb (EGoaClient *goa_client,
                                        GoaObject *goa_object,
                                        EGnomeOnlineAccounts *extension)
{
	GoaAccount *goa_account;
	ESourceRegistryServer *server;
	EBackendFactory *backend_factory = NULL;
	const gchar *provider_type;
	const gchar *backend_name;
	const gchar *account_id;
	const gchar *source_uid;

	server = gnome_online_accounts_get_server (extension);

	goa_account = goa_object_get_account (goa_object);
	provider_type = goa_account_get_provider_type (goa_account);
	backend_name = gnome_online_accounts_get_backend_name (provider_type);

	account_id = goa_account_get_id (goa_account);
	source_uid = g_hash_table_lookup (extension->goa_to_eds, account_id);

	if (backend_name) {
		if (source_uid) {
			e_goa_debug_printf ("Pairing account '%s' with existing source '%s' and backend '%s'\n",
				account_id, source_uid, backend_name);
		} else {
			e_goa_debug_printf ("Create new factory for account '%s' and backend '%s'\n",
				account_id, backend_name);
		}
	} else {
		e_goa_debug_printf ("No suitable backend found for account '%s'\n", account_id);
	}

	if (source_uid == NULL && backend_name != NULL)
		backend_factory = e_data_factory_ref_backend_factory (
			E_DATA_FACTORY (server), backend_name, E_SOURCE_EXTENSION_COLLECTION);

	if (backend_factory != NULL) {
		gnome_online_accounts_create_collection (
			extension, backend_factory, goa_object);
		g_object_unref (backend_factory);
	}

	g_object_unref (goa_account);
}

static void
gnome_online_accounts_account_removed_cb (EGoaClient *goa_client,
                                          GoaObject *goa_object,
                                          EGnomeOnlineAccounts *extension)
{
	ESource *source = NULL;
	ESourceRegistryServer *server;
	GoaAccount *goa_account;
	const gchar *account_id;
	const gchar *source_uid;

	server = gnome_online_accounts_get_server (extension);

	goa_account = goa_object_get_account (goa_object);

	account_id = goa_account_get_id (goa_account);
	source_uid = g_hash_table_lookup (extension->goa_to_eds, account_id);

	if (source_uid) {
		e_goa_debug_printf ("Account '%s' removed with corresponding to source '%s'\n",
			account_id, source_uid);
	} else {
		e_goa_debug_printf ("Account '%s' removed without any corresponding source\n", account_id);
	}

	if (source_uid != NULL)
		source = e_source_registry_server_ref_source (
			server, source_uid);

	if (source != NULL) {
		gnome_online_accounts_remove_collection (extension, source);
		g_object_unref (source);
	}

	g_object_unref (goa_account);
}

static void
gnome_online_accounts_account_swapped_cb (EGoaClient *goa_client,
                                          GoaObject *old_goa_object,
                                          GoaObject *new_goa_object,
                                          EGnomeOnlineAccounts *extension)
{
	ESource *source = NULL;
	ESourceRegistryServer *server;
	GoaAccount *goa_account;
	const gchar *account_id;
	const gchar *source_uid;

	/* The old GoaObject is about to be destroyed so we should
	 * not need to bother with undoing property bindings on it.
	 * Just set up new property bindings on the new GoaObject. */

	server = gnome_online_accounts_get_server (extension);

	goa_account = goa_object_get_account (new_goa_object);

	account_id = goa_account_get_id (goa_account);
	source_uid = g_hash_table_lookup (extension->goa_to_eds, account_id);

	e_goa_debug_printf ("Account '%s' swapped to '%s'\n",
		goa_account_get_id (goa_object_get_account (old_goa_object)), account_id);

	if (source_uid != NULL)
		source = e_source_registry_server_ref_source (
			server, source_uid);

	if (source != NULL) {
		gnome_online_accounts_config_sources (
			extension, source, new_goa_object);
		g_object_unref (source);
	}

	g_object_unref (goa_account);
}

static gint
gnome_online_accounts_compare_id (GoaObject *goa_object,
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
gnome_online_accounts_populate_accounts_table (EGnomeOnlineAccounts *extension,
                                               GList *goa_objects)
{
	ESourceRegistryServer *server;
	GQueue trash = G_QUEUE_INIT;
	GList *list, *link;
	const gchar *extension_name;

	server = gnome_online_accounts_get_server (extension);

	extension_name = E_SOURCE_EXTENSION_GOA;
	list = e_source_registry_server_list_sources (server, extension_name);

	e_goa_debug_printf ("Found %d existing sources\n", g_list_length (list));

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

		if (account_id == NULL) {
			e_goa_debug_printf ("Source '%s' has no account id\n", source_uid);
			continue;
		}

		if (g_hash_table_lookup (extension->goa_to_eds, account_id)) {
			e_goa_debug_printf ("Source '%s' references account '%s' which is already used by other source\n",
				source_uid, account_id);

			/* There are more ESource-s referencing the same GOA account;
			   delete the later. */
			g_queue_push_tail (&trash, source);
			continue;
		}

		/* Verify the GOA account still exists. */
		match = g_list_find_custom (
			goa_objects, account_id,
			(GCompareFunc) gnome_online_accounts_compare_id);

		/* If a matching GoaObject was found, add its ID
		 * to our accounts hash table.  Otherwise remove
		 * the ESource after we finish looping. */
		if (match != NULL) {
			GoaObject *goa_object;

			e_goa_debug_printf ("Assign source '%s' (enabled:%d) with account '%s'\n",
				source_uid, e_source_get_enabled (source), account_id);

			g_hash_table_insert (
				extension->goa_to_eds,
				g_strdup (account_id),
				g_strdup (source_uid));

			goa_object = GOA_OBJECT (match->data);
			gnome_online_accounts_config_sources (
				extension, source, goa_object);
		} else {
			e_goa_debug_printf ("Account '%s' doesn't exist, remove source '%s'\n",
				account_id, source_uid);

			g_queue_push_tail (&trash, source);
		}
	}

	/* Empty the trash. */
	while (!g_queue_is_empty (&trash)) {
		ESource *source = g_queue_pop_head (&trash);
		gnome_online_accounts_remove_collection (extension, source);
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
gnome_online_accounts_create_client_cb (GObject *source_object,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
	EGnomeOnlineAccounts *extension;
	EGoaClient *goa_client;
	GList *list, *link;
	gulong handler_id;
	GError *error = NULL;

	/* If we get back a G_IO_ERROR_CANCELLED then it means the
	 * EGnomeOnlineAccounts is already finalized, so be careful
	 * not to touch it until after we have a valid EGoaClient. */

	goa_client = e_goa_client_new_finish (result, &error);

	if (error != NULL) {
		e_goa_debug_printf ("Failed to connect to the service: %s\n", error->message);

		g_warn_if_fail (goa_client == NULL);
		g_warning (
			"Unable to connect to the GNOME Online "
			"Accounts service: %s", error->message);
		g_error_free (error);
		return;
	}

	g_return_if_fail (E_IS_GOA_CLIENT (goa_client));

	/* Should be safe to dereference the EGnomeOnlineAccounts now. */

	extension = E_GNOME_ONLINE_ACCOUNTS (user_data);
	extension->goa_client = goa_client;  /* takes ownership */

	/* Don't need the GCancellable anymore. */
	g_object_unref (extension->create_client);
	extension->create_client = NULL;

	list = e_goa_client_list_accounts (extension->goa_client);

	e_goa_debug_printf ("Connected to service, received %d accounts\n", g_list_length (list));

	/* This populates a hash table of GOA ID -> ESource UID strings by
	 * searching through available data sources for ones with a "GNOME
	 * Online Accounts" extension.  If such an extension is found, but
	 * no corresponding GoaAccount (presumably meaning the GOA account
	 * was somehow deleted between E-D-S sessions) then the ESource in
	 * which the extension was found gets deleted. */
	gnome_online_accounts_populate_accounts_table (extension, list);

	for (link = list; link != NULL; link = g_list_next (link))
		gnome_online_accounts_account_added_cb (
			extension->goa_client,
			GOA_OBJECT (link->data),
			extension);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Listen for Online Account changes. */

	handler_id = g_signal_connect (
		extension->goa_client, "account-added",
		G_CALLBACK (gnome_online_accounts_account_added_cb),
		extension);
	extension->account_added_handler_id = handler_id;

	handler_id = g_signal_connect (
		extension->goa_client, "account-removed",
		G_CALLBACK (gnome_online_accounts_account_removed_cb),
		extension);
	extension->account_removed_handler_id = handler_id;

	handler_id = g_signal_connect (
		extension->goa_client, "account-swapped",
		G_CALLBACK (gnome_online_accounts_account_swapped_cb),
		extension);
	extension->account_swapped_handler_id = handler_id;
}

static void
gnome_online_accounts_bus_acquired_cb (EDBusServer *server,
                                       GDBusConnection *connection,
                                       EGnomeOnlineAccounts *extension)
{
	/* Connect to the GNOME Online Accounts service. */

	e_goa_debug_printf ("Bus-acquired, connecting to the service\n");

	/* Note we don't reference the extension.  If the
	 * extension gets destroyed before this completes
	 * we cancel the operation from dispose(). */
	e_goa_client_new (
		extension->create_client,
		gnome_online_accounts_create_client_cb,
		extension);
}

static void
gnome_online_accounts_dispose (GObject *object)
{
	EGnomeOnlineAccounts *extension;

	extension = E_GNOME_ONLINE_ACCOUNTS (object);

	if (extension->account_added_handler_id > 0) {
		g_signal_handler_disconnect (
			extension->goa_client,
			extension->account_added_handler_id);
		extension->account_added_handler_id = 0;
	}

	if (extension->account_removed_handler_id > 0) {
		g_signal_handler_disconnect (
			extension->goa_client,
			extension->account_removed_handler_id);
		extension->account_removed_handler_id = 0;
	}

	if (extension->account_swapped_handler_id > 0) {
		g_signal_handler_disconnect (
			extension->goa_client,
			extension->account_swapped_handler_id);
		extension->account_swapped_handler_id = 0;
	}

	/* This cancels e_goa_client_new() in case it still
	 * hasn't completed.  We're no longer interested. */
	g_cancellable_cancel (extension->create_client);

	g_clear_object (&extension->goa_client);
	g_clear_object (&extension->create_client);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_gnome_online_accounts_parent_class)->
		dispose (object);
}

static void
gnome_online_accounts_finalize (GObject *object)
{
	EGnomeOnlineAccounts *extension;

	extension = E_GNOME_ONLINE_ACCOUNTS (object);

	g_hash_table_destroy (extension->goa_to_eds);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_gnome_online_accounts_parent_class)->
		finalize (object);
}

static void
gnome_online_accounts_constructed (GObject *object)
{
	EExtension *extension;
	EExtensible *extensible;

	extension = E_EXTENSION (object);
	extensible = e_extension_get_extensible (extension);

	e_goa_debug_printf ("Waiting for bus-acquired signal\n");

	/* Wait for the registry service to acquire its well-known
	 * bus name so we don't do anything destructive beforehand. */

	g_signal_connect (
		extensible, "bus-acquired",
		G_CALLBACK (gnome_online_accounts_bus_acquired_cb),
		extension);

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_gnome_online_accounts_parent_class)->constructed (object);
}

static gboolean
gnome_online_accounts_get_access_token_sync (EOAuth2Support *support,
                                             ESource *source,
                                             GCancellable *cancellable,
                                             gchar **out_access_token,
                                             gint *out_expires_in,
                                             GError **error)
{
	GoaObject *goa_object;
	GoaAccount *goa_account;
	GoaOAuth2Based *goa_oauth2_based;
	gboolean success;
	GError *local_error = NULL;

	goa_object = gnome_online_accounts_ref_account (
		E_GNOME_ONLINE_ACCOUNTS (support), source);

	if (goa_object == NULL) {
		e_goa_debug_printf ("GetAccessToken: \"%s\" (%s): Cannot find a corresponding GOA account\n",
			e_source_get_display_name (source), e_source_get_uid (source));

		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("Cannot find a corresponding account in "
			"the org.gnome.OnlineAccounts service from "
			"which to obtain an access token for “%s”"),
			e_source_get_display_name (source));
		return FALSE;
	}

	goa_account = goa_object_get_account (goa_object);
	g_return_val_if_fail (goa_account != NULL, FALSE);

	goa_oauth2_based = goa_object_get_oauth2_based (goa_object);
	g_return_val_if_fail (goa_oauth2_based != NULL, FALSE);

	e_goa_debug_printf ("GetAccessToken: \"%s\" (%s): Calling ensure-credentials\n",
		e_source_get_display_name (source), e_source_get_uid (source));

	success = goa_account_call_ensure_credentials_sync (
		goa_account, NULL, cancellable, &local_error);

	if (success) {
		e_goa_debug_printf ("GetAccessToken: \"%s\" (%s): ensure-credentials succeeded, calling get-access-token\n",
			e_source_get_display_name (source), e_source_get_uid (source));

		success = goa_oauth2_based_call_get_access_token_sync (
			goa_oauth2_based, out_access_token,
			out_expires_in, cancellable, &local_error);

		if (success) {
			e_goa_debug_printf ("GetAccessToken: \"%s\" (%s): get-access-token succeeded\n",
				e_source_get_display_name (source), e_source_get_uid (source));
		} else {
			e_goa_debug_printf ("GetAccessToken: \"%s\" (%s): get-access-token failed: %s\n",
				e_source_get_display_name (source), e_source_get_uid (source),
				local_error ? local_error->message : "Unknown error");
		}
	} else {
		e_goa_debug_printf ("GetAccessToken: \"%s\" (%s): ensure-credentials failed: %s\n",
			e_source_get_display_name (source), e_source_get_uid (source),
			local_error ? local_error->message : "Unknown error");
	}

	g_object_unref (goa_oauth2_based);
	g_object_unref (goa_account);
	g_object_unref (goa_object);

	if (local_error) {
		g_dbus_error_strip_remote_error (local_error);

		g_prefix_error (
			&local_error,
			_("Failed to obtain an access token for “%s”: "),
			e_source_get_display_name (source));

		g_propagate_error (error, local_error);
	}

	return success;
}

static void
e_gnome_online_accounts_class_init (EGnomeOnlineAccountsClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = gnome_online_accounts_dispose;
	object_class->finalize = gnome_online_accounts_finalize;
	object_class->constructed = gnome_online_accounts_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_SOURCE_REGISTRY_SERVER;
}

static void
e_gnome_online_accounts_class_finalize (EGnomeOnlineAccountsClass *class)
{
}

static void
e_gnome_online_accounts_oauth2_support_init (EOAuth2SupportInterface *iface)
{
	iface->get_access_token_sync =
		gnome_online_accounts_get_access_token_sync;
}

static void
e_gnome_online_accounts_init (EGnomeOnlineAccounts *extension)
{
	/* Used to cancel unfinished e_goa_client_new(). */
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
	e_goa_client_type_register (type_module);
	e_gnome_online_accounts_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

