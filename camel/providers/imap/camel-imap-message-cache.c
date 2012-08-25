/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-message-cache.c: Class for an IMAP message cache */

/*
 * Author:
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-imap-message-cache.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Common define to start reducing duplication of base-part handling on win32.
 */
#ifdef G_OS_WIN32
/* See comment in insert_setup() */
#define BASE_PART_SUFFIX ".~"
#else
#define BASE_PART_SUFFIX "."
#endif

struct _part_find {
	/* UID name on disk - e.g. "0." or "0.HEADERS". On windows "0." is
	 * stored as "0.~"
	 */
	gchar *disk_part_name;
	/* Was the part found? */
	gint found;
};

G_DEFINE_TYPE (CamelImapMessageCache, camel_imap_message_cache, CAMEL_TYPE_OBJECT)

static void
stream_finalize (CamelImapMessageCache *cache,
                 GObject *where_the_object_was)
{
	gchar *key;

	key = g_hash_table_lookup (cache->cached, where_the_object_was);
	if (!key)
		return;
	g_hash_table_remove (cache->cached, where_the_object_was);
	g_hash_table_insert (cache->parts, key, NULL);
}

static void
free_part (gpointer key,
           gpointer value,
           gpointer data)
{
	if (value) {
		if (strchr (key, '.')) {
			g_object_weak_unref (
				G_OBJECT (value), (GWeakNotify)
				stream_finalize, data);
			g_object_unref (value);
		} else
			g_ptr_array_free (value, TRUE);
	}
	g_free (key);
}

static void
imap_message_cache_finalize (GObject *object)
{
	CamelImapMessageCache *cache;

	cache = CAMEL_IMAP_MESSAGE_CACHE (object);

	g_free (cache->path);

	if (cache->parts) {
		g_hash_table_foreach (cache->parts, free_part, cache);
		g_hash_table_destroy (cache->parts);
	}

	if (cache->cached)
		g_hash_table_destroy (cache->cached);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_message_cache_parent_class)->finalize (object);
}

static void
camel_imap_message_cache_class_init (CamelImapMessageCacheClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = imap_message_cache_finalize;
}

static void
camel_imap_message_cache_init (CamelImapMessageCache *imap_message_cache)
{
}

static void
cache_put (CamelImapMessageCache *cache,
           const gchar *uid,
           const gchar *key,
           CamelStream *stream)
{
	gchar *hash_key;
	GPtrArray *subparts;
	gpointer okey, ostream;
	guint32 uidval;

	uidval = strtoul (uid, NULL, 10);
	if (uidval > cache->max_uid)
		cache->max_uid = uidval;

	subparts = g_hash_table_lookup (cache->parts, uid);
	if (!subparts) {
		subparts = g_ptr_array_new ();
		g_hash_table_insert (cache->parts, g_strdup (uid), subparts);
	}

	if (g_hash_table_lookup_extended (cache->parts, key, &okey, &ostream)) {
		if (ostream) {
			g_object_weak_unref (
				G_OBJECT (ostream), (GWeakNotify)
				stream_finalize, cache);
			g_hash_table_remove (cache->cached, ostream);
			g_object_unref (ostream);
		}
		hash_key = okey;
	} else {
		hash_key = g_strdup (key);
		g_ptr_array_add (subparts, hash_key);
	}

	g_hash_table_insert (cache->parts, hash_key, stream);
	g_hash_table_insert (cache->cached, stream, hash_key);

	if (stream) {
		g_object_weak_ref (
			G_OBJECT (stream), (GWeakNotify)
			stream_finalize, cache);
	}
}

/**
 * camel_imap_message_cache_new:
 * @path: directory to use for storage
 * @summary: CamelFolderSummary for the folder we are caching
 * @error: return location for a #GError, or %NULL
 *
 * Returns: a new CamelImapMessageCache object using @path for
 * storage. If cache files already exist in @path, then any that do not
 * correspond to messages in @summary will be deleted.
 * @path is scanned for its contents, which means creating a cache object can be
 * expensive, but the parts hash is immediately usable.
 **/
CamelImapMessageCache *
camel_imap_message_cache_new (const gchar *path,
                              CamelFolderSummary *summary,
                              GError **error)
{
	CamelImapMessageCache *cache;
	GDir *dir;
	const gchar *dname;
	gchar *uid, *p;
	GPtrArray *deletes;

	dir = g_dir_open (path, 0, error);
	if (!dir) {
		g_prefix_error (error, _("Could not open cache directory: "));
		return NULL;
	}

	cache = g_object_new (CAMEL_TYPE_IMAP_MESSAGE_CACHE, NULL);
	cache->path = g_strdup (path);

	cache->parts = g_hash_table_new (g_str_hash, g_str_equal);
	cache->cached = g_hash_table_new (NULL, NULL);
	deletes = g_ptr_array_new ();

	while ((dname = g_dir_read_name (dir))) {
		if (!isdigit (dname[0]))
			continue;
		p = strchr (dname, '.');
		if (p)
			uid = g_strndup (dname, p - dname);
		else
			uid = g_strdup (dname);

		if (camel_folder_summary_check_uid (summary, uid))
			cache_put (cache, uid, dname, NULL);
		else
			g_ptr_array_add (deletes, g_strdup_printf ("%s/%s", cache->path, dname));

		g_free (uid);
	}
	g_dir_close (dir);

	while (deletes->len) {
		g_unlink (deletes->pdata[0]);
		g_free (deletes->pdata[0]);
		g_ptr_array_remove_index_fast (deletes, 0);
	}
	g_ptr_array_free (deletes, TRUE);

	return cache;
}

/**
 * camel_imap_message_cache_delete:
 * @path: directory to use for storage
 * @error: return location for a #GError, or %NULL
 *
 * All the files under this directory would be deleted
 **/

gboolean
camel_imap_message_cache_delete (const gchar *path,
                                 GError **error)
{
	GDir *dir;
	const gchar *dname;
	GPtrArray *deletes;

	dir = g_dir_open (path, 0, error);
	if (!dir) {
		g_prefix_error (error, _("Could not open cache directory: "));
		return FALSE;
	}

	deletes = g_ptr_array_new ();
	while ((dname = g_dir_read_name (dir)))
		g_ptr_array_add (deletes, g_strdup_printf ("%s/%s", path, dname));

	g_dir_close (dir);

	while (deletes->len) {
		g_unlink (deletes->pdata[0]);
		g_free (deletes->pdata[0]);
		g_ptr_array_remove_index_fast (deletes, 0);
	}
	g_ptr_array_free (deletes, TRUE);

	return TRUE;
}

/**
 * camel_imap_message_cache_max_uid:
 * @cache: the cache
 *
 * Returns: the largest (real) UID in the cache.
 **/
guint32
camel_imap_message_cache_max_uid (CamelImapMessageCache *cache)
{
	return cache->max_uid;
}

/**
 * camel_imap_message_cache_set_path:
 * @cache:
 * @path:
 *
 * Set the path used for the message cache.
 **/
void
camel_imap_message_cache_set_path (CamelImapMessageCache *cache,
                                   const gchar *path)
{
	g_free (cache->path);
	cache->path = g_strdup (path);
}

static CamelStream *
insert_setup (CamelImapMessageCache *cache,
              const gchar *uid,
              const gchar *part_spec,
              gchar **path,
              gchar **key,
              GError **error)
{
	CamelStream *stream;
	gint fd;

#ifdef G_OS_WIN32
	/* Trailing periods in file names are silently dropped on
	 * Win32, argh. The code in this file requires the period to
	 * be there. So in case part_spec is empty, use a tilde (just
	 * a random choice) instead.
	 */
	if (!*part_spec)
		part_spec = "~";
#endif
	*path = g_strdup_printf ("%s/%s.%s", cache->path, uid, part_spec);
	*key = strrchr (*path, '/') + 1;
	stream = g_hash_table_lookup (cache->parts, *key);
	if (stream)
		g_object_unref (stream);

	fd = g_open (*path, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
	if (fd == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Failed to cache message %s: %s"),
			uid, g_strerror (errno));
		g_free (*path);
		return NULL;
	}

	return camel_stream_fs_new_with_fd (fd);
}

static CamelStream *
insert_abort (gchar *path,
              CamelStream *stream)
{
	g_unlink (path);
	g_free (path);
	g_object_unref (stream);
	return NULL;
}

static CamelStream *
insert_finish (CamelImapMessageCache *cache,
               const gchar *uid,
               gchar *path,
               gchar *key,
               CamelStream *stream)
{
	camel_stream_flush (stream, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_SET, NULL, NULL);
	cache_put (cache, uid, key, stream);
	g_free (path);

	return stream;
}

/**
 * camel_imap_message_cache_insert:
 * @cache: the cache
 * @uid: UID of the message data to cache
 * @part_spec: the IMAP part_spec of the data
 * @data: the data
 * @len: length of @data
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Caches the provided data into @cache.
 *
 * Returns: a CamelStream containing the cached data, which the
 * caller must unref.
 **/
CamelStream *
camel_imap_message_cache_insert (CamelImapMessageCache *cache,
                                 const gchar *uid,
                                 const gchar *part_spec,
                                 const gchar *data,
                                 gint len,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gchar *path, *key;
	CamelStream *stream;

	stream = insert_setup (cache, uid, part_spec, &path, &key, error);
	if (!stream)
		return NULL;

	if (camel_stream_write (stream, data, len, cancellable, error) == -1) {
		g_prefix_error (error, _("Failed to cache message %s: "), uid);
		return insert_abort (path, stream);
	}

	return insert_finish (cache, uid, path, key, stream);
}

/**
 * camel_imap_message_cache_insert_stream:
 * @cache: the cache
 * @uid: UID of the message data to cache
 * @part_spec: the IMAP part_spec of the data
 * @data_stream: the stream to cache
 *
 * Caches the provided data into @cache.
 **/
void
camel_imap_message_cache_insert_stream (CamelImapMessageCache *cache,
                                        const gchar *uid,
                                        const gchar *part_spec,
                                        CamelStream *data_stream)
{
	gchar *path, *key;
	CamelStream *stream;

	stream = insert_setup (cache, uid, part_spec, &path, &key, NULL);
	if (!stream)
		return;

	if (camel_stream_write_to_stream (data_stream, stream, NULL, NULL) == -1) {
		insert_abort (path, stream);
	} else {
		insert_finish (cache, uid, path, key, stream);
		g_object_unref (stream);
	}
}

/**
 * camel_imap_message_cache_insert_wrapper:
 * @cache: the cache
 * @uid: UID of the message data to cache
 * @part_spec: the IMAP part_spec of the data
 * @wrapper: the wrapper to cache
 *
 * Caches the provided data into @cache.
 **/
void
camel_imap_message_cache_insert_wrapper (CamelImapMessageCache *cache,
                                         const gchar *uid,
                                         const gchar *part_spec,
                                         CamelDataWrapper *wrapper)
{
	gchar *path, *key;
	CamelStream *stream;

	stream = insert_setup (cache, uid, part_spec, &path, &key, NULL);
	if (!stream)
		return;

	if (camel_data_wrapper_write_to_stream_sync (
		wrapper, stream, NULL, NULL) == -1) {
		insert_abort (path, stream);
	} else {
		insert_finish (cache, uid, path, key, stream);
		g_object_unref (stream);
	}
}

/**
 * camel_imap_message_cache_get_filename:
 * @cache: the cache
 * @uid: the UID of the data to get
 * @part_spec: the part_spec of the data to get
 * @error: return location for a #GError, or %NULL
 *
 * Returns: the filename of a cache item
 **/
gchar *
camel_imap_message_cache_get_filename (CamelImapMessageCache *cache,
                                       const gchar *uid,
                                       const gchar *part_spec,
                                       GError **error)
{
	gchar *path;

	if (uid[0] == 0)
		return NULL;

#ifdef G_OS_WIN32
	/* See comment in insert_setup() */
	if (!*part_spec)
		part_spec = "~";
#endif
	path = g_strdup_printf ("%s/%s.%s", cache->path, uid, part_spec);

	return path;
}

/**
 * camel_imap_message_cache_get:
 * @cache: the cache
 * @uid: the UID of the data to get
 * @part_spec: the part_spec of the data to get
 * @error: return location for a #GError, or %NULL
 *
 * Returns: a CamelStream containing the cached data (which the
 * caller must unref), or %NULL if that data is not cached.
 **/
CamelStream *
camel_imap_message_cache_get (CamelImapMessageCache *cache,
                              const gchar *uid,
                              const gchar *part_spec,
                              GError **error)
{
	CamelStream *stream;
	gchar *path, *key;

	if (uid[0] == 0)
		return NULL;

#ifdef G_OS_WIN32
	/* See comment in insert_setup() */
	if (!*part_spec)
		part_spec = "~";
#endif
	path = g_strdup_printf ("%s/%s.%s", cache->path, uid, part_spec);
	key = strrchr (path, '/') + 1;

	stream = g_hash_table_lookup (cache->parts, key);
	if (stream) {
		g_seekable_seek (
			G_SEEKABLE (stream), 0,
			G_SEEK_SET, NULL, NULL);
		g_object_ref (stream);
		g_free (path);
		return stream;
	}

	stream = camel_stream_fs_new_with_name (path, O_RDONLY, 0, error);
	if (stream)
		cache_put (cache, uid, key, stream);
	else
		g_prefix_error (error, _("Failed to cache %s: "), part_spec);

	g_free (path);

	return stream;
}

/**
 * camel_imap_message_cache_remove:
 * @cache: the cache
 * @uid: UID of the data to remove
 *
 * Removes all data associated with @uid from @cache.
 **/
void
camel_imap_message_cache_remove (CamelImapMessageCache *cache,
                                 const gchar *uid)
{
	GPtrArray *subparts;
	gchar *key, *path;
	CamelObject *stream;
	gint i;

	subparts = g_hash_table_lookup (cache->parts, uid);
	if (!subparts)
		return;
	for (i = 0; i < subparts->len; i++) {
		key = subparts->pdata[i];
		path = g_strdup_printf ("%s/%s", cache->path, key);
		g_unlink (path);
		g_free (path);
		stream = g_hash_table_lookup (cache->parts, key);
		if (stream) {
			g_object_weak_unref (
				G_OBJECT (stream), (GWeakNotify)
				stream_finalize, cache);
			g_object_unref (stream);
			g_hash_table_remove (cache->cached, stream);
		}
		g_hash_table_remove (cache->parts, key);
		g_free (key);
	}
	g_hash_table_remove (cache->parts, uid);
	g_ptr_array_free (subparts, TRUE);
}

static void
add_uids (gpointer key,
          gpointer value,
          gpointer data)
{
	if (!strchr (key, '.'))
		g_ptr_array_add (data, key);
}

/**
 * camel_imap_message_cache_clear:
 * @cache: the cache
 *
 * Removes all cached data from @cache.
 **/
void
camel_imap_message_cache_clear (CamelImapMessageCache *cache)
{
	GPtrArray *uids;
	gint i;

	uids = g_ptr_array_new ();
	g_hash_table_foreach (cache->parts, add_uids, uids);

	for (i = 0; i < uids->len; i++)
		camel_imap_message_cache_remove (cache, uids->pdata[i]);
	g_ptr_array_free (uids, TRUE);
}

/**
 * camel_imap_message_cache_copy:
 * @source: the source message cache
 * @source_uid: UID of a message in @source
 * @dest: the destination message cache
 * @dest_uid: UID of the message in @dest
 *
 * Copies all cached parts from @source_uid in @source to @dest_uid in
 * @destination.
 **/
void
camel_imap_message_cache_copy (CamelImapMessageCache *source,
                               const gchar *source_uid,
                               CamelImapMessageCache *dest,
                               const gchar *dest_uid)
{
	GPtrArray *subparts;
	CamelStream *stream;
	gchar *part;
	gint i;

	subparts = g_hash_table_lookup (source->parts, source_uid);
	if (!subparts || !subparts->len)
		return;

	for (i = 0; i < subparts->len; i++) {
		part = strchr (subparts->pdata[i], '.');
		if (!part++)
			continue;

		if ((stream = camel_imap_message_cache_get (source, source_uid, part, NULL))) {
			camel_imap_message_cache_insert_stream (dest, dest_uid, part, stream);
			g_object_unref (stream);
		}
	}
}

static void
_match_part (gpointer part_name,
             gpointer user_data)
{
	struct _part_find *part_find = (struct _part_find *) user_data;
	if (g_str_equal (part_name, part_find->disk_part_name))
		part_find->found = 1;
}

/**
 * Filter uids by the uids cached in cache.
 * The intent is that only uids fully cached are returned, but that may not be
 * what is achieved. An additional constraint is that this check should be
 * cheap, so that going offline is not an expensive operation. Filtering all
 * uids is inefficient in the first place; significant processing per uid
 * makes synchronisation very expensive. At the suggestion of Srinivasa Ragavan
 * (see http://bugzilla.gnome.org/show_bug.cgi?id=564339) the cache->parts hash
 * table is consulted. If there is a parts-list in the hash table containing
 * the part "", then we assume the message has been completely downloaded. This
 * is incorrect (see http://bugzilla.gnome.org/show_bug.cgi?id=561211 for the
 * symptoms). The code this replaces, a loop over all uids asking for the ""
 * part of the message has the same flaw: it is no /less/ accurate to assess
 * 'cached' in the manner this method does (assuming no concurrent process is
 * removing messages from the cache).
 *
 * In the future, fixing bug 561211 needs a check for *all* the parts of a
 * given uid. If the complete list of parts is available in the folder summary
 * information then it can be done cheaply, otherwise some redesign will be
 * needed.
 */
GPtrArray *
camel_imap_message_cache_filter_cached (CamelImapMessageCache *cache,
                                        GPtrArray *uids,
                                        GError **error)
{
	GPtrArray *result, *parts_list;
	gint i;
	struct _part_find part_find;
	/* Look for a part "" for each uid. */
	result = g_ptr_array_sized_new (uids->len);
	for (i = 0; i < uids->len; i++) {
		if ((parts_list = g_hash_table_lookup (cache->parts, uids->pdata[i]))) {
			/* At least one part locally present; look for "" (the
			 * HEADERS part can be present without anything else,
			 * and that part is not useful for users wanting to
			 * read the message).
			 */
			part_find.found = 0;
			part_find.disk_part_name = g_strdup_printf (
				"%s" BASE_PART_SUFFIX,
				(gchar *) uids->pdata[i]);
			g_ptr_array_foreach (parts_list, _match_part, &part_find);
			g_free (part_find.disk_part_name);
			if (part_find.found)
				/* The message is cached locally, do not
				 * include it in the result.
				 */
				continue;
		}
		/* No message parts, or message part "" not found: include the
		 * uid in the result.
		 */
		g_ptr_array_add (result, (gchar *) camel_pstring_strdup (uids->pdata[i]));
	}
	return result;
}
