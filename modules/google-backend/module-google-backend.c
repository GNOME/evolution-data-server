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

#include <config.h>
#include <glib/gi18n-lib.h>

#include <libebackend/libebackend.h>

#include "e-webdav-discover.h"

#ifdef HAVE_GOOGLE
#include <gdata/gdata.h>
#endif

/* This macro was introduced in libgdata 0.11,
 * but we currently only require libgdata 0.10. */
#ifndef GDATA_CHECK_VERSION
#define GDATA_CHECK_VERSION(major,minor,micro) 0
#endif

/* Standard GObject macros */
#define E_TYPE_GOOGLE_BACKEND \
	(e_google_backend_get_type ())
#define E_GOOGLE_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GOOGLE_BACKEND, EGoogleBackend))

/* Just for readability... */
#define METHOD(x) (CAMEL_NETWORK_SECURITY_METHOD_##x)

/* IMAP Configuration Details */
#define GOOGLE_IMAP_BACKEND_NAME	"imapx"
#define GOOGLE_IMAP_HOST		"imap.gmail.com"
#define GOOGLE_IMAP_PORT		993
#define GOOGLE_IMAP_SECURITY_METHOD	METHOD (SSL_ON_ALTERNATE_PORT)

/* SMTP Configuration Details */
#define GOOGLE_SMTP_BACKEND_NAME	"smtp"
#define GOOGLE_SMTP_HOST		"smtp.gmail.com"
#define GOOGLE_SMTP_PORT		587
#define GOOGLE_SMTP_SECURITY_METHOD	METHOD (STARTTLS_ON_STANDARD_PORT)

/* Contacts Configuration Details */
#define GOOGLE_CONTACTS_BACKEND_NAME	"google"
#define GOOGLE_CONTACTS_HOST		"www.google.com"
#define GOOGLE_CONTACTS_RESOURCE_ID	"Contacts"

/* Tasks Configuration Details */
#define GOOGLE_TASKS_BACKEND_NAME	"gtasks"
#define GOOGLE_TASKS_RESOURCE_ID	"Tasks List"

typedef struct _EGoogleBackend EGoogleBackend;
typedef struct _EGoogleBackendClass EGoogleBackendClass;

typedef struct _EGoogleBackendFactory EGoogleBackendFactory;
typedef struct _EGoogleBackendFactoryClass EGoogleBackendFactoryClass;

struct _EGoogleBackend {
	ECollectionBackend parent;
};

struct _EGoogleBackendClass {
	ECollectionBackendClass parent_class;
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
	E_TYPE_COLLECTION_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EGoogleBackendFactory,
	e_google_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
google_backend_calendar_update_auth_method (ESource *source)
{
	EOAuth2Support *oauth2_support;
	ESourceAuthentication *auth_extension;
	const gchar *method;

	oauth2_support = e_server_side_source_ref_oauth2_support (E_SERVER_SIDE_SOURCE (source));

	/* The host name and WebDAV resource path depend on the
	 * authentication method used, so update those here too. */

	if (oauth2_support != NULL) {
		method = "OAuth2";
	} else {
		method = "plain/password";
	}

	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_authentication_set_method (auth_extension, method);

	g_clear_object (&oauth2_support);
}

static void
google_backend_contacts_update_auth_method (ESource *source)
{
	EOAuth2Support *oauth2_support;
	ESourceAuthentication *extension;
	const gchar *extension_name;
	const gchar *method;

	oauth2_support = e_server_side_source_ref_oauth2_support (
		E_SERVER_SIDE_SOURCE (source));

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);
	method = (oauth2_support != NULL) ? "OAuth2" : "ClientLogin";
	e_source_authentication_set_method (extension, method);

	g_clear_object (&oauth2_support);
}

static void
google_add_uid_to_hashtable (gpointer source,
			     gpointer known_sources)
{
	ESourceResource *resource;
	gchar *uid, *rid;

	if (!e_source_has_extension (source, E_SOURCE_EXTENSION_RESOURCE))
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
google_add_found_source (ECollectionBackend *collection,
			   EWebDAVDiscoverSupports source_type,
			   SoupURI *uri,
			   const gchar *display_name,
			   const gchar *color,
			   GHashTable *known_sources)
{
	ESourceRegistryServer *server;
	ESourceBackend *backend;
	ESource *source = NULL;
	const gchar *backend_name = NULL;
	const gchar *provider = NULL;
	const gchar *identity_prefix = NULL;
	const gchar *source_uid;
	gboolean is_new;
	gchar *url;
	gchar *identity;

	g_return_if_fail (collection != NULL);
	g_return_if_fail (uri != NULL);
	g_return_if_fail (display_name != NULL);
	g_return_if_fail (known_sources != NULL);

	switch (source_type) {
	case E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS:
		backend_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
		provider = "webdav";
		identity_prefix = "contacts";
		break;
	case E_WEBDAV_DISCOVER_SUPPORTS_EVENTS:
		backend_name = E_SOURCE_EXTENSION_CALENDAR;
		provider = "caldav";
		identity_prefix = "events";
		break;
	case E_WEBDAV_DISCOVER_SUPPORTS_MEMOS:
		backend_name = E_SOURCE_EXTENSION_MEMO_LIST;
		provider = "caldav";
		identity_prefix = "memos";
		break;
	case E_WEBDAV_DISCOVER_SUPPORTS_TASKS:
		backend_name = E_SOURCE_EXTENSION_TASK_LIST;
		provider = "caldav";
		identity_prefix = "tasks";
		break;
	default:
		g_warn_if_reached ();
		return;
	}

	g_return_if_fail (backend_name != NULL);

	server = e_collection_backend_ref_server (collection);

	url = soup_uri_to_string (uri, FALSE);
	identity = g_strconcat (identity_prefix, "::", url, NULL);
	source_uid = g_hash_table_lookup (known_sources, identity);
	is_new = !source_uid;
	if (is_new) {
		ESource *master_source;

		source = e_collection_backend_new_child (collection, identity);
		g_warn_if_fail (source != NULL);

		if (source) {
			ESourceCollection *collection_extension;
			ESourceAuthentication *child_auth;
			ESourceResource *resource;
			ESourceWebdav *master_webdav, *child_webdav;

			master_source = e_backend_get_source (E_BACKEND (collection));
			master_webdav = e_source_get_extension (master_source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			collection_extension = e_source_get_extension (master_source, E_SOURCE_EXTENSION_COLLECTION);
			child_auth = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
			child_webdav = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
			resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);

			e_source_authentication_set_user (child_auth, e_source_collection_get_identity (collection_extension));
			e_source_webdav_set_soup_uri (child_webdav, uri);
			e_source_resource_set_identity (resource, identity);

			/* inherit ssl trust options */
			e_source_webdav_set_ssl_trust (child_webdav, e_source_webdav_get_ssl_trust (master_webdav));
		}
	} else {
		source = e_source_registry_server_ref_source (server, source_uid);
		g_warn_if_fail (source != NULL);

		g_hash_table_remove (known_sources, identity);
	}

	g_free (identity);
	g_free (url);

	/* these properties are synchronized always */
	if (source) {
		gint rr, gg, bb;

		backend = e_source_get_extension (source, backend_name);
		e_source_backend_set_backend_name (backend, provider);

		e_source_set_display_name (source, display_name);
		e_source_set_enabled (source, TRUE);

		/* Also check whether the color format is as expected; it cannot
		   be used gdk_rgba_parse here, because it required gdk/gtk. */
		if (is_new && source_type != E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS && color &&
		    sscanf (color, "#%02x%02x%02x", &rr, &gg, &bb) == 3) {
			gchar *safe_color;

			/* In case an #RRGGBBAA is returned */
			safe_color = g_strdup_printf ("#%02x%02x%02x", rr, gg, bb);

			e_source_selectable_set_color (E_SOURCE_SELECTABLE (backend), safe_color);

			g_free (safe_color);
		}

		if (is_new)
			e_source_registry_server_add_source (server, source);

		g_object_unref (source);
	}

	g_object_unref (server);
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
	ESourceCollection *collection_extension;
	ESourceGoa *goa_extension = NULL;
	ESource *source;
	ESourceAuthenticationResult result;
	GHashTable *known_sources;
	GList *sources;
	GSList *discovered_sources = NULL;
	ENamedParameters *credentials_copy = NULL;
	const gchar *calendar_url;
	gboolean any_success = FALSE;
	GError *local_error = NULL;

	g_return_val_if_fail (collection != NULL, E_SOURCE_AUTHENTICATION_ERROR);

	source = e_backend_get_source (backend);
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);
	if (e_source_has_extension (source, E_SOURCE_EXTENSION_GOA))
		goa_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_GOA);

	g_return_val_if_fail (e_source_collection_get_calendar_enabled (collection_extension) ||
		e_source_collection_get_contacts_enabled (collection_extension), E_SOURCE_AUTHENTICATION_ERROR);

	if (credentials && !e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME)) {
		credentials_copy = e_named_parameters_new_clone (credentials);
		e_named_parameters_set (credentials_copy, E_SOURCE_CREDENTIAL_USERNAME, e_source_collection_get_identity (collection_extension));
		credentials = credentials_copy;
	}

	/* resource-id => source's UID */
	known_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	sources = e_collection_backend_list_calendar_sources (collection);
	g_list_foreach (sources, google_add_uid_to_hashtable, known_sources);
	g_list_free_full (sources, g_object_unref);

	google_backend_calendar_update_auth_method (source);

	if (goa_extension)
		calendar_url = e_source_goa_get_calendar_url (goa_extension);
	else
		calendar_url = "https://www.google.com/calendar/dav/";

	if (e_source_collection_get_calendar_enabled (collection_extension) && calendar_url &&
	    e_webdav_discover_sources_sync (source, calendar_url, E_WEBDAV_DISCOVER_SUPPORTS_NONE,
		credentials, out_certificate_pem, out_certificate_errors,
		&discovered_sources, NULL, cancellable, &local_error)) {
		EWebDAVDiscoverSupports source_types[] = {
			E_WEBDAV_DISCOVER_SUPPORTS_EVENTS,
			E_WEBDAV_DISCOVER_SUPPORTS_MEMOS,
			E_WEBDAV_DISCOVER_SUPPORTS_TASKS
		};
		GSList *link;
		gint ii;

		for (link = discovered_sources; link; link = g_slist_next (link)) {
			EWebDAVDiscoveredSource *discovered_source = link->data;
			SoupURI *soup_uri;

			if (!discovered_source || !discovered_source->href || !discovered_source->display_name)
				continue;

			soup_uri = soup_uri_new (discovered_source->href);
			if (!soup_uri)
				continue;

			for (ii = 0; ii < G_N_ELEMENTS (source_types); ii++) {
				if ((discovered_source->supports & source_types[ii]) == source_types[ii])
					google_add_found_source (collection, source_types[ii], soup_uri,
						discovered_source->display_name, discovered_source->color, known_sources);
			}

			soup_uri_free (soup_uri);
		}

		any_success = TRUE;
	}

	if (any_success) {
		ESourceRegistryServer *server;

		server = e_collection_backend_ref_server (collection);

		g_hash_table_foreach (known_sources, google_remove_unknown_sources_cb, server);

		g_object_unref (server);
	}

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
		e_collection_backend_authenticate_children (collection, credentials);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED) ||
		   g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_FORBIDDEN)) {
		result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_clear_error (&local_error);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
		result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
		g_propagate_error (error, local_error);
	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	g_hash_table_destroy (known_sources);
	e_named_parameters_free (credentials_copy);

	return result;
}

#if GDATA_CHECK_VERSION(0,15,1)
static void
google_backend_add_tasks (ECollectionBackend *backend)
{
	ESource *source;
	ESource *collection_source;
	ESourceRegistryServer *server;
	ESourceExtension *extension;
	ESourceCollection *collection_extension;
	const gchar *backend_name;
	const gchar *extension_name;
	const gchar *resource_id;

	/* FIXME As a future enhancement, we should query Google
	 *       for a list of user calendars and add them to the
	 *       collection with matching display names and colors. */

	collection_source = e_backend_get_source (E_BACKEND (backend));

	/* Tasks require OAuth2, which is supported only through GOA */
	if (!e_source_has_extension (collection_source, E_SOURCE_EXTENSION_GOA))
		return;

	resource_id = GOOGLE_TASKS_RESOURCE_ID;
	source = e_collection_backend_new_child (backend, resource_id);
	e_source_set_display_name (source, _("Tasks"));

	collection_extension = e_source_get_extension (
		collection_source, E_SOURCE_EXTENSION_COLLECTION);

	/* Configure the calendar source. */

	backend_name = GOOGLE_TASKS_BACKEND_NAME;

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), backend_name);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);

	e_source_authentication_set_host (E_SOURCE_AUTHENTICATION (extension), "www.google.com");
	e_source_authentication_set_method (E_SOURCE_AUTHENTICATION (extension), "OAuth2");

	e_binding_bind_property (
		collection_extension, "identity",
		extension, "user",
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_ALARMS;
	extension = e_source_get_extension (source, extension_name);
	e_source_alarms_set_include_me (E_SOURCE_ALARMS (extension), FALSE);

	server = e_collection_backend_ref_server (backend);
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);

	g_object_unref (source);
}
#endif /* GDATA_CHECK_VERSION(0,15,1) */

static void
google_backend_add_contacts (ECollectionBackend *backend)
{
	ESource *source;
	ESource *collection_source;
	ESourceRegistryServer *server;
	ESourceExtension *extension;
	ESourceCollection *collection_extension;
	const gchar *backend_name;
	const gchar *extension_name;
	const gchar *resource_id;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	resource_id = GOOGLE_CONTACTS_RESOURCE_ID;
	source = e_collection_backend_new_child (backend, resource_id);
	e_source_set_display_name (source, _("Contacts"));

	/* Add the address book source to the collection. */
	collection_extension = e_source_get_extension (
		collection_source, E_SOURCE_EXTENSION_COLLECTION);

	/* Configure the address book source. */

	backend_name = GOOGLE_CONTACTS_BACKEND_NAME;

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), backend_name);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);

	e_source_authentication_set_host (
		E_SOURCE_AUTHENTICATION (extension),
		GOOGLE_CONTACTS_HOST);

	e_binding_bind_property (
		collection_extension, "identity",
		extension, "user",
		G_BINDING_SYNC_CREATE);

	server = e_collection_backend_ref_server (backend);
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);

	g_object_unref (source);
}

static void
google_backend_populate (ECollectionBackend *backend)
{
	GList *list, *link;
	gboolean have_tasks = FALSE;
	ESourceRegistryServer *server;
	ESourceCollection *collection_extension;
	ESource *source;

	server = e_collection_backend_ref_server (backend);
	list = e_collection_backend_claim_all_resources (backend);
	for (link = list; link; link = g_list_next (link)) {
		ESource *source = link->data;

		if (e_source_has_extension (source, E_SOURCE_EXTENSION_RESOURCE)) {
			ESourceResource *resource;
			ESource *child;

			resource = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
			child = e_collection_backend_new_child (backend, e_source_resource_get_identity (resource));
			if (child) {
				e_source_registry_server_add_source (server, source);
				g_object_unref (child);
			}
		}
	}

	g_list_free_full (list, g_object_unref);
	g_object_unref (server);

	list = e_collection_backend_list_calendar_sources (backend);
	for (link = list; link && !have_tasks; link = g_list_next (link)) {
		ESource *source = link->data;

		have_tasks = have_tasks || e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST);
		if (have_tasks) {
			source = e_backend_get_source (E_BACKEND (backend));

			/* Tasks require OAuth2, which is supported only through GOA */
			if (!e_source_has_extension (source, E_SOURCE_EXTENSION_GOA)) {
				e_source_remove_sync (source, NULL, NULL);
				have_tasks = FALSE;
			}
		}
	}
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

#if GDATA_CHECK_VERSION(0,15,1)
	if (!have_tasks)
		google_backend_add_tasks (backend);
#endif

	list = e_collection_backend_list_contacts_sources (backend);
	if (list == NULL)
		google_backend_add_contacts (backend);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Chain up to parent's populate() method. */
	E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->populate (backend);

	source = e_backend_get_source (E_BACKEND (backend));
	collection_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	if (e_source_collection_get_calendar_enabled (collection_extension)) {
		e_backend_schedule_credentials_required (E_BACKEND (backend),
			E_SOURCE_CREDENTIALS_REASON_REQUIRED, NULL, 0, NULL, NULL, G_STRFUNC);
	}
}

static gchar *
google_backend_dup_resource_id (ECollectionBackend *backend,
                                ESource *child_source)
{
	const gchar *extension_name;

	/* XXX This is trivial for now since we only
	 *     add one calendar and one address book. */

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (child_source, extension_name))
		return E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->dup_resource_id (backend, child_source);

	extension_name = E_SOURCE_EXTENSION_TASK_LIST;
	if (e_source_has_extension (child_source, extension_name))
		return g_strdup (GOOGLE_TASKS_RESOURCE_ID);

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (child_source, extension_name))
		return g_strdup (GOOGLE_CONTACTS_RESOURCE_ID);

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
			GTimeVal today_tv;
			gchar *today;

			g_get_current_time (&today_tv);
			today = g_time_val_to_iso8601 (&today_tv);
			e_source_alarms_set_last_notified (alarms_extension, today);
			g_free (today);
		}

		google_backend_calendar_update_auth_method (child_source);
		g_signal_connect (
			child_source, "notify::oauth2-support",
			G_CALLBACK (google_backend_calendar_update_auth_method),
			NULL);
	}

	/* Keep the contacts authentication method up-to-date.
	 *
	 * XXX Not using a property binding here in case I end up adding
	 *     other "support" interfaces which influence authentication.
	 *     Many-to-one property bindings tend not to work so well. */
	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	if (e_source_has_extension (child_source, extension_name)) {
		google_backend_contacts_update_auth_method (child_source);
		g_signal_connect (
			child_source, "notify::oauth2-support",
			G_CALLBACK (google_backend_contacts_update_auth_method),
			NULL);
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

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = google_backend_authenticate_sync;
	backend_class->get_destination_address = google_backend_get_destination_address;

	collection_backend_class = E_COLLECTION_BACKEND_CLASS (class);
	collection_backend_class->populate = google_backend_populate;
	collection_backend_class->dup_resource_id = google_backend_dup_resource_id;
	collection_backend_class->child_added = google_backend_child_added;
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

