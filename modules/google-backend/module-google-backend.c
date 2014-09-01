/*
 * module-google-backend.c
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

#include <config.h>
#include <glib/gi18n-lib.h>

#include <libebackend/libebackend.h>

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

/* Calendar Configuration Details */
#define GOOGLE_CALENDAR_BACKEND_NAME	"caldav"
#define GOOGLE_CALENDAR_RESOURCE_ID	"Calendar"

/* CalDAV v1 Configuration Details */
#define GOOGLE_CALDAV_V1_HOST		"www.google.com"
#define GOOGLE_CALDAV_V1_PATH		"/calendar/dav/%s/events"

/* CalDAV v2 Configuration Details */
#define GOOGLE_CALDAV_V2_HOST		"apidata.googleusercontent.com"
#define GOOGLE_CALDAV_V2_PATH		"/caldav/v2/%s/events"

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
	ESourceWebdav *webdav_extension;
	const gchar *extension_name;
	const gchar *host;
	const gchar *method;
	const gchar *path_format;
	gchar *path;
	gchar *user;

	oauth2_support = e_server_side_source_ref_oauth2_support (
		E_SERVER_SIDE_SOURCE (source));

	/* The host name and WebDAV resource path depend on the
	 * authentication method used, so update those here too. */

	if (oauth2_support != NULL) {
		method = "OAuth2";
		host = GOOGLE_CALDAV_V2_HOST;
		path_format = GOOGLE_CALDAV_V2_PATH;
	} else {
		method = "plain/password";
		host = GOOGLE_CALDAV_V1_HOST;
		path_format = GOOGLE_CALDAV_V1_PATH;
	}

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);
	e_source_authentication_set_host (auth_extension, host);
	e_source_authentication_set_method (auth_extension, method);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	user = e_source_authentication_dup_user (auth_extension);
	path = g_strdup_printf (path_format, (user != NULL) ? user : "");
	e_source_webdav_set_resource_path (webdav_extension, path);
	g_free (path);
	g_free (user);

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
google_backend_add_calendar (ECollectionBackend *backend)
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

	/* NOTE: Host name and WebDAV resource path are set in
	 *       google_backend_calendar_update_auth_method(),
	 *       since they depend on the auth method used. */

	collection_source = e_backend_get_source (E_BACKEND (backend));

	resource_id = GOOGLE_CALENDAR_RESOURCE_ID;
	source = e_collection_backend_new_child (backend, resource_id);
	e_source_set_display_name (source, _("Calendar"));

	collection_extension = e_source_get_extension (
		collection_source, E_SOURCE_EXTENSION_COLLECTION);

	/* Configure the calendar source. */

	backend_name = GOOGLE_CALENDAR_BACKEND_NAME;

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), backend_name);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);

	g_object_bind_property (
		collection_extension, "identity",
		extension, "user",
		G_BINDING_SYNC_CREATE);

	/* Make sure the WebDAV resource path is up-to-date, since
	 * it's built from the "user" property that we just set. */
	google_backend_calendar_update_auth_method (source);

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	extension = e_source_get_extension (source, extension_name);

	e_source_security_set_secure (
		E_SOURCE_SECURITY (extension), TRUE);

	extension_name = E_SOURCE_EXTENSION_ALARMS;
	extension = e_source_get_extension (source, extension_name);
	if (!e_source_alarms_get_last_notified (E_SOURCE_ALARMS (extension))) {
		GTimeVal today_tv;
		gchar *today;

		g_get_current_time (&today_tv);
		today = g_time_val_to_iso8601 (&today_tv);
		e_source_alarms_set_last_notified (
			E_SOURCE_ALARMS (extension), today);
		g_free (today);
	}

	server = e_collection_backend_ref_server (backend);
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);

	g_object_unref (source);
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

	g_object_bind_property (
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

	g_object_bind_property (
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
	gboolean have_calendar = FALSE, have_tasks = FALSE;

	list = e_collection_backend_list_calendar_sources (backend);
	for (link = list; link; link = g_list_next (link)) {
		ESource *source = link->data;

		have_calendar = have_calendar || e_source_has_extension (source, E_SOURCE_EXTENSION_CALENDAR);
		have_tasks = have_tasks || e_source_has_extension (source, E_SOURCE_EXTENSION_TASK_LIST);

		if (have_calendar && have_tasks)
			break;
	}
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	if (!have_calendar)
		google_backend_add_calendar (backend);

#if GDATA_CHECK_VERSION(0,15,1)
	if (!have_tasks)
		google_backend_add_tasks (backend);
#endif

	list = e_collection_backend_list_contacts_sources (backend);
	if (list == NULL)
		google_backend_add_contacts (backend);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Chain up to parent's populate() method. */
	E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->
		populate (backend);
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
		return g_strdup (GOOGLE_CALENDAR_RESOURCE_ID);

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

	/* Chain up to parent's child_added() method. */
	E_COLLECTION_BACKEND_CLASS (e_google_backend_parent_class)->
		child_added (backend, child_source);
}

static void
e_google_backend_class_init (EGoogleBackendClass *class)
{
	ECollectionBackendClass *backend_class;

	backend_class = E_COLLECTION_BACKEND_CLASS (class);
	backend_class->populate = google_backend_populate;
	backend_class->dup_resource_id = google_backend_dup_resource_id;
	backend_class->child_added = google_backend_child_added;
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

