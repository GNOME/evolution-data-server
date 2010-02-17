/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* A class to cache address  book conents on local file system
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "e-book-backend-cache.h"
#include "e-book-backend-sexp.h"

G_DEFINE_TYPE (EBookBackendCache, e_book_backend_cache, E_TYPE_FILE_CACHE)

struct _EBookBackendCachePrivate {
	gchar *uri;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_URI
};

static GObjectClass *parent_class = NULL;

static gchar *
get_filename_from_uri (const gchar *uri)
{
	gchar *mangled_uri, *filename;
	gint i;

	/* mangle the URI to not contain invalid characters */
	mangled_uri = g_strdup (uri);
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	/* generate the file name */
	filename = g_build_filename (g_get_home_dir (), ".evolution/cache/addressbook",
				     mangled_uri, "cache.xml", NULL);

	/* free memory */
	g_free (mangled_uri);

	return filename;
}

static void
e_book_backend_cache_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	EBookBackendCache *cache;
	EBookBackendCachePrivate *priv;
	gchar *cache_file;

	cache = E_BOOK_BACKEND_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_URI :
		cache_file = get_filename_from_uri (g_value_get_string (value));
		if (!cache_file)
			break;

		g_object_set (G_OBJECT (cache), "filename", cache_file, NULL);
		g_free (cache_file);

		if (priv->uri)
			g_free (priv->uri);
		priv->uri = g_value_dup_string (value);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_book_backend_cache_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	EBookBackendCache *cache;
	EBookBackendCachePrivate *priv;

	cache = E_BOOK_BACKEND_CACHE (object);
	priv = cache->priv;

	switch (property_id) {
	case PROP_URI :
		g_value_set_string (value, priv->uri);
		break;
	default :
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
e_book_backend_cache_finalize (GObject *object)
{
	EBookBackendCache *cache;
	EBookBackendCachePrivate *priv;

	cache = E_BOOK_BACKEND_CACHE (object);
	priv = cache->priv;

	if (priv) {
		if (priv->uri) {
			g_free (priv->uri);
			priv->uri = NULL;
		}

		g_free (priv);
		cache->priv = NULL;
	}

	parent_class->finalize (object);
}

static GObject *
e_book_backend_cache_constructor (GType type,
                                 guint n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
	GObject *obj;
	const gchar *uri;
	gchar *cache_file;

	/* Invoke parent constructor. */
	obj = parent_class->constructor (type,
					 n_construct_properties,
					 construct_properties);

	/* extract uid */
	if (!g_ascii_strcasecmp ( g_param_spec_get_name (construct_properties->pspec), "uri")) {
		uri = g_value_get_string (construct_properties->value);
		cache_file = get_filename_from_uri (uri);
		if (cache_file)
			g_object_set (obj, "filename", cache_file, NULL);
		g_free (cache_file);
	}

	return obj;
}

static void
e_book_backend_cache_class_init (EBookBackendCacheClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_book_backend_cache_finalize;
	object_class->set_property = e_book_backend_cache_set_property;
	object_class->get_property = e_book_backend_cache_get_property;

        object_class->constructor = e_book_backend_cache_constructor;
	g_object_class_install_property (object_class, PROP_URI,
					 g_param_spec_string ("uri", NULL, NULL, "",
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));
}

static void
e_book_backend_cache_init (EBookBackendCache *cache)
{
	EBookBackendCachePrivate *priv;

	priv = g_new0 (EBookBackendCachePrivate, 1);

	cache->priv = priv;

}

/**
 * e_book_backend_cache_new
 * @uri: URI of the backend to be cached.
 *
 * Creates a new #EBookBackendCache object, which implements a local
 * cache of #EContact objects, useful for remote backends.
 *
 * Return value: A new #EBookBackendCache.
 */
EBookBackendCache *
e_book_backend_cache_new (const gchar *uri)
{
	EBookBackendCache *cache;

	cache = g_object_new (E_TYPE_BOOK_BACKEND_CACHE, "uri", uri, NULL);

        return cache;
}

/**
 * e_book_backend_cache_get_contact:
 * @cache: an #EBookBackendCache
 * @uid: a unique contact ID
 *
 * Get a cached contact. Note that the returned #EContact will be
 * newly created, and must be unreffed by the caller when no longer
 * needed.
 *
 * Return value: A cached #EContact, or %NULL if @uid is not cached.
 **/
EContact *
e_book_backend_cache_get_contact (EBookBackendCache *cache, const gchar *uid)
{
	const gchar *vcard_str;
	EContact *contact = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	vcard_str = e_file_cache_get_object (E_FILE_CACHE (cache), uid);
	if (vcard_str) {
		contact = e_contact_new_from_vcard (vcard_str);

	}

	return contact;
}

/**
 * e_book_backend_cache_add_contact:
 * @cache: an #EBookBackendCache
 * @contact: an #EContact
 *
 * Adds @contact to @cache.
 *
 * Return value: %TRUE if the contact was cached successfully, %FALSE otherwise.
 **/
gboolean
e_book_backend_cache_add_contact (EBookBackendCache *cache,
				   EContact *contact)
{
	gchar *vcard_str;
	const gchar *uid;
	gboolean retval;
	EBookBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), FALSE);

	priv = cache->priv;

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	vcard_str = e_vcard_to_string (E_VCARD(contact), EVC_FORMAT_VCARD_30);

	if (e_file_cache_get_object (E_FILE_CACHE (cache), uid))
		retval = e_file_cache_replace_object (E_FILE_CACHE (cache), uid, vcard_str);
	else
		retval = e_file_cache_add_object (E_FILE_CACHE (cache), uid, vcard_str);

	g_free (vcard_str);

	return retval;
}

/**
 * e_book_backend_cache_remove_contact:
 * @cache: an #EBookBackendCache
 * @uid: a unique contact ID
 *
 * Removes the contact identified by @uid from @cache.
 *
 * Return value: %TRUE if the contact was found and removed, %FALSE otherwise.
 **/
gboolean
e_book_backend_cache_remove_contact (EBookBackendCache *cache,
				    const gchar *uid)

{
	gboolean retval;
	EBookBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	priv = cache->priv;

	if (!e_file_cache_get_object (E_FILE_CACHE (cache), uid)) {
		return FALSE;
	}

	retval = e_file_cache_remove_object (E_FILE_CACHE (cache), uid);

	return retval;
}

/**
 * e_book_backend_cache_check_contact:
 * @cache: an #EBookBackendCache
 * @uid: a unique contact ID
 *
 * Checks if the contact identified by @uid exists in @cache.
 *
 * Return value: %TRUE if the cache contains the contact, %FALSE otherwise.
 **/
gboolean
e_book_backend_cache_check_contact (EBookBackendCache *cache, const gchar *uid)
{

	gboolean retval;
	EBookBackendCachePrivate *priv;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	priv = cache->priv;

	retval = FALSE;
	if (e_file_cache_get_object (E_FILE_CACHE (cache), uid))
		retval = TRUE;
	return retval;
}

/**
 * e_book_backend_cache_get_contacts:
 * @cache: an #EBookBackendCache
 * @query: an s-expression
 *
 * Returns a list of #EContact elements from @cache matching @query.
 * When done with the list, the caller must unref the contacts and
 * free the list.
 *
 * Return value: A #GList of pointers to #EContact.
 **/
GList *
e_book_backend_cache_get_contacts (EBookBackendCache *cache, const gchar *query)
{
        gchar *vcard_str;
        GSList *l, *lcache;
	GList *list = NULL;
	EContact *contact;
        EBookBackendSExp *sexp = NULL;
	const gchar *uid;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), NULL);
	if (query) {
		sexp = e_book_backend_sexp_new (query);
		if (!sexp)
			return NULL;
	}

        lcache = l = e_file_cache_get_objects (E_FILE_CACHE (cache));

        for (; l != NULL; l = g_slist_next (l)) {
                vcard_str = l->data;
                if (vcard_str && !strncmp (vcard_str, "BEGIN:VCARD", 11)) {
                        contact = e_contact_new_from_vcard (vcard_str);
			uid = e_contact_get_const (contact, E_CONTACT_UID);
                        if (contact && uid && *uid &&(query && e_book_backend_sexp_match_contact(sexp, contact)))
				list = g_list_prepend (list, contact);
			else
				g_object_unref (contact);
                }

        }
	if (lcache)
		g_slist_free (lcache);
	if (sexp)
		g_object_unref (sexp);

        return g_list_reverse (list);
}

/**
 * e_book_backend_cache_search:
 * @cache: an #EBookBackendCache
 * @query: an s-expression
 *
 * Returns an array of pointers to unique contact ID strings for contacts
 * in @cache matching @query. When done with the array, the caller must
 * free the ID strings and the array.
 *
 * Return value: A #GPtrArray of pointers to contact ID strings.
 **/
GPtrArray *
e_book_backend_cache_search (EBookBackendCache *cache, const gchar *query)
{
	GList *matching_contacts, *temp;
	GPtrArray *ptr_array;

	matching_contacts = e_book_backend_cache_get_contacts (cache, query);
	ptr_array = g_ptr_array_new ();

	temp = matching_contacts;
	for (; matching_contacts != NULL; matching_contacts = g_list_next (matching_contacts)) {
		g_ptr_array_add (ptr_array, e_contact_get (matching_contacts->data, E_CONTACT_UID));
		g_object_unref (matching_contacts->data);
	}
	g_list_free (temp);

	return ptr_array;
}

/**
 * e_book_backend_cache_exists:
 * @uri: URI for the cache
 *
 * Checks if an #EBookBackendCache exists at @uri.
 *
 * Return value: %TRUE if cache exists, %FALSE if not.
 **/
gboolean
e_book_backend_cache_exists (const gchar *uri)
{
	gchar *file_name;
	gboolean exists = FALSE;
	file_name = get_filename_from_uri (uri);

	if (file_name && g_file_test (file_name, G_FILE_TEST_EXISTS)) {
		exists = TRUE;
		g_free (file_name);
	}

	return exists;
}

/**
 * e_book_backend_cache_set_populated:
 * @cache: an #EBookBackendCache
 *
 * Flags @cache as being populated - that is, it is up-to-date on the
 * contents of the book it's caching.
 **/
void
e_book_backend_cache_set_populated (EBookBackendCache *cache)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_CACHE (cache));
	e_file_cache_add_object (E_FILE_CACHE (cache), "populated", "TRUE");

}

/**
 * e_book_backend_cache_is_populated:
 * @cache: an #EBookBackendCache
 *
 * Checks if @cache is populated.
 *
 * Return value: %TRUE if @cache is populated, %FALSE otherwise.
 **/
gboolean
e_book_backend_cache_is_populated (EBookBackendCache *cache)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), FALSE);
	if (e_file_cache_get_object (E_FILE_CACHE (cache), "populated"))
		return TRUE;
	return FALSE;
}

void
e_book_backend_cache_set_time (EBookBackendCache *cache, const gchar *t)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_CACHE (cache));
	e_file_cache_add_object (E_FILE_CACHE (cache), "last_update_time", t);
}

gchar *
e_book_backend_cache_get_time (EBookBackendCache *cache)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), NULL);
	return g_strdup (e_file_cache_get_object (E_FILE_CACHE (cache), "last_update_time"));
}

