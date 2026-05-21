/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

/**
 * CamelDataCache:
 *
 * A file-based cache for arbitrary keyed data, typically used by mail
 * providers to store downloaded message bodies.
 *
 * Each item is identified by a (@path, @key) pair.  The @path selects a
 * logical sub-cache (e.g. `"cur"` for downloaded messages, `"new"` for
 * outgoing spool), and @key is a per-item identifier such as a message UID.
 *
 * To write a new item safely — so that a crash or cancellation never leaves
 * a truncated file in the cache — use the atomic write API:
 *
 * |[<!-- language="C" -->
 *     GIOStream *stream;
 *     gboolean success;
 *
 *     stream = camel_data_cache_add_atomic (cache, "cur", uid, error);
 *     if (!stream)
 *         goto fail;
 *
 *     success = write_message_to_stream (G_OUTPUT_STREAM (
 *         g_io_stream_get_output_stream (stream)), message, error);
 *
 *     if (success) {
 *         stream = camel_data_cache_commit_atomic (cache, g_steal_pointer (&stream), error);
 *         /<!-- -->* stream is now open on the committed file, or NULL on error *<!-- -->/
 *     } else {
 *         camel_data_cache_discard_atomic (cache, g_steal_pointer (&stream));
 *     }
 * ]|
 *
 * camel_data_cache_add_atomic() writes to a temporary file in the same
 * directory as the target, so camel_data_cache_commit_atomic() can rename
 * it into place atomically.  Any concurrent camel_data_cache_get() call for
 * the same item blocks until the write is either committed or discarded.
 **/

#include "evolution-data-server-config.h"

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-data-cache.h"
#include "camel-object-bag.h"
#include "camel-stream-mem.h"
#include "camel-file-utils.h"
#include "camel-utils.h"

#define d(x)

/* how many 'bits' of hash are used to key the toplevel directory */
#define CAMEL_DATA_CACHE_BITS (6)
#define CAMEL_DATA_CACHE_MASK ((1 << CAMEL_DATA_CACHE_BITS)-1)

/* timeout before a cache dir is checked again for expired entries,
 * once an hour should be enough */
#define CAMEL_DATA_CACHE_CYCLE_TIME (60*60)

struct _CamelDataCachePrivate {
	CamelObjectBag *busy_bag;

	gchar *path;

	gboolean expire_enabled;
	time_t expire_age;
	time_t expire_access;

	time_t expire_last[1 << CAMEL_DATA_CACHE_BITS];
};

enum {
	PROP_0,
	PROP_PATH,
	PROP_EXPIRE_ENABLED,
	N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (CamelDataCache, camel_data_cache, G_TYPE_OBJECT)

static void
data_cache_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PATH:
			camel_data_cache_set_path (
				CAMEL_DATA_CACHE (object),
				g_value_get_string (value));
			return;

		case PROP_EXPIRE_ENABLED:
			camel_data_cache_set_expire_enabled (
				CAMEL_DATA_CACHE (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cache_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_PATH:
			g_value_set_string (
				value, camel_data_cache_get_path (
				CAMEL_DATA_CACHE (object)));
			return;

		case PROP_EXPIRE_ENABLED:
			g_value_set_boolean (
				value, camel_data_cache_get_expire_enabled (
				CAMEL_DATA_CACHE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cache_finalize (GObject *object)
{
	CamelDataCachePrivate *priv;

	priv = CAMEL_DATA_CACHE (object)->priv;

	camel_object_bag_destroy (priv->busy_bag);
	g_free (priv->path);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_data_cache_parent_class)->finalize (object);
}

static void
camel_data_cache_class_init (CamelDataCacheClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = data_cache_set_property;
	object_class->get_property = data_cache_get_property;
	object_class->finalize = data_cache_finalize;

	/**
	 * CamelDataCache:path
	 *
	 * Path
	 **/
	properties[PROP_PATH] =
		g_param_spec_string (
			"path", NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * CamelDataCache:expire-enabled
	 *
	 * Expire Enabled
	 **/
	properties[PROP_EXPIRE_ENABLED] =
		g_param_spec_boolean (
			"expire-enabled", NULL, NULL,
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
camel_data_cache_init (CamelDataCache *data_cache)
{
	CamelObjectBag *busy_bag;

	busy_bag = camel_object_bag_new (
		g_str_hash, g_str_equal,
		(CamelCopyFunc) g_strdup,
		(GFreeFunc) g_free);

	data_cache->priv = camel_data_cache_get_instance_private (data_cache);
	data_cache->priv->busy_bag = busy_bag;
	data_cache->priv->expire_enabled = TRUE;
	data_cache->priv->expire_age = -1;
	data_cache->priv->expire_access = -1;
}

/**
 * camel_data_cache_new:
 * @path: Base path of cache, subdirectories will be created here.
 * @error: return location for a #GError, or %NULL
 *
 * Create a new data cache.
 *
 * Returns: A new cache object, or %NULL if the base path cannot
 *    be written to.
 **/
CamelDataCache *
camel_data_cache_new (const gchar *path,
                      GError **error)
{
	g_return_val_if_fail (path != NULL, NULL);

	if (g_mkdir_with_parents (path, 0700) == -1) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Unable to create cache path"));
		return NULL;
	}

	return g_object_new (CAMEL_TYPE_DATA_CACHE, "path", path, NULL);
}

/**
 * camel_data_cache_get_path:
 * @cdc: a #CamelDataCache
 *
 * Returns the path to the data cache.
 *
 * Returns: the path to the data cache
 *
 * Since: 2.32
 **/
const gchar *
camel_data_cache_get_path (CamelDataCache *cdc)
{
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), NULL);

	return cdc->priv->path;
}

/**
 * camel_data_cache_set_path:
 * @cdc: a #CamelDataCache
 * @path: path to the data cache
 *
 * Sets the path to the data cache.
 *
 * Since: 2.32
 **/
void
camel_data_cache_set_path (CamelDataCache *cdc,
                           const gchar *path)
{
	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));
	g_return_if_fail (path != NULL);

	if (g_strcmp0 (cdc->priv->path, path) == 0)
		return;

	g_free (cdc->priv->path);
	cdc->priv->path = g_strdup (path);

	g_object_notify_by_pspec (G_OBJECT (cdc), properties[PROP_PATH]);
}

/**
 * camel_data_cache_get_expire_enabled:
 * @cdc: a #CamelDataCache
 *
 * Gets whether expire of cache data is enabled.
 *
 * This is a complementary property for camel_data_cache_set_expire_age()
 * and camel_data_cache_set_expire_access(), which allows to disable expiry
 * without touching the two values. Having expire enabled, but not have set
 * any of the two times, still behaves like not having expiry enabled.
 *
 * Returns: Whether expire is enabled.
 *
 * Since: 3.24
 **/
gboolean
camel_data_cache_get_expire_enabled (CamelDataCache *cdc)
{
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), FALSE);

	return cdc->priv->expire_enabled;
}

/**
 * camel_data_cache_set_expire_enabled:
 * @cdc: a #CamelDataCache
 * @expire_enabled: a value to set
 *
 * Sets whether expire of cache data is enabled.
 *
 * This is a complementary property for camel_data_cache_set_expire_age()
 * and camel_data_cache_set_expire_access(), which allows to disable expiry
 * without touching the two values. Having expire enabled, but not have set
 * any of the two times, still behaves like not having expiry enabled.
 *
 * Since: 3.24
 **/
void
camel_data_cache_set_expire_enabled (CamelDataCache *cdc,
				     gboolean expire_enabled)
{
	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));

	if (!cdc->priv->expire_enabled == !expire_enabled)
		return;

	cdc->priv->expire_enabled = expire_enabled;

	g_object_notify_by_pspec (G_OBJECT (cdc), properties[PROP_EXPIRE_ENABLED]);
}

/**
 * camel_data_cache_set_expire_age:
 * @cdc: A #CamelDataCache
 * @when: Timeout for age expiry, or -1 to disable.
 *
 * Set the cache expiration policy for aged entries.
 *
 * Items in the cache older than @when seconds may be
 * flushed at any time.  Items are expired in a lazy
 * manner, so it is indeterminate when the items will
 * physically be removed.
 *
 * Note you can set both an age and an access limit.  The
 * age acts as a hard limit on cache entries.
 **/
void
camel_data_cache_set_expire_age (CamelDataCache *cdc,
                                 time_t when)
{
	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));

	cdc->priv->expire_age = when;
}

/**
 * camel_data_cache_set_expire_access:
 * @cdc: A #CamelDataCache
 * @when: Timeout for access, or -1 to disable access expiry.
 *
 * Set the cache expiration policy for access times.
 *
 * Items in the cache which haven't been accessed for @when
 * seconds may be expired at any time.  Items are expired in a lazy
 * manner, so it is indeterminate when the items will
 * physically be removed.
 *
 * Note you can set both an age and an access limit.  The
 * age acts as a hard limit on cache entries.
 **/
void
camel_data_cache_set_expire_access (CamelDataCache *cdc,
                                    time_t when)
{
	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));

	cdc->priv->expire_access = when;
}

static void
data_cache_expire (CamelDataCache *cdc,
                   const gchar *path,
                   const gchar *keep,
                   time_t now,
                   gboolean expire_all)
{
	GDir *dir;
	const gchar *dname;
	struct stat st;
	GIOStream *stream;

	dir = g_dir_open (path, 0, NULL);
	if (dir == NULL)
		return;

	while ((dname = g_dir_read_name (dir))) {
		gchar *dpath;

		if (keep && strcmp (dname, keep) == 0)
			continue;

		dpath = g_build_filename (path, dname, NULL);

		if (g_stat (dpath, &st) == 0
		    && S_ISREG (st.st_mode)
		    && (expire_all
			|| (cdc->priv->expire_age != -1 && st.st_mtime + cdc->priv->expire_age < now)
			|| (cdc->priv->expire_access != -1 && st.st_atime + cdc->priv->expire_access < now))) {
			g_unlink (dpath);
			stream = camel_object_bag_get (cdc->priv->busy_bag, dpath);
			if (stream) {
				camel_object_bag_remove (cdc->priv->busy_bag, stream);
				g_object_unref (stream);
			}
		}

		g_free (dpath);
	}
	g_dir_close (dir);
}

/* Since we have to stat the directory anyway, we use this opportunity to
 * lazily expire old data.
 * If it is this directories 'turn', and we haven't done it for CYCLE_TIME seconds,
 * then we perform an expiry run */
static gchar *
data_cache_path (CamelDataCache *cdc,
                 gint create,
                 const gchar *path,
                 const gchar *key)
{
	gchar *dir, *real, *tmp;
	gsize dir_len;
	guint32 hash;

	hash = g_str_hash (key);
	hash = (hash >> 5) &CAMEL_DATA_CACHE_MASK;
	dir_len = strlen (cdc->priv->path) + strlen (path) + 8;
	dir = alloca (dir_len);
	g_snprintf (dir, dir_len, "%s/%s/%02x", cdc->priv->path, path, hash);

	if (g_access (dir, F_OK) == -1) {
		if (create)
			g_mkdir_with_parents (dir, 0700);
	} else if (cdc->priv->expire_enabled && (cdc->priv->expire_age != -1 || cdc->priv->expire_access != -1)) {
		time_t now;

		/* This has a race, but at worst we re-run an expire cycle which is safe */
		now = time (NULL);
		if (cdc->priv->expire_last[hash] + CAMEL_DATA_CACHE_CYCLE_TIME < now) {
			cdc->priv->expire_last[hash] = now;
			data_cache_expire (cdc, dir, key, now, FALSE);
		}
	}

	tmp = camel_file_util_safe_filename (key);
	real = g_strdup_printf ("%s/%s", dir, tmp);
	g_free (tmp);

	return real;
}

typedef struct {
	gchar *tmp_filename;
	gchar *final_path;
} CamelDCAtomicData;

static void
camel_dc_atomic_data_free (CamelDCAtomicData *adata)
{
	g_free (adata->tmp_filename);
	g_free (adata->final_path);
	g_free (adata);
}

/**
 * camel_data_cache_add_atomic:
 * @cdc: A #CamelDataCache
 * @path: Relative path of item to add.
 * @key: Key of item to add.
 * @error: return location for a #GError, or %NULL
 *
 * Open a temporary file for writing a new cache item identified by @path
 * and @key.  The temporary file lives in the same directory as the final
 * target so that camel_data_cache_commit_atomic() can rename it into place
 * atomically (same-filesystem rename).
 *
 * Any concurrent call to camel_data_cache_get() for the same @path/@key will
 * block until the caller either commits or discards the stream.
 *
 * On success, pass the returned stream to camel_data_cache_commit_atomic()
 * or camel_data_cache_discard_atomic(); the stream is consumed
 * (transfer full) by both functions.  Do not call g_object_unref() on it
 * directly.
 *
 * Returns: (transfer full): a #GIOStream for writing, or %NULL on error
 *
 * Since: 3.62
 **/
GIOStream *
camel_data_cache_add_atomic (CamelDataCache *cdc,
                             const gchar *path,
                             const gchar *key,
                             GError **error)
{
	CamelDCAtomicData *adata;
	GFileIOStream *stream;
	GFile *file;
	gchar *final_path, *tmp_filename;
	gint fd;

	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	final_path = data_cache_path (cdc, TRUE, path, key);

	do {
		stream = camel_object_bag_reserve (cdc->priv->busy_bag, final_path);
		if (stream) {
			g_unlink (final_path);
			camel_object_bag_remove (cdc->priv->busy_bag, stream);
			g_object_unref (stream);
		}
	} while (stream != NULL);

	tmp_filename = g_strdup_printf ("%s.XXXXXX", final_path);

	fd = g_mkstemp (tmp_filename);
	if (fd == -1) {
		gint errsv = errno;
		g_set_error (error, G_IO_ERROR,
		             g_io_error_from_errno (errsv),
		             _("Failed to create cache file “%s”: %s"),
		             tmp_filename, g_strerror (errsv));
		camel_object_bag_abort (cdc->priv->busy_bag, final_path);
		g_free (tmp_filename);
		g_free (final_path);
		return NULL;
	}

	close (fd);

	file = g_file_new_for_path (tmp_filename);
	stream = g_file_open_readwrite (file, NULL, error);
	g_object_unref (file);

	if (stream != NULL) {
		adata = g_new (CamelDCAtomicData, 1);
		adata->tmp_filename = tmp_filename;
		adata->final_path = final_path;
		g_object_set_data_full (G_OBJECT (stream), "camel-dc-atomic",
		                        adata, (GDestroyNotify) camel_dc_atomic_data_free);
	} else {
		camel_object_bag_abort (cdc->priv->busy_bag, final_path);
		g_unlink (tmp_filename);
		g_free (tmp_filename);
		g_free (final_path);
	}

	return stream ? G_IO_STREAM (stream) : NULL;
}

/**
 * camel_data_cache_commit_atomic:
 * @cdc: A #CamelDataCache
 * @stream: (transfer full): the #GIOStream returned by camel_data_cache_add_atomic()
 * @error: return location for a #GError, or %NULL
 *
 * Finalise an atomic cache write.  @stream is closed and unreffed, the
 * temporary file is renamed to its final location, and a new #GIOStream
 * opened on the committed file is returned (equivalent to calling
 * camel_data_cache_get() after a successful write).
 *
 * On failure the temporary file is removed and the bag reservation is
 * released, so the caller does not need to call
 * camel_data_cache_discard_atomic().
 *
 * Returns: (transfer full): a #GIOStream for the committed file, or %NULL on error
 *
 * Since: 3.62
 **/
GIOStream *
camel_data_cache_commit_atomic (CamelDataCache *cdc,
                                GIOStream *stream,
                                GError **error)
{
	CamelDCAtomicData *adata;
	GFileIOStream *result;
	GFile *file;

	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), NULL);
	g_return_val_if_fail (G_IS_IO_STREAM (stream), NULL);

	adata = g_object_steal_data (G_OBJECT (stream), "camel-dc-atomic");
	g_return_val_if_fail (adata != NULL, NULL);

	g_io_stream_close (stream, NULL, NULL);
	g_object_unref (stream);

	if (g_rename (adata->tmp_filename, adata->final_path) == -1) {
		gint errsv = errno;
		g_set_error (error, G_IO_ERROR,
		             g_io_error_from_errno (errsv),
		             _("Failed to rename cache file “%s” to “%s”: %s"),
		             adata->tmp_filename, adata->final_path, g_strerror (errsv));
		g_unlink (adata->tmp_filename);
		camel_object_bag_abort (cdc->priv->busy_bag, adata->final_path);
		camel_dc_atomic_data_free (adata);
		return NULL;
	}

	file = g_file_new_for_path (adata->final_path);
	result = g_file_open_readwrite (file, NULL, error);
	g_object_unref (file);

	if (result != NULL)
		camel_object_bag_add (cdc->priv->busy_bag, adata->final_path, result);
	else
		camel_object_bag_abort (cdc->priv->busy_bag, adata->final_path);

	camel_dc_atomic_data_free (adata);

	return result ? G_IO_STREAM (result) : NULL;
}

/**
 * camel_data_cache_discard_atomic:
 * @cdc: A #CamelDataCache
 * @stream: (transfer full): the #GIOStream returned by camel_data_cache_add_atomic()
 *
 * Discard an atomic cache write.  @stream is closed and unreffed, the
 * temporary file is removed, and the bag reservation is released so that
 * other threads can retry the operation.
 *
 * Call this on any error path before camel_data_cache_commit_atomic().
 *
 * Since: 3.62
 **/
void
camel_data_cache_discard_atomic (CamelDataCache *cdc,
                                 GIOStream *stream)
{
	CamelDCAtomicData *adata;

	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));
	g_return_if_fail (G_IS_IO_STREAM (stream));

	adata = g_object_steal_data (G_OBJECT (stream), "camel-dc-atomic");
	g_return_if_fail (adata != NULL);

	g_io_stream_close (stream, NULL, NULL);
	g_object_unref (stream);

	g_unlink (adata->tmp_filename);
	camel_object_bag_abort (cdc->priv->busy_bag, adata->final_path);
	camel_dc_atomic_data_free (adata);
}

/**
 * camel_data_cache_get:
 * @cdc: A #CamelDataCache
 * @path: Path to the (sub) cache the item exists in.
 * @key: Key for the cache item.
 * @error: return location for a #GError, or %NULL
 *
 * Lookup an item in the cache.  If the item exists, a #GIOStream is returned
 * for the item.  The stream may be shared by multiple callers, so ensure the
 * stream is in a valid state through external locking.
 *
 * The returned #GIOStream is referenced for thread-safety and must be
 * unreferenced with g_object_unref() when finished with it.
 *
 * Returns: (transfer full): a #GIOStream for the requested cache item, or %NULL on error
 **/
GIOStream *
camel_data_cache_get (CamelDataCache *cdc,
                      const gchar *path,
                      const gchar *key,
                      GError **error)
{
	GFileIOStream *stream;
	GFile *file;
	struct stat st;
	gchar *real;

	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), NULL);

	real = data_cache_path (cdc, FALSE, path, key);
	stream = camel_object_bag_reserve (cdc->priv->busy_bag, real);
	if (stream != NULL)
		goto exit;

	/* An empty cache file is useless.  Return an error. */
	if (g_stat (real, &st) == 0 && st.st_size == 0) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Cache file “%s” is empty"), real);
		camel_object_bag_abort (cdc->priv->busy_bag, real);
		goto exit;
	}

	file = g_file_new_for_path (real);
	stream = g_file_open_readwrite (file, NULL, error);
	g_object_unref (file);

	if (stream != NULL)
		camel_object_bag_add (cdc->priv->busy_bag, real, stream);
	else
		camel_object_bag_abort (cdc->priv->busy_bag, real);

exit:
	g_free (real);

	return stream ? G_IO_STREAM (stream) : NULL;
}

/**
 * camel_data_cache_get_filename:
 * @cdc: A #CamelDataCache
 * @path: Path to the (sub) cache the item exists in.
 * @key: Key for the cache item.
 *
 * Lookup the filename for an item in the cache
 *
 * Returns: The filename for a cache item
 *
 * Since: 2.26
 **/
gchar *
camel_data_cache_get_filename (CamelDataCache *cdc,
                               const gchar *path,
                               const gchar *key)
{
	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), NULL);

	return data_cache_path (cdc, FALSE, path, key);
}

/**
 * camel_data_cache_remove:
 * @cdc: A #CamelDataCache
 * @path: Path to the (sub) cache the item exists in.
 * @key: Key for the cache item.
 * @error: return location for a #GError, or %NULL
 *
 * Remove/expire a cache item.
 *
 * Returns: 0 on success, -1 on error
 **/
gint
camel_data_cache_remove (CamelDataCache *cdc,
                         const gchar *path,
                         const gchar *key,
                         GError **error)
{
	GIOStream *stream;
	gchar *real;
	gint ret;

	g_return_val_if_fail (CAMEL_IS_DATA_CACHE (cdc), -1);

	real = data_cache_path (cdc, FALSE, path, key);
	stream = camel_object_bag_get (cdc->priv->busy_bag, real);
	if (stream) {
		camel_object_bag_remove (cdc->priv->busy_bag, stream);
		g_object_unref (stream);
	}

	/* maybe we were a mem stream */
	if (g_unlink (real) == -1 && errno != ENOENT) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not remove cache entry: %s: %s"),
			real, g_strerror (errno));
		ret = -1;
	} else {
		ret = 0;
	}

	g_free (real);

	return ret;
}

/**
 * camel_data_cache_clear:
 * @cdc: a #CamelDataCache
 * @path: Path to the (sub) cache the item exists in.
 *
 * Clear cache's content in @path.
 *
 * Since: 3.2
 **/
void
camel_data_cache_clear (CamelDataCache *cdc,
                        const gchar *path)
{
	gchar *base_dir;
	GDir *dir;
	const gchar *dname;
	struct stat st;

	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));
	g_return_if_fail (path != NULL);

	base_dir = g_build_filename (cdc->priv->path, path, NULL);

	dir = g_dir_open (base_dir, 0, NULL);
	if (dir == NULL) {
		g_free (base_dir);
		return;
	}

	while ((dname = g_dir_read_name (dir))) {
		gchar *dpath;

		dpath = g_build_filename (base_dir, dname, NULL);

		if (g_stat (dpath, &st) == 0
		    && S_ISDIR (st.st_mode)
		    && !g_str_equal (dname, ".")
		    && !g_str_equal (dname, "..")) {
			data_cache_expire (cdc, dpath, NULL, -1, TRUE);
		}

		g_free (dpath);
	}

	g_dir_close (dir);
	g_free (base_dir);
}

static void
data_cache_foreach_remove (CamelDataCache *cdc,
			   const gchar *path,
			   CamelDataCacheRemoveFunc func,
			   gpointer user_data)
{
	GDir *dir;
	const gchar *dname;
	struct stat st;
	GIOStream *stream;

	dir = g_dir_open (path, 0, NULL);
	if (!dir)
		return;

	while ((dname = g_dir_read_name (dir))) {
		gchar *filename;

		filename = g_build_filename (path, dname, NULL);

		if (g_stat (filename, &st) == 0
		    && S_ISREG (st.st_mode)
		    && func (cdc, filename, user_data)) {
			g_unlink (filename);
			stream = camel_object_bag_get (cdc->priv->busy_bag, filename);
			if (stream) {
				camel_object_bag_remove (cdc->priv->busy_bag, stream);
				g_object_unref (stream);
			}
		}

		g_free (filename);
	}

	g_dir_close (dir);
}

/**
 * camel_data_cache_foreach_remove:
 * @cdc: a #CamelDataCache
 * @path: Path to the (sub) cache the items exist in
 * @func: (scope call) (closure user_data): a callback to call for each found file in the cache
 * @user_data: user data passed to @func
 *
 * Traverses the @cdc sub-cache identified by @path and calls @func for each found file.
 * If the @func returns %TRUE, then the file is removed, if %FALSE, it's kept in the cache.
 *
 * Since: 3.26
 **/
void
camel_data_cache_foreach_remove (CamelDataCache *cdc,
				 const gchar *path,
				 CamelDataCacheRemoveFunc func,
				 gpointer user_data)
{
	gchar *base_dir;
	GDir *dir;
	const gchar *dname;
	struct stat st;

	g_return_if_fail (CAMEL_IS_DATA_CACHE (cdc));
	g_return_if_fail (path != NULL);
	g_return_if_fail (func != NULL);

	base_dir = g_build_filename (cdc->priv->path, path, NULL);

	dir = g_dir_open (base_dir, 0, NULL);
	if (dir == NULL) {
		g_free (base_dir);
		return;
	}

	while ((dname = g_dir_read_name (dir))) {
		gchar *dpath;

		dpath = g_build_filename (base_dir, dname, NULL);

		if (g_stat (dpath, &st) == 0
		    && S_ISDIR (st.st_mode)
		    && !g_str_equal (dname, ".")
		    && !g_str_equal (dname, "..")) {
			data_cache_foreach_remove (cdc, dpath, func, user_data);
		}

		g_free (dpath);
	}

	g_dir_close (dir);
	g_free (base_dir);
}
