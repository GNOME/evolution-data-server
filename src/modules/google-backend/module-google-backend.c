/*
 * module-google-backend.c
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

#include <libebackend/libebackend.h>
#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_GOOGLE_BACKEND \
	(e_google_backend_get_type ())
#define E_GOOGLE_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GOOGLE_BACKEND, EGoogleBackend))

/* Just for readability... */
#define METHOD(x) (CAMEL_NETWORK_SECURITY_METHOD_##x)

#define GOOGLE_OAUTH2_METHOD		"Google"

/* IMAP Configuration Details */
#define GOOGLE_IMAP_BACKEND_NAME	"imapx"
#define GOOGLE_IMAP_HOST		"imap.googlemail.com"
#define GOOGLE_IMAP_PORT		993
#define GOOGLE_IMAP_SECURITY_METHOD	METHOD (SSL_ON_ALTERNATE_PORT)

/* SMTP Configuration Details */
#define GOOGLE_SMTP_BACKEND_NAME	"smtp"
#define GOOGLE_SMTP_HOST		"smtp.googlemail.com"
#define GOOGLE_SMTP_PORT		465
#define GOOGLE_SMTP_SECURITY_METHOD	METHOD (SSL_ON_ALTERNATE_PORT)

/* Tasks Configuration Details */
#define GOOGLE_TASKS_BACKEND_NAME	"gtasks"

typedef struct _EGoogleBackend EGoogleBackend;
typedef struct _EGoogleBackendClass EGoogleBackendClass;

typedef struct _EGoogleBackendFactory EGoogleBackendFactory;
typedef struct _EGoogleBackendFactoryClass EGoogleBackendFactoryClass;

struct _EGoogleBackend {
	EWebDAVCollectionBackend parent;
};

struct _EGoogleBackendClass {
	EWebDAVCollectionBackendClass parent_class;
};

struct _EGoogleBackendFactory {
	ECollectionBackendFactory parent;
};

struct _EGoogleBackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_google_backend_get_type (void);
GType e_google_backend_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EGoogleBackend,
	e_google_backend,
	E_TYPE_WEBDAV_COLLECTION_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EGoogleBackendFactory,
	e_google_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static gboolean
google_backend_can_use_google_auth (ESource *source)
{
	ESourceRegistryServer *registry;
	ESourceAuthentication *auth_extension;
	gboolean res;

	g_return_val_if_fail (E_IS_SERVER_SIDE_SOURCE (source), FALSE);

	registry = e_server_side_source_get_server (E_SERVER_SIDE_SOURCE (source));
	if (!e_oauth2_services_is_oauth2_alias (e_source_registry_server_get_oauth2_services (registry), GOOGLE_OAUTH2_METHOD))
		return FALSE;

	g_object_ref (source);

	while (source && e_source_get_parent (source)) {
		ESource *adept_source;

		adept_source = e_source_registry_server_ref_source (registry, e_source_get_parent (source));
		if (adept_source) {
			g_object_unref (source);
			source = adept_source;
		} else {
			break;
		}
	}

	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	res = !e_source_authentication_get_is_external (auth_extension);

	g_object_unref (source);

	return res;
}

static gboolean
host_ends_with (const gchar *host,
		const gchar *ends_with)
{
	gint host_len, ends_with_len;

	if (!host || !ends_with)
		return FALSE;

	host_len = strlen (host);
	ends_with_len = strlen (ends_with);

	if (host_len <= ends_with_len)
		return FALSE;

	return g_ascii_strcasecmp (host + host_len - ends_with_len, ends_with) == 0;
}

static gboolean
google_backend_is_google_host (ESourceAuthentication *auth_extension,
			       gboolean *out_requires_oauth2)
{
	gboolean is_google;
	gboolean requires_oauth2;
	gchar *host;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (auth_extension), FALSE);

	host = e_source_authentication_dup_host (auth_extension);

	requires_oauth2 = host && host_ends_with (host, "googleusercontent.com");

	is_google = requires_oauth2 || (host && (
		host_ends_with (host, "gmail.com") ||
		host_ends_with (host, "googlemail.com") ||
		host_ends_with (host, "google.com")));

	g_free (host);

	if (out_requires_oauth2)
		*out_requires_oauth2 = requires_oauth2;

	return is_google;
}

static gboolean
google_backend_is_oauth2 (const gchar *method)
{
	return g_strcmp0 (method, GOOGLE_OAUTH2_METHOD) == 0 ||
		g_strcmp0 (method, "OAuth2") == 0 ||
		g_strcmp0 (method, "XOAUTH2") == 0;
}

static gboolean
google_backend_can_change_auth_method (ESourceAuthentication *auth_extension,
				       const gchar *new_method)
{
	gchar *cur_method;
	gboolean can_change;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (auth_extension), FALSE);

	if (!new_method)
		return FALSE;

	cur_method = e_source_authentication_dup_method (auth_extension);

	/* Only when turning off OAuth2 */
	can_change = google_backend_is_oauth2 (cur_method) && !google_backend_is_oauth2 (new_method);

	g_free (cur_method);

	return can_change;
}

static void
google_backend_mail_update_auth_method (ECollectionBackend *collection_backend,
					ESource *child_source,
					ESource *master_source)
{
	ESourceAuthentication *auth_extension;
	EOAuth2Support *oauth2_support;
	const gchar *method;
	gboolean can_use_google_auth;

	auth_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	if (!google_backend_is_google_host (auth_extension, NULL))
		return;

	oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (child_source));
	if (!oauth2_support && master_source)
		oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (master_source));

	can_use_google_auth = google_backend_can_use_google_auth (child_source);
	if (!can_use_google_auth && master_source)
		can_use_google_auth = google_backend_can_use_google_auth (master_source);

	if (oauth2_support && !can_use_google_auth) {
		method = "XOAUTH2";
	} else if (can_use_google_auth) {
		method = GOOGLE_OAUTH2_METHOD;
	} else {
		method = NULL;
	}

	if (method && (e_collection_backend_is_new_source (collection_backend, child_source) ||
	    google_backend_can_change_auth_method (auth_extension, method)))
		e_source_authentication_set_method (auth_extension, method);

	g_clear_object (&oauth2_support);
}

static void
google_backend_mail_update_auth_method_cb (ESource *child_source,
					   GParamSpec *param,
					   EBackend *backend)
{
	google_backend_mail_update_auth_method (E_COLLECTION_BACKEND (backend), child_source, e_backend_get_source (backend));
}

static void
google_backend_calendar_update_auth_method (ECollectionBackend *collection_backend,
					    ESource *child_source,
					    ESource *master_source)
{
	EOAuth2Support *oauth2_support;
	ESourceAuthentication *auth_extension;
	const gchar *method;
	gboolean can_use_google_auth, requires_oauth2 = FALSE;

	auth_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	if (!google_backend_is_google_host (auth_extension, &requires_oauth2))
		return;

	oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (child_source));
	if (!oauth2_support && master_source)
		oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (master_source));

	can_use_google_auth = google_backend_can_use_google_auth (child_source);
	if (!can_use_google_auth && master_source)
		can_use_google_auth = google_backend_can_use_google_auth (master_source);

	if (oauth2_support && !can_use_google_auth) {
		method = "OAuth2";
	} else if (can_use_google_auth) {
		method = GOOGLE_OAUTH2_METHOD;
	} else {
		method = "plain/password";
	}

	if (requires_oauth2 ||
	    e_collection_backend_is_new_source (collection_backend, child_source) ||
	    google_backend_can_change_auth_method (auth_extension, method))
		e_source_authentication_set_method (auth_extension, method);

	g_clear_object (&oauth2_support);
}

static void
google_backend_calendar_update_auth_method_cb (ESource *child_source,
					       GParamSpec *param,
					       EBackend *backend)
{
	google_backend_calendar_update_auth_method (E_COLLECTION_BACKEND (backend), child_source, e_backend_get_source (backend));
}

static void
google_backend_contacts_update_auth_method (ESource *child_source,
					    ESource *master_source)
{
	EOAuth2Support *oauth2_support;
	ESourceAuthentication *extension;
	const gchar *method;
	gboolean can_use_google_auth;

	extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_AUTHENTICATION);

	if (!google_backend_is_google_host (extension, NULL))
		return;

	oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (child_source));
	if (!oauth2_support && master_source)
		oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (master_source));

	can_use_google_auth = google_backend_can_use_google_auth (child_source);
	if (!can_use_google_auth && master_source)
		can_use_google_auth = google_backend_can_use_google_auth (master_source);

	if (oauth2_support && !can_use_google_auth)
		method = "OAuth2";
	else if (can_use_google_auth)
		method = GOOGLE_OAUTH2_METHOD;
	else
		method = "OAuth2";

	/* "ClientLogin" for Contacts is not supported anymore, thus
	   fallback to OAuth2 method regardless it's supported or not. */

	e_source_authentication_set_method (extension, method);

	g_clear_object (&oauth2_support);
}

static void
google_backend_contacts_update_auth_method_cb (ESource *child_source,
					       GParamSpec *param,
					       EBackend *backend)
{
	google_backend_contacts_update_auth_method (child_source, e_backend_get_source (backend));
}

static void
google_add_task_list_uid_to_hashtable (gpointer source,
				       gpointer known_sources)
{
	ESourceResource *resource;
	gchar *uid, *rid;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_RESOURCE) ||
	    !e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
		return;

	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);

	uid = e_source_dup_uid (source);
	if (!uid || !*uid) {
		g_free (uid);
		return;
	}

	rid = e_source_resource_dup_identity (resource);
	if (!rid || !*rid) {
		g_free (rid);
		g_free (uid);
		return;
	}

	g_hash_table_insert (known_sources, rid, uid);
}

static void
google_remove_unknown_sources_cb (gpointer resource_id,
				  gpointer uid,
				  gpointer user_data)
{
	ESourceRegistryServer *server = user_data;
	ESource *source;

	source = e_source_registry_server_ref_source (server, uid);

	if (source) {
		e_source_remove_sync (source, NULL, NULL);
		g_object_unref (source);
	}
}

static void
google_add_task_list (ECollectionBackend *collection,
		      const gchar *resource_id,
		      const gchar *display_name,
		      GHashTable *known_sources)
{
	ESourceRegistryServer *server;
	ESource *source;
	ESource *collection_source;
	ESourceExtension *extension;
	ESourceCollection *collection_extension;
	ESourceResource *resource;
	const gchar *source_uid;
	gchar *identity;
	gboolean is_new;

	collection_source = e_backend_get_source (E_BACKEND (collection));

	server = e_collection_backend_ref_server (collection);
	if (!server)
		return;

	identity = g_strconcat (GOOGLE_TASKS_BACKEND_NAME, "::", resource_id, NULL);
	source_uid = g_hash_table_lookup (known_sources, identity);
	is_new = !source_uid;
	if (is_new) {
		source = e_collection_backend_new_child (collection, identity);
		g_warn_if_fail (source != NULL);
	} else {
		source = e_source_registry_server_ref_source (server, source_uid);
		g_warn_if_fail (source != NULL);

		g_hash_table_remove (known_sources, identity);
	}

	resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
	e_source_resource_set_identity (resource, identity);

	e_source_set_display_name (source, display_name);

	collection_extension = e_source_get_extension (
		collection_source, E_SOURCE_EXTENSION_COLLECTION);

	/* Configure the calendar source. */

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_TASK_LIST);

	e_source_backend_set_backend_name (E_SOURCE_BACKEND (extension), GOOGLE_TASKS_BACKEND_NAME);

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	e_source_authentication_set_host (E_SOURCE_AUTHENTICATION (extension), "www.google.com");
	if (google_backend_can_use_google_auth (collection_source))
		e_source_authentication_set_method (E_SOURCE_AUTHENTICATION (extension), GOOGLE_OAUTH2_METHOD);
	else
		e_source_authentication_set_method (E_SOURCE_AUTHENTICATION (extension), "OAuth2");

	e_binding_bind_property (
		collection_extension, "identity",
		extension, "user",
		G_BINDING_SYNC_CREATE);

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_ALARMS);
	e_source_alarms_set_include_me (E_SOURCE_ALARMS (extension), FALSE);

	if (is_new) {
		ESourceRegistryServer *server;

		server = e_collection_backend_ref_server (collection);
		e_source_registry_server_add_source (server, source);
		g_object_unref (server);
	}

	g_object_unref (source);
	g_object_unref (server);
	g_free (identity);
}

typedef struct _TaskListsData {
	ECollectionBackend *collection;
	GHashTable *known_sources;
} TaskListsData;

static gboolean
google_backend_list_task_lists_cb (EGDataSession *gdata,
				   JsonObject *tasklist,
				   gpointer user_data)
{
	TaskListsData *tld = user_data;

	google_add_task_list (tld->collection,
		e_gdata_tasklist_get_id (tasklist),
		e_gdata_tasklist_get_title (tasklist),
		tld->known_sources);

	return TRUE;
}

static ESourceAuthenticationResult
google_backend_authenticate_sync (EBackend *backend,
				  const ENamedParameters *credentials,
				  gchar **out_certificate_pem,
				  GTlsCertificateFlags *out_certificate_errors,
				  GCancellable *cancellable,
				  GError **error)
{
	ECollectionBackend *collection = E_COLLECTION_BACKEND (backend);
	ESourceAuthentication *auth_extension = NULL;
	ESourceCollection *collection_extension;
	ESourceGoa *goa_extension = NULL;
	ESource *source;
	ESourceAuthenticationResult result = E_SOURCE_AUTHENTICATION_ERROR;
	GHashTable *known_sources;
	GList *sources;
	ENamedParameters *credentials_copy = NULL;
	const gchar *calendar_url;
	const gchar *contacts_url = NULL;

	g_return_val_if_fail (collection != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	source = e_backend_get_source (backend);
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
	if (e_source_has_extension (source, E_SOURCE_EXTENSION_GOA))
		goa_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_GOA);
	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION))
		auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	g_return_val_if_fail (e_source_collection_get_calendar_enabled (collection_extension) ||
		e_source_collection_get_contacts_enabled (collection_extension), E_SOURCE_AUTHENTICATION_ERROR);

	e_collection_backend_freeze_populate (collection);

	if (credentials && !e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME)) {
		credentials_copy = e_named_parameters_new_clone (credentials);
		e_named_parameters_set (credentials_copy, E_SOURCE_CREDENTIAL_USERNAME, e_source_collection_get_identity (collection_extension));
		credentials = credentials_copy;
	}

	/* resource-id => source's UID */
	known_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	sources = e_collection_backend_list_calendar_sources (collection);
	g_list_foreach (sources, google_add_task_list_uid_to_hashtable, known_sources);
	g_list_free_full (sources, g_object_unref);

	/* When the WebDAV extension is created, the auth method can be reset, thus ensure
	   it's there before setting correct authentication method on the master source. */
	(void) e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	google_backend_calendar_update_auth_method (collection, source, NULL);

	if (goa_extension) {
		calendar_url = e_source_goa_get_calendar_url (goa_extension);
	} else {
		calendar_url = "https://www.google.com/calendar/dav/";

		if (auth_extension) {
			gchar *method;

			method = e_source_authentication_dup_method (auth_extension);
			if (g_strcmp0 (method, GOOGLE_OAUTH2_METHOD) == 0)
				calendar_url = "https://apidata.googleusercontent.com/caldav/v2/";

			g_free (method);
		}
	}

	if (!e_source_collection_get_calendar_enabled (collection_extension))
		calendar_url = NULL;

	if (e_source_collection_get_contacts_enabled (collection_extension))
		contacts_url = "https://www.googleapis.com/.well-known/carddav";

	if (calendar_url || contacts_url) {
		result = e_webdav_collection_backend_discover_sync (E_WEBDAV_COLLECTION_BACKEND (backend), calendar_url, contacts_url,
			credentials, out_certificate_pem, out_certificate_errors, cancellable, error);
	} else {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	}

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED &&
	    e_source_collection_get_calendar_enabled (collection_extension) &&
	    (goa_extension || e_oauth2_services_is_supported ())) {
		EGDataSession *gdata;
		TaskListsData tld;
		GError *local_error = NULL;

		gdata = e_gdata_session_new (e_backend_get_source (backend));

		e_binding_bind_property (
			backend, "proxy-resolver",
			gdata, "proxy-resolver",
			G_BINDING_SYNC_CREATE);

		tld.collection = collection;
		tld.known_sources = known_sources;

		if (!e_gdata_session_tasklists_list_sync (gdata, NULL, google_backend_list_task_lists_cb, &tld, cancellable, &local_error))
			e_source_registry_debug_print ("%s: Failed to get tasks list: %s\n", G_STRFUNC, local_error ? local_error->message : "Unknown error");

		g_clear_object (&gdata);
		g_clear_error (&local_error);
	}

	if (result == E_SOURCE_AUTHENTICATION_ACCEPTED) {
		ESourceRegistryServer *server;

		server = e_collection_backend_ref_server (collection);

		if (server) {
			g_hash_table_foreach (known_sources, google_remove_unknown_sources_cb, server);
			g_object_unref (server);
		}
	}

	g_hash_table_destroy (known_sources);
	e_named_parameters_free (credentials_copy);

	e_collection_backend_thaw_populate (collection);

	return result;
}

static gboolean
google_backend_is_custom_source (EWebDAVCollectionBackend *webdav_backend,
				 ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), FALSE);

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST))
		return TRUE;

	/* Chain up to parent's method. */
	return E_WEBDAV_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->is_custom_source (webdav_backend, source);
}

static void
google_backend_populate (ECollectionBackend *backend)
{
	ESourceAuthentication *authentication_extension;
	ESource *source;

	source = e_backend_get_source (E_BACKEND (backend));
	authentication_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	/* When the WebDAV extension is created, the auth method can be reset, thus ensure
	   it's there before setting correct authentication method on the master source. */
	(void) e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);

	/* Force OAuth2 for Google accounts using external auth method and do it before calling
	   parent method, thus it knows what authentication method it's supposed to use */
	if (e_source_authentication_get_is_external (authentication_extension))
		e_source_authentication_set_method (authentication_extension, "OAuth2");

	/* Chain up to parent's method. */
	E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->populate (backend);
}

static gchar *
google_backend_dup_resource_id (ECollectionBackend *backend,
                                ESource *child_source)
{
	if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_CALENDAR) ||
	    e_source_has_extension (child_source, E_SOURCE_EXTENSION_MEMO_LIST) ||
	    e_source_has_extension (child_source, E_SOURCE_EXTENSION_TASK_LIST) ||
	    e_source_has_extension (child_source, E_SOURCE_EXTENSION_ADDRESS_BOOK))
		return E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->dup_resource_id (backend, child_source);

	return NULL;
}

static void
google_backend_child_added (ECollectionBackend *backend,
                            ESource *child_source)
{
	ESource *collection_source;
	const gchar *extension_name;
	gboolean is_mail = FALSE;

	/* Chain up to parent's child_added() method. */
	E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->
		child_added (backend, child_source);

	collection_source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	/* Synchronize mail-related user with the collection identity. */
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	if (is_mail && e_source_has_extension (child_source, extension_name)) {
		ESourceAuthentication *auth_child_extension;
		ESourceCollection *collection_extension;
		const gchar *collection_identity;
		const gchar *auth_child_user;

		extension_name = E_SOURCE_EXTENSION_COLLECTION;
		collection_extension = e_source_get_extension (
			collection_source, extension_name);
		collection_identity = e_source_collection_get_identity (
			collection_extension);

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		auth_child_extension = e_source_get_extension (
			child_source, extension_name);
		auth_child_user = e_source_authentication_get_user (
			auth_child_extension);

		/* XXX Do not override an existing user name setting.
		 *     The IMAP or (especially) SMTP configuration may
		 *     have been modified to use a non-Google server. */
		if (auth_child_user == NULL)
			e_source_authentication_set_user (
				auth_child_extension,
				collection_identity);

		if (e_source_has_extension (child_source, E_SOURCE_EXTENSION_MAIL_ACCOUNT) ||
		    e_source_has_extension (child_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT)) {
			google_backend_mail_update_auth_method (backend, child_source, collection_source);
			g_signal_connect (
				child_source, "notify::oauth2-support",
				G_CALLBACK (google_backend_mail_update_auth_method_cb),
				backend);
		}
	}

	/* Keep the calendar authentication method up-to-date.
	 *
	 * XXX Not using a property binding here in case I end up adding
	 *     other "support" interfaces which influence authentication.
	 *     Many-to-one property bindinds tend not to work so well. */
	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (child_source, extension_name)) {
		ESourceAlarms *alarms_extension;

		/* To not notify about past reminders. */
		alarms_extension = e_source_get_extension (child_source, E_SOURCE_EXTENSION_ALARMS);
		if (!e_source_alarms_get_last_notified (alarms_extension)) {
			GDateTime *today_dt;
			gchar *today;

			today_dt = g_date_time_new_now_utc ();
			today = g_date_time_format_iso8601 (today_dt);
			g_date_time_unref (today_dt);
			e_source_alarms_set_last_notified (alarms_extension, today);
			g_free (today);
		}

		google_backend_calendar_update_auth_method (backend, child_source, collection_source);
		g_signal_connect (
			child_source, "notify::oauth2-support",
			G_CALLBACK (google_backend_calendar_update_auth_method_cb),
			backend);
	}

	/* Keep the contacts authentication method up-to-date.
	 *
	 * XXX Not using a property binding here in case I end up adding
	 *     other "support" interfaces which influence authentication.
	 *     Many-to-one property bindings tend not to work so well. */
	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (child_source, extension_name)) {
		google_backend_contacts_update_auth_method (child_source, collection_source);
		g_signal_connect (
			child_source, "notify::oauth2-support",
			G_CALLBACK (google_backend_contacts_update_auth_method_cb),
			backend);
	}
}

static gboolean
google_backend_get_destination_address (EBackend *backend,
					gchar **host,
					guint16 *port)
{
	g_return_val_if_fail (host != NULL, FALSE);
	g_return_val_if_fail (port != NULL, FALSE);

	*host = g_strdup ("www.google.com");
	*port = 443;

	return TRUE;
}

static void
e_google_backend_class_init (EGoogleBackendClass *class)
{
	EBackendClass *backend_class;
	ECollectionBackendClass *collection_backend_class;
	EWebDAVCollectionBackendClass *webdav_collection_backend_class;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = google_backend_authenticate_sync;
	backend_class->get_destination_address = google_backend_get_destination_address;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = google_backend_populate;
	collection_backend_class->dup_resource_id = google_backend_dup_resource_id;
	collection_backend_class->child_added = google_backend_child_added;

	webdav_collection_backend_class = E_WEBDAV_COLLECTION_BACKEND_CLASS (class);
	webdav_collection_backend_class->is_custom_source = google_backend_is_custom_source;
}

static void
e_google_backend_class_finalize (EGoogleBackendClass *class)
{
}

static void
e_google_backend_init (EGoogleBackend *backend)
{
}

static void
google_backend_prepare_mail_account_source (ESource *source)
{
	ESourceCamel *camel_extension;
	ESourceExtension *extension;
	CamelSettings *settings;
	const gchar *backend_name;
	const gchar *extension_name;

	backend_name = GOOGLE_IMAP_BACKEND_NAME;

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), backend_name);

	extension_name = e_source_camel_get_extension_name (backend_name);
	camel_extension = e_source_get_extension (source, extension_name);
	settings = e_source_camel_get_settings (camel_extension);

	/* The "auth-mechanism" should be determined elsewhere. */

	camel_network_settings_set_host (
		CAMEL_NETWORK_SETTINGS (settings),
		GOOGLE_IMAP_HOST);

	camel_network_settings_set_port (
		CAMEL_NETWORK_SETTINGS (settings),
		GOOGLE_IMAP_PORT);

	camel_network_settings_set_security_method (
		CAMEL_NETWORK_SETTINGS (settings),
		GOOGLE_IMAP_SECURITY_METHOD);
}

static void
google_backend_prepare_mail_transport_source (ESource *source)
{
	ESourceCamel *camel_extension;
	ESourceExtension *extension;
	CamelSettings *settings;
	const gchar *backend_name;
	const gchar *extension_name;

	/* Configure the mail transport source. */

	backend_name = GOOGLE_SMTP_BACKEND_NAME;

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), backend_name);

	extension_name = e_source_camel_get_extension_name (backend_name);
	camel_extension = e_source_get_extension (source, extension_name);
	settings = e_source_camel_get_settings (camel_extension);

	/* The "auth-mechanism" should be determined elsewhere. */

	camel_network_settings_set_host (
		CAMEL_NETWORK_SETTINGS (settings),
		GOOGLE_SMTP_HOST);

	camel_network_settings_set_port (
		CAMEL_NETWORK_SETTINGS (settings),
		GOOGLE_SMTP_PORT);

	camel_network_settings_set_security_method (
		CAMEL_NETWORK_SETTINGS (settings),
		GOOGLE_SMTP_SECURITY_METHOD);
}

static void
google_backend_factory_prepare_mail (ECollectionBackendFactory *factory,
                                     ESource *mail_account_source,
                                     ESource *mail_identity_source,
                                     ESource *mail_transport_source)
{
	ECollectionBackendFactoryClass *parent_class;

	/* Chain up to parent's prepare_mail() method. */
	parent_class =
		E_COLLECTION_BACKEND_FACTORY_CLASS (
		e_google_backend_factory_parent_class);
	parent_class->prepare_mail (
		factory,
		mail_account_source,
		mail_identity_source,
		mail_transport_source);

	google_backend_prepare_mail_account_source (mail_account_source);
	google_backend_prepare_mail_transport_source (mail_transport_source);
}

static void
e_google_backend_factory_class_init (EGoogleBackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "google";
	factory_class->backend_type = E_TYPE_GOOGLE_BACKEND;
	factory_class->prepare_mail = google_backend_factory_prepare_mail;
}

static void
e_google_backend_factory_class_finalize (EGoogleBackendFactoryClass *class)
{
}

static void
e_google_backend_factory_init (EGoogleBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_google_backend_register_type (type_module);
	e_google_backend_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

