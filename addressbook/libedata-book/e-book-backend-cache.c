/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* A class to cache address  book conents on local file system
 *
 * Copyright (C) 2004 Novell, Inc.
 *
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "e-book-backend-cache.h"
#include "e-book-backend-sexp.h"

struct _EBookBackendCachePrivate {
	char *uri;
};

/* Property IDs */
enum {
	PROP_0,
	PROP_URI
};

static GObjectClass *parent_class = NULL;

static char *
get_filename_from_uri (const char *uri)
{
	char *mangled_uri, *filename;
	int i;

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
	char *cache_file;

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
	const char *uri;
	EBookBackendCacheClass *klass;
	GObjectClass *parent_class;

	/* Invoke parent constructor. */
	klass = E_BOOK_BACKEND_CACHE_CLASS (g_type_class_peek (E_TYPE_BOOK_BACKEND_CACHE));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	obj = parent_class->constructor (type,
					 n_construct_properties,
					 construct_properties);
  
	/* extract uid */
	if (!g_ascii_strcasecmp ( g_param_spec_get_name (construct_properties->pspec), "uri")) {
		uri = g_value_get_string (construct_properties->value);
		g_object_set (obj, "filename", get_filename_from_uri (uri), NULL);
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


GType
e_book_backend_cache_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EBookBackendCacheClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_book_backend_cache_class_init,
                        NULL, NULL,
                        sizeof (EBookBackendCache),
                        0,
                        (GInstanceInitFunc) e_book_backend_cache_init,
                };
		type = g_type_register_static (E_TYPE_FILE_CACHE, "EBookBackendCache", &info, 0);
	}

	return type;
}

/**
 * e_cal_backend_cache_new
 * @uri: URI of the backend to be cached.
 *
 * Creates a new #EBookBackendCache object, which implements a cache of
 * calendar/tasks objects, very useful for remote backends.
 *
 * Return value: The newly created object.
 */
EBookBackendCache *
e_book_backend_cache_new (const char *uri)
{
	EBookBackendCache *cache;
        
       	cache = g_object_new (E_TYPE_BOOK_BACKEND_CACHE, "uri", uri, NULL);

        return cache;
}


EContact *
e_book_backend_cache_get_contact (EBookBackendCache *cache, const char *uid)
{
	const char *vcard_str;
	EContact *contact = NULL;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	vcard_str = e_file_cache_get_object (E_FILE_CACHE (cache), uid);
	if (vcard_str) {
		contact = e_contact_new_from_vcard (vcard_str);
		
	}


	return contact;
}


gboolean
e_book_backend_cache_add_contact (EBookBackendCache *cache,
				   EContact *contact)
{
	char *vcard_str;
	const char *uid;
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


gboolean
e_book_backend_cache_remove_contact (EBookBackendCache *cache,
				    const char *uid)
				      
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
gboolean 
e_book_backend_cache_check_contact (EBookBackendCache *cache, const char *uid)
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

GList *
e_book_backend_cache_get_contacts (EBookBackendCache *cache, const char *query)
{
        char *vcard_str;
        GSList *l;
	GList *list = NULL;
	EContact *contact;
        EBookBackendSExp *sexp = NULL;
	const char *uid;
	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), NULL);
	if (query) {
		sexp = e_book_backend_sexp_new (query);
		if (!sexp)
			return NULL;
	}
       

        l = e_file_cache_get_objects (E_FILE_CACHE (cache));
        if (!l)
                return NULL;
        for ( ; l != NULL; l = g_slist_next (l)) {
                vcard_str = l->data;
                if (vcard_str) {
                        contact = e_contact_new_from_vcard (vcard_str);
			uid = e_contact_get_const (contact, E_CONTACT_UID);
                        if (contact && uid && *uid &&(query && e_book_backend_sexp_match_contact(sexp, contact)))
				list = g_list_append (list, contact);
                }
                
        }

        return list;
}

GPtrArray *
e_book_backend_cache_search (EBookBackendCache *cache, const char *query)
{
	GList *matching_contacts, *temp;
	GPtrArray *ptr_array;
	
	matching_contacts = e_book_backend_cache_get_contacts (cache, query);
	ptr_array = g_ptr_array_new ();
	
	temp = matching_contacts;
	for (; matching_contacts != NULL; matching_contacts = g_list_next (matching_contacts))
		g_ptr_array_add (ptr_array, e_contact_get (matching_contacts->data, E_CONTACT_UID));
		
	return ptr_array;
	

}

gboolean 
e_book_backend_cache_exists (const char *uri)
{
	char *file_name;
	gboolean exists = FALSE;
	file_name = get_filename_from_uri (uri);
	
	if (file_name && g_file_test (file_name, G_FILE_TEST_EXISTS)) {
		exists = TRUE;
		g_free (file_name);
	}
	
	return exists;
}

void
e_book_backend_cache_set_populated (EBookBackendCache *cache)
{
  	g_return_if_fail (E_IS_BOOK_BACKEND_CACHE (cache));
	e_file_cache_add_object (E_FILE_CACHE (cache), "populated", "TRUE");
	
}

gboolean
e_book_backend_cache_is_populated (EBookBackendCache *cache)
{
  	g_return_val_if_fail (E_IS_BOOK_BACKEND_CACHE (cache), NULL);
	if (e_file_cache_get_object (E_FILE_CACHE (cache), "populated"))
		return TRUE;
	return FALSE;	
}
