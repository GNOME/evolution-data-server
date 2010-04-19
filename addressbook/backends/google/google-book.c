/* goggle-book.c - Google contact list abstraction with caching.
 *
 * Copyright (C) 2008 Joergen Scheibengruber
 * Copyright (C) 2010 Philip Withnall
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Joergen Scheibengruber <joergen.scheibengruber AT googlemail.com>,
 * Philip Withnall <philip@tecnocode.co.uk>
 */

#include <string.h>
#include <libedata-book/e-book-backend-cache.h>
#include <libedataserver/e-proxy.h>
#include <gdata/gdata-service.h>
#include <gdata/services/contacts/gdata-contacts-service.h>
#include <gdata/services/contacts/gdata-contacts-query.h>
#include <gdata/services/contacts/gdata-contacts-contact.h>

#include "util.h"
#include "google-book.h"

G_DEFINE_TYPE (GoogleBook, google_book, G_TYPE_OBJECT)

#define URI_GET_CONTACTS "://www.google.com/m8/feeds/contacts/default/full"

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_GOOGLE_BOOK, GoogleBookPrivate))

typedef struct _GoogleBookPrivate GoogleBookPrivate;

enum {
	PROP_NONE,

	PROP_USERNAME,
	PROP_USE_CACHE,
	PROP_REFRESH_INTERVAL,
	PROP_USE_SSL
};

enum {
	CONTACT_ADDED,
	CONTACT_CHANGED,
	CONTACT_REMOVED,
	SEQUENCE_COMPLETE,
	AUTH_REQUIRED,

	LAST_SIGNAL
};

static guint google_book_signals [LAST_SIGNAL];

typedef enum {
	NO_CACHE,
	ON_DISK_CACHE,
	IN_MEMORY_CACHE
} CacheType;

struct _GoogleBookPrivate {
	gchar *username;
	CacheType cache_type;
	union {
		EBookBackendCache *on_disk;
		struct {
			GHashTable *contacts;
			GHashTable *gdata_entries;
			GTimeVal last_updated;
		} in_memory;
	} cache;

	gboolean offline;
	GDataService *service;
	EProxy *proxy;
	guint refresh_interval;
	gboolean use_ssl;

	gboolean live_mode;

	/* In live mode we will send out signals in an idle_handler */
	guint idle_id;

	guint refresh_id;
};

static gboolean google_book_get_new_contacts_in_chunks (GoogleBook *book, gint chunk_size, GError **error);

static void
google_book_cache_init (GoogleBook *book, gboolean on_disk)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	if (on_disk) {
		priv->cache_type = ON_DISK_CACHE;
		priv->cache.on_disk = e_book_backend_cache_new (priv->username);
	} else {
		priv->cache_type = IN_MEMORY_CACHE;
		priv->cache.in_memory.contacts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		priv->cache.in_memory.gdata_entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		memset (&priv->cache.in_memory.last_updated, 0, sizeof (GTimeVal));
	}
}

static EContact *
google_book_cache_add_contact (GoogleBook *book, GDataEntry *entry)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	EContact *contact;
	const gchar *uid;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		contact = _e_contact_new_from_gdata_entry (entry);
		_e_contact_add_gdata_entry_xml (contact, entry);
		e_book_backend_cache_add_contact (priv->cache.on_disk, contact);
		_e_contact_remove_gdata_entry_xml (contact);
		return contact;
	case IN_MEMORY_CACHE:
		contact = _e_contact_new_from_gdata_entry (entry);
		uid = e_contact_get_const (contact, E_CONTACT_UID);
		g_hash_table_insert (priv->cache.in_memory.contacts, g_strdup (uid), g_object_ref (contact));
		g_hash_table_insert (priv->cache.in_memory.gdata_entries, g_strdup (uid), g_object_ref (entry));
		return contact;
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static gboolean
google_book_cache_remove_contact (GoogleBook *book, const gchar *uid)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	gboolean success = TRUE;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		return e_book_backend_cache_remove_contact (priv->cache.on_disk, uid);
	case IN_MEMORY_CACHE:
		success = g_hash_table_remove (priv->cache.in_memory.contacts, uid);
		return success && g_hash_table_remove (priv->cache.in_memory.gdata_entries, uid);
	case NO_CACHE:
	default:
		break;
	}

	return FALSE;
}

static gboolean
google_book_cache_has_contact (GoogleBook *book, const gchar *uid)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		return e_book_backend_cache_check_contact (priv->cache.on_disk, uid);
	case IN_MEMORY_CACHE:
		return g_hash_table_lookup (priv->cache.in_memory.contacts, uid) ? TRUE : FALSE;
	case NO_CACHE:
	default:
		break;
	}

	return FALSE;
}

static EContact *
google_book_cache_get_contact (GoogleBook *book, const gchar *uid, GDataEntry **entry)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	EContact *contact;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		contact = e_book_backend_cache_get_contact (priv->cache.on_disk, uid);
		if (contact) {
			if (entry) {
				const gchar *entry_xml, *edit_link;

				entry_xml = _e_contact_get_gdata_entry_xml (contact, &edit_link);
				*entry = GDATA_ENTRY (gdata_parsable_new_from_xml (GDATA_TYPE_CONTACTS_CONTACT, entry_xml, -1, NULL));

				if (*entry) {
					GDataLink *link = gdata_link_new (edit_link, GDATA_LINK_EDIT);
					gdata_entry_add_link (*entry, link);
					g_object_unref (link);
				}
			}

			_e_contact_remove_gdata_entry_xml (contact);
		}
		return contact;
	case IN_MEMORY_CACHE:
		contact = g_hash_table_lookup (priv->cache.in_memory.contacts, uid);
		if (entry) {
			*entry = g_hash_table_lookup (priv->cache.in_memory.gdata_entries, uid);
			if (*entry)
				g_object_ref (*entry);
		}

		if (contact)
			g_object_ref (contact);

		return contact;
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static GList *
_g_hash_table_to_list (GHashTable *ht)
{
	GList *l = NULL;
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, ht);
	while (g_hash_table_iter_next (&iter, &key, &value))
		l = g_list_prepend (l, g_object_ref (G_OBJECT (value)));

	l = g_list_reverse (l);

	return l;
}

static GList *
google_book_cache_get_contacts (GoogleBook *book)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	GList *contacts, *iter;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		contacts = e_book_backend_cache_get_contacts (priv->cache.on_disk, "(contains \"x-evolution-any-field\" \"\")");
		for (iter = contacts; iter; iter = iter->next)
			_e_contact_remove_gdata_entry_xml (iter->data);

		return contacts;
	case IN_MEMORY_CACHE:
		return _g_hash_table_to_list (priv->cache.in_memory.contacts);
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static void
google_book_cache_freeze (GoogleBook *book)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	if (priv->cache_type == ON_DISK_CACHE)
		e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache.on_disk));
}

static void
google_book_cache_thaw (GoogleBook *book)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	if (priv->cache_type == ON_DISK_CACHE)
		e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache.on_disk));
}

static gchar *
google_book_cache_get_last_update (GoogleBook *book)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		return e_book_backend_cache_get_time (priv->cache.on_disk);
	case IN_MEMORY_CACHE:
		if (priv->cache.in_memory.contacts)
			return g_time_val_to_iso8601 (&priv->cache.in_memory.last_updated);
		break;
	case NO_CACHE:
	default:
		break;
	}

	return NULL;
}

static gboolean
google_book_cache_get_last_update_tv (GoogleBook *book, GTimeVal *tv)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	gchar *last_update;
	gint rv;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		last_update = e_book_backend_cache_get_time (priv->cache.on_disk);
		rv = last_update ? g_time_val_from_iso8601 (last_update, tv) : FALSE;
		g_free (last_update);
		return rv;
	case IN_MEMORY_CACHE:
		memcpy (tv, &priv->cache.in_memory.last_updated, sizeof (GTimeVal));
		return priv->cache.in_memory.contacts != NULL;
	case NO_CACHE:
	default:
		break;
	}

	return FALSE;
}

static void
google_book_cache_set_last_update (GoogleBook *book, GTimeVal *tv)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	gchar *_time;

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		_time = g_time_val_to_iso8601 (tv);
		/* Work around a bug in EBookBackendCache */
		e_file_cache_remove_object (E_FILE_CACHE (priv->cache.on_disk), "last_update_time");
		e_book_backend_cache_set_time (priv->cache.on_disk, _time);
		g_free (_time);
		return;
	case IN_MEMORY_CACHE:
		memcpy (&priv->cache.in_memory.last_updated, tv, sizeof (GTimeVal));
	case NO_CACHE:
	default:
		break;
	}
}

static gboolean
google_book_cache_needs_update (GoogleBook *book, guint *remaining_secs)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	GTimeVal last, current;
	guint diff;
	gboolean rv;

	if (remaining_secs)
		*remaining_secs = G_MAXUINT;

	/* We never want to update in offline mode */
	if (priv->offline)
		return FALSE;

	rv = google_book_cache_get_last_update_tv (book, &last);

	if (!rv)
		return TRUE;

	g_get_current_time (&current);
	if (last.tv_sec > current.tv_sec) {
		g_warning ("last update is in the feature?");

		/* Do an update so we can fix this */
		return TRUE;
	}
	diff = current.tv_sec - last.tv_sec;

	if (diff >= priv->refresh_interval)
		return TRUE;

	if (remaining_secs)
		*remaining_secs = priv->refresh_interval - diff;

	__debug__ ("No update needed. Next update needed in %d secs", priv->refresh_interval - diff);

	return FALSE;
}

static gboolean on_refresh_timeout (gpointer user_data);

static gboolean
google_book_cache_refresh_if_needed (GoogleBook *book, GError **error)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);
	guint remaining_secs;
	gint rv = TRUE;
	gboolean install_timeout;

	__debug__ (G_STRFUNC);

	if (priv->offline || !priv->service) {
		__debug__ ("We are not connected to Google%s.", priv->offline ? " (offline mode)" : "");
		return TRUE;
	}

	install_timeout = (priv->live_mode) && (priv->refresh_interval > 0) && (0 == priv->refresh_id);

	if (google_book_cache_needs_update (book, &remaining_secs)) {
		rv = google_book_get_new_contacts_in_chunks (book, 32, error);
		if (install_timeout)
			priv->refresh_id = g_timeout_add_seconds (priv->refresh_interval, on_refresh_timeout, book);
	} else {
		if (install_timeout) {
			__debug__ ("Installing timeout with %d seconds", remaining_secs);
			priv->refresh_id = g_timeout_add_seconds (remaining_secs, on_refresh_timeout, book);
		}
	}
	return rv;
}

static gboolean
on_refresh_timeout (gpointer user_data)
{
	GoogleBook *book = user_data;
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);

	priv->refresh_id = 0;
	if (priv->live_mode)
		google_book_cache_refresh_if_needed (book, NULL);

	return FALSE;
}

static void
google_book_cache_destroy (GoogleBook *book)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	switch (priv->cache_type) {
	case ON_DISK_CACHE:
		g_object_unref (priv->cache.on_disk);
		break;
	case IN_MEMORY_CACHE:
		g_hash_table_destroy (priv->cache.in_memory.contacts);
		g_hash_table_destroy (priv->cache.in_memory.gdata_entries);
		break;
	case NO_CACHE:
	default:
		break;
	}

	priv->cache_type = NO_CACHE;
}

static void
google_book_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	GoogleBookPrivate *priv = GET_PRIVATE (object);

	switch (property_id) {
	case PROP_USERNAME:
		g_value_set_string (value, priv->username);
		break;
	case PROP_USE_CACHE:
		g_value_set_boolean (value, (priv->cache_type == ON_DISK_CACHE));
		break;
	case PROP_REFRESH_INTERVAL:
		g_value_set_uint (value, priv->refresh_interval);
		break;
	case PROP_USE_SSL:
		g_value_set_boolean (value, priv->use_ssl);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
google_book_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	GoogleBookPrivate *priv = GET_PRIVATE (object);

	switch (property_id) {
	case PROP_USERNAME:
		priv->username = g_value_dup_string (value);
		break;
	case PROP_USE_CACHE:
		google_book_cache_init (GOOGLE_BOOK (object), g_value_get_boolean (value));
		break;
	case PROP_REFRESH_INTERVAL:
		priv->refresh_interval = g_value_get_uint (value);

		/* Remove and re-add the timeout */
		if (priv->refresh_id != 0) {
			g_source_remove (priv->refresh_id);
			priv->refresh_id = g_timeout_add_seconds (priv->refresh_interval, on_refresh_timeout, GOOGLE_BOOK (object));
		}
		break;
	case PROP_USE_SSL:
		priv->use_ssl = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
google_book_dispose (GObject *object)
{
	GoogleBookPrivate *priv = GET_PRIVATE (object);

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	if (priv->service)
		g_object_unref (priv->service);
	priv->service = NULL;

	if (priv->proxy)
		g_object_unref (priv->proxy);
	priv->proxy = NULL;

	google_book_cache_destroy (GOOGLE_BOOK (object));

	if (G_OBJECT_CLASS (google_book_parent_class)->dispose)
		G_OBJECT_CLASS (google_book_parent_class)->dispose (object);
}

static void
google_book_finalize (GObject *object)
{
	GoogleBookPrivate *priv = GET_PRIVATE (object);

	g_free (priv->username);

	if (G_OBJECT_CLASS (google_book_parent_class)->finalize)
		G_OBJECT_CLASS (google_book_parent_class)->finalize (object);
}

static void
google_book_emit_contact_added (GoogleBook *book, EContact *contact)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	if (priv->live_mode)
		g_signal_emit (book, google_book_signals [CONTACT_ADDED], 0, contact);
}

static void
google_book_emit_contact_changed (GoogleBook *book, EContact *contact)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	if (priv->live_mode)
		g_signal_emit (book, google_book_signals [CONTACT_CHANGED], 0, contact);
}

static void
google_book_emit_contact_removed (GoogleBook *book, const gchar *uid)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	if (priv->live_mode)
		g_signal_emit (book, google_book_signals [CONTACT_REMOVED], 0, uid);
}

static void
google_book_emit_sequence_complete (GoogleBook *book, GError *error)
{
	GoogleBookPrivate *priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	if (priv->live_mode)
		g_signal_emit (book, google_book_signals [SEQUENCE_COMPLETE], 0, error);
}

static void
google_book_emit_auth_required (GoogleBook *book)
{
	__debug__ (G_STRFUNC);
	g_signal_emit (book, google_book_signals [AUTH_REQUIRED], 0);
}

static void
google_book_class_init (GoogleBookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (GoogleBookPrivate));

	object_class->get_property = google_book_get_property;
	object_class->set_property = google_book_set_property;
	object_class->dispose = google_book_dispose;
	object_class->finalize = google_book_finalize;

	g_object_class_install_property (object_class, PROP_USERNAME,
	                                 g_param_spec_string ("username",
	                                                      "Username",
	                                                      "The username.",
	                                                      NULL,
	                                                      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_USE_CACHE,
	                                 g_param_spec_boolean ("use-cache",
	                                                       "Use Cache?",
	                                                       "Whether an on-disk cache should be used.",
	                                                       TRUE,
	                                                       G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_REFRESH_INTERVAL,
	                                 g_param_spec_uint ("refresh-interval",
	                                                    "Refresh Interval",
	                                                    "Specifies the number of seconds until the local cache is updated from the "
	                                                    "server. 0 means no updates.",
	                                                    0, G_MAXUINT, 3600,
	                                                    G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_USE_SSL,
	                                 g_param_spec_boolean ("use-ssl",
	                                                       "Use SSL?",
	                                                       "Whether SSL should be used.",
	                                                       TRUE,
	                                                       G_PARAM_READWRITE));

	google_book_signals [CONTACT_CHANGED] = g_signal_new ("contact-changed",
	                                                      G_OBJECT_CLASS_TYPE (object_class),
	                                                      G_SIGNAL_RUN_LAST,
	                                                      G_STRUCT_OFFSET (GoogleBookClass, contact_changed),
	                                                      NULL, NULL,
	                                                      g_cclosure_marshal_VOID__POINTER,
	                                                      G_TYPE_NONE, 1,
	                                                      G_TYPE_POINTER);

	google_book_signals [CONTACT_ADDED] = g_signal_new ("contact-added",
	                                                    G_OBJECT_CLASS_TYPE (object_class),
	                                                    G_SIGNAL_RUN_LAST,
	                                                    G_STRUCT_OFFSET (GoogleBookClass, contact_added),
	                                                    NULL, NULL,
	                                                    g_cclosure_marshal_VOID__POINTER,
	                                                    G_TYPE_NONE, 1,
	                                                    G_TYPE_POINTER);

	google_book_signals [CONTACT_REMOVED] = g_signal_new ("contact-removed",
	                                                      G_OBJECT_CLASS_TYPE (object_class),
	                                                      G_SIGNAL_RUN_LAST,
	                                                      G_STRUCT_OFFSET (GoogleBookClass, contact_removed),
	                                                      NULL, NULL,
	                                                      g_cclosure_marshal_VOID__POINTER,
	                                                      G_TYPE_NONE, 1,
	                                                      G_TYPE_POINTER);

	google_book_signals [SEQUENCE_COMPLETE] = g_signal_new ("sequence-complete",
	                                                        G_OBJECT_CLASS_TYPE (object_class),
	                                                        G_SIGNAL_RUN_LAST,
	                                                        G_STRUCT_OFFSET (GoogleBookClass, sequence_complete),
	                                                        NULL, NULL,
	                                                        g_cclosure_marshal_VOID__POINTER,
	                                                        G_TYPE_NONE, 1,
	                                                        G_TYPE_POINTER);

	google_book_signals [AUTH_REQUIRED] = g_signal_new ("auth-required",
	                                                    G_OBJECT_CLASS_TYPE (object_class),
	                                                    G_SIGNAL_RUN_LAST,
	                                                    G_STRUCT_OFFSET (GoogleBookClass, auth_required),
	                                                    NULL, NULL,
	                                                    g_cclosure_marshal_VOID__VOID,
	                                                    G_TYPE_NONE, 0);
}

static void
google_book_init (GoogleBook *self)
{
	__debug__ (G_STRFUNC);
}

GoogleBook *
google_book_new (const gchar *username, gboolean use_cache)
{
	return g_object_new (TYPE_GOOGLE_BOOK,
	                     "username", username,
	                     "use-cache", use_cache,
	                     "use-ssl", TRUE,
	                     "refresh-interval", 3600,
	                     NULL);
}

static void
proxy_settings_changed (EProxy *proxy, gpointer user_data)
{
	SoupURI *proxy_uri = NULL;
	gchar *uri;
	GoogleBookPrivate *priv = (GoogleBookPrivate*) user_data;

	if (!priv || !priv->service)
		return;

	/* Build the URI which libgdata would use to query contacts */
	uri = g_strconcat (priv->use_ssl ? "https" : "http", URI_GET_CONTACTS, NULL);

	/* use proxy if necessary */
	if (e_proxy_require_proxy_for_uri (proxy, uri))
		proxy_uri = e_proxy_peek_uri_for (proxy, uri);
	gdata_service_set_proxy_uri (GDATA_SERVICE (priv->service), proxy_uri);

	g_free (uri);
}

gboolean
google_book_connect_to_google (GoogleBook *book, const gchar *password, GError **error)
{
	GoogleBookPrivate *priv;

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);
	g_return_val_if_fail (password, FALSE);

	priv = GET_PRIVATE (book);

	if (priv->service) {
		g_warning ("Connection to Google already established.");
		return TRUE;
	}

	priv->service = GDATA_SERVICE (gdata_contacts_service_new ("evolution-client-0.0.1"));
	priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (priv->proxy);
	proxy_settings_changed (priv->proxy, priv);

	if (!gdata_service_authenticate (priv->service, priv->username, password, NULL, error)) {
		g_object_unref (priv->service);
		priv->service = NULL;
		g_object_unref (priv->proxy);
		priv->proxy = NULL;
		return FALSE;
	}

	g_signal_connect (priv->proxy, "changed", G_CALLBACK (proxy_settings_changed), priv);

	return google_book_cache_refresh_if_needed (book, error);
}

void
google_book_set_offline_mode (GoogleBook *book, gboolean offline)
{
	GoogleBookPrivate *priv;

	__debug__ (G_STRFUNC);
	g_return_if_fail (IS_GOOGLE_BOOK (book));

	priv = GET_PRIVATE (book);

	priv->offline = offline;
	if (offline) {
		/* Going offline, so we can free our service and proxy */
		if (priv->service)
			g_object_unref (priv->service);
		priv->service = NULL;

		if (priv->proxy)
			g_object_unref (priv->proxy);
		priv->proxy = NULL;
	} else {
		/* Going online, so we need to re-authenticate and re-create the service and proxy.
		 * This is done in google_book_connect_to_google(), which is called by EBookBackendGoogle when it gets the authentication data. */
		google_book_emit_auth_required (book);
	}
}

gboolean
google_book_add_contact (GoogleBook *book, EContact *contact, EContact **out_contact, GError **error)
{
	GoogleBookPrivate *priv;
	GDataEntry *entry, *new_entry;
	gchar *xml;

	*out_contact = NULL;

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

	priv = GET_PRIVATE (book);

	g_return_val_if_fail (priv->service, FALSE);

	entry = _gdata_entry_new_from_e_contact (contact);
	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	__debug__ ("new entry with xml: %s", xml);
	g_free (xml);

	new_entry = GDATA_ENTRY (gdata_contacts_service_insert_contact (GDATA_CONTACTS_SERVICE (priv->service), GDATA_CONTACTS_CONTACT (entry),
	                                                                NULL, error));
	g_object_unref (entry);
	if (!new_entry)
		return FALSE;

	*out_contact = google_book_cache_add_contact (book, new_entry);
	g_object_unref (new_entry);

	return TRUE;
}

gboolean
google_book_update_contact (GoogleBook *book, EContact *contact, EContact **out_contact, GError **error)
{
	GoogleBookPrivate *priv;
	GDataEntry *entry, *new_entry;
	EContact *cached_contact;
	gchar *xml;
	const gchar *uid;

	*out_contact = NULL;

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

	priv = GET_PRIVATE (book);

	g_return_val_if_fail (priv->service, FALSE);

	uid = e_contact_get (contact, E_CONTACT_UID);

	entry = NULL;
	cached_contact = google_book_cache_get_contact (book, uid, &entry);
	if (!cached_contact) {
		g_set_error (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_NOT_FOUND, "Contact with uid %s not found in cache.", uid);
		return FALSE;
	}
	g_object_unref (cached_contact);
	_gdata_entry_update_from_e_contact (entry, contact);

	xml = gdata_parsable_get_xml (GDATA_PARSABLE (entry));
	__debug__ ("Before:\n%s", xml);
	g_free (xml);

	new_entry = GDATA_ENTRY (gdata_contacts_service_update_contact (GDATA_CONTACTS_SERVICE (priv->service), GDATA_CONTACTS_CONTACT (entry),
	                                                                NULL, error));
	g_object_unref (entry);

	if (!new_entry)
		return FALSE;

	xml = NULL;
	if (new_entry)
		xml = gdata_parsable_get_xml (GDATA_PARSABLE (new_entry));
	__debug__ ("After:\n%s", xml);
	g_free (xml);

	*out_contact = google_book_cache_add_contact (book, new_entry);
	g_object_unref (new_entry);

	return TRUE;
}

gboolean
google_book_remove_contact (GoogleBook *book, const gchar *uid, GError **error)
{
	GoogleBookPrivate *priv;
	GDataEntry *entry = NULL;
	EContact *cached_contact;
	gboolean success;

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), FALSE);

	priv = GET_PRIVATE (book);

	g_return_val_if_fail (priv->service, FALSE);

	cached_contact = google_book_cache_get_contact (book, uid, &entry);
	if (!cached_contact) {
		g_set_error (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_NOT_FOUND, "Contact with uid %s not found in cache.", uid);
		return FALSE;
	}

	google_book_cache_remove_contact (book, uid);
	success = gdata_service_delete_entry (GDATA_SERVICE (priv->service), entry, NULL, error);
	g_object_unref (entry);
	g_object_unref (cached_contact);

	return success;
}

static void
process_subsequent_entry (gpointer list_data, gpointer user_data)
{
	GoogleBookPrivate *priv;
	GoogleBook *book = user_data;
	GDataEntry *entry;
	gboolean is_deleted, is_cached;
	const gchar *uid;

	__debug__ (G_STRFUNC);
	priv = GET_PRIVATE (book);
	entry = GDATA_ENTRY (list_data);
	uid = gdata_entry_get_id (entry);
	is_deleted = gdata_contacts_contact_is_deleted (GDATA_CONTACTS_CONTACT (entry));

	is_cached = google_book_cache_has_contact (book, uid);
	if (is_deleted) {
		/* Do we have this item in our cache? */
		if (is_cached) {
			google_book_cache_remove_contact (book, uid);
			google_book_emit_contact_removed (book, uid);
		}
	} else {
		EContact *contact = google_book_cache_add_contact (book, entry);

		if (is_cached)
			google_book_emit_contact_changed (book, contact);
		else
			google_book_emit_contact_added (book, contact);

		g_object_unref (contact);
	}
}

static void
process_initial_entry (gpointer list_data, gpointer user_data)
{
	GoogleBookPrivate *priv;
	GoogleBook *book = user_data;
	GDataEntry *entry;
	EContact *contact;

	__debug__ (G_STRFUNC);
	priv = GET_PRIVATE (book);
	entry = GDATA_ENTRY (list_data);

	contact = google_book_cache_add_contact (book, entry);

	google_book_emit_contact_added (GOOGLE_BOOK (book), contact);
	g_object_unref (contact);
}

static gboolean
google_book_get_new_contacts_in_chunks (GoogleBook *book, gint chunk_size, GError **error)
{
	GoogleBookPrivate *priv;
	GDataFeed *feed;
	GDataQuery *query;
	gchar *last_updated;
	GError *our_error = NULL;
	gboolean rv = TRUE;
	GTimeVal current_time;
	int results;

	priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (priv->service, FALSE);

	last_updated = google_book_cache_get_last_update (book);

	google_book_cache_freeze (book);

	/* Build our query */
	query = GDATA_QUERY (gdata_contacts_query_new_with_limits (NULL, 1, chunk_size));
	if (last_updated) {
		GTimeVal updated;

		g_assert (g_time_val_from_iso8601 (last_updated, &updated) == TRUE);
		gdata_query_set_updated_min (query, &updated);
		gdata_contacts_query_set_show_deleted (GDATA_CONTACTS_QUERY (query), TRUE);
	}

	/* Get the paginated results */
	do {
		GList *entries;

		/* Run the query */
		feed = gdata_contacts_service_query_contacts (GDATA_CONTACTS_SERVICE (priv->service), query, NULL, NULL, NULL, &our_error);

		if (our_error) {
			google_book_emit_sequence_complete (book, our_error);
			g_propagate_error (error, our_error);

			rv = FALSE;
			goto out;
		}

		entries = gdata_feed_get_entries (feed);
		results = entries ? g_list_length (entries) : 0;
		__debug__ ("Feed has %d entries", results);

		/* Process the entries from this page */
		if (last_updated)
			g_list_foreach (entries, process_subsequent_entry, book);
		else
			g_list_foreach (entries, process_initial_entry, book);
		g_object_unref (feed);

		/* Move to the next page */
		gdata_query_next_page (query);
	} while (results == chunk_size);

	/* Finish updating the cache */
	g_get_current_time (&current_time);
	google_book_cache_set_last_update (book, &current_time);
	google_book_emit_sequence_complete (book, NULL);

out:
	g_free (last_updated);
	google_book_cache_thaw (book);

	return rv;
}

EContact *
google_book_get_contact (GoogleBook *book, const gchar *uid, GError **error)
{
	GoogleBookPrivate *priv;
	EContact *contact;
	GError *child_error = NULL;

	priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), NULL);

	google_book_cache_refresh_if_needed (book, &child_error);

	contact = google_book_cache_get_contact (book, uid, NULL);

	if (contact) {
		/* We found the contact, so forget about errors during refresh */
		if (child_error)
			g_error_free (child_error);

		return contact;
	}

	if (!child_error)
		g_propagate_error (error, child_error);
	else
		g_set_error (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_NOT_FOUND, "Contact with uid %s not found in cache.", uid);

	return NULL;
}

GList *
google_book_get_all_contacts (GoogleBook *book, GError **error)
{
	GoogleBookPrivate *priv;
	GList *contacts;
	GError *child_error = NULL;

	priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), NULL);

	google_book_cache_refresh_if_needed (book, &child_error);

	contacts = google_book_cache_get_contacts (book);

	if (contacts) {
		/* We found the contact, so forget about errors during refresh */
		if (child_error)
			g_error_free (child_error);

		return contacts;
	}

	g_propagate_error (error, child_error);
	return NULL;
}

static gboolean
on_refresh_idle (gpointer user_data)
{
	GoogleBook *book = user_data;
	GoogleBookPrivate *priv;

	priv = GET_PRIVATE (book);

	priv->idle_id = 0;

	google_book_cache_refresh_if_needed (book, NULL);

	return FALSE;
}

GList *
google_book_get_all_contacts_in_live_mode (GoogleBook *book)
{
	GoogleBookPrivate *priv;
	gboolean need_update;
	GList *contacts;

	priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	g_return_val_if_fail (IS_GOOGLE_BOOK (book), NULL);

	priv->live_mode = TRUE;

	need_update = google_book_cache_needs_update (book, NULL);

	if (need_update) {
		if (!priv->service) {
			/* We need authorization first */
			google_book_emit_auth_required (book);
		} else {
			priv->idle_id = g_idle_add (on_refresh_idle, book);
		}
	}

	contacts = google_book_cache_get_contacts (book);
	__debug__ ("%d contacts found in cache", g_list_length (contacts));

	return contacts;
}

void
google_book_set_live_mode (GoogleBook *book, gboolean live_mode)
{
	GoogleBookPrivate *priv;

	priv = GET_PRIVATE (book);

	__debug__ (G_STRFUNC);
	priv->live_mode = live_mode;

	if (FALSE == live_mode && priv->refresh_id > 0) {
		g_source_remove (priv->refresh_id);
		priv->refresh_id = 0;
	}

	if (priv->live_mode)
		google_book_cache_refresh_if_needed (book, NULL);
}
